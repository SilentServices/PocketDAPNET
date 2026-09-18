/*
 * PocketDAPNET - open-source POCSAG transceiver and DAPNET node firmware
 * Copyright (C) 2026 DM1PWN and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <FS.h>
#include <LittleFS.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <TinyGPSPlus.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

#include "defaults.h"
#include "app_info.h"
#include "dapnet_client.h"

// T-Beam AXP2101 V1.2 / SX1278
static constexpr int PIN_I2C_SDA   = 21;
static constexpr int PIN_I2C_SCL   = 22;
static constexpr int PIN_LORA_SCK  = 5;
static constexpr int PIN_LORA_MISO = 19;
static constexpr int PIN_LORA_MOSI = 27;
static constexpr int PIN_LORA_CS   = 18;
static constexpr int PIN_LORA_RST  = 23;
static constexpr int PIN_LORA_DIO0 = 26;
static constexpr int PIN_LORA_DIO1 = 33;
static constexpr int PIN_LORA_DIO2 = 32;
static constexpr int PIN_USER_BUTTON = 38;
static constexpr int PIN_GPS_RX = 34;   // ESP32 RX <- GNSS TX
static constexpr int PIN_GPS_TX = 12;   // ESP32 TX -> GNSS RX
static constexpr uint8_t OLED_ADDR = 0x3C;
static constexpr uint8_t DS3231_ADDR = 0x68;
static constexpr uint32_t RTC_RESYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;

static constexpr size_t MAX_RX_RICS = 32;
static constexpr uint32_t EXACT_RIC_MASK = 0x1FFFFFu;

struct AppConfig {
  String wifiMode = DEFAULT_WIFI_MODE;
  String staSsid;
  String staPassword;
  String apSsid = DEFAULT_AP_SSID;
  String apPassword = DEFAULT_AP_PASSWORD;

  bool txInhibit = DEFAULT_TX_INHIBIT;
  String webUsername = DEFAULT_WEB_USERNAME;
  String webPassword = DEFAULT_WEB_PASSWORD;
  bool apiEnabled = DEFAULT_API_ENABLED;
  String apiToken = DEFAULT_API_TOKEN;

  bool stationIdEnabled = DEFAULT_STATION_ID_ENABLED;
  uint16_t stationIdIntervalMin = DEFAULT_STATION_ID_INTERVAL_MIN;

  float frequencyMHz = DEFAULT_POCSAG_FREQUENCY_MHZ;
  float rxCorrectionMHz = DEFAULT_RX_FREQ_CORR_MHZ;
  float txCorrectionMHz = DEFAULT_TX_FREQ_CORR_MHZ;
  uint16_t baud = DEFAULT_POCSAG_BAUD;
  uint16_t shiftHz = DEFAULT_POCSAG_SHIFT_HZ;
  int8_t txPowerDbm = DEFAULT_TX_POWER_DBM;
  bool invert = DEFAULT_POCSAG_INVERT;
  uint32_t ownRic = DEFAULT_OWN_RIC;
  String rxRics = DEFAULT_RX_RICS;
  bool debugAllRics = false;
  String ledMode = DEFAULT_LED_MODE;

  String ntpProvider = DEFAULT_NTP_PROVIDER;
  String ntpCustomServer = DEFAULT_NTP_CUSTOM_SERVER;

  bool dapnetEnabled = DEFAULT_DAPNET_ENABLED;
  String dapnetHost = DEFAULT_DAPNET_HOST;
  uint16_t dapnetPort = DEFAULT_DAPNET_PORT;
  String dapnetCallsign = DEFAULT_DAPNET_CALLSIGN;
  String dapnetAuthKey = DEFAULT_DAPNET_AUTHKEY;
  String dapnetTimeslots = DEFAULT_DAPNET_TIMESLOTS;
};

struct HistoryEntry {
  uint32_t sequence = 0;
  uint32_t uptimeSeconds = 0;
  int64_t timestamp = 0;
  uint32_t ric = 0;
  int16_t rssi10 = 0;
  uint16_t baud = 0;
  int8_t txPowerDbm = 0;
  uint8_t type = 0;
  uint8_t function = 0;
  uint8_t speedCode = 0;
  uint8_t flags = 0;
  char source[16] = {0};
  char status[32] = {0};
  char message[241] = {0};
};

static constexpr uint8_t HISTORY_FLAG_TIME_VALID = 0x01;
static constexpr uint8_t HISTORY_FLAG_CONFIGURED = 0x02;
static constexpr uint8_t HISTORY_FLAG_SUCCESS = 0x04;
static constexpr size_t HISTORY_SIZE = 30;
static constexpr uint32_t HISTORY_MAGIC = 0x50444831u; // PDH1
static constexpr uint16_t HISTORY_FORMAT_VERSION = 1;

struct HistoryFileHeader {
  uint32_t magic = HISTORY_MAGIC;
  uint16_t version = HISTORY_FORMAT_VERSION;
  uint16_t recordSize = sizeof(HistoryEntry);
  uint16_t capacity = HISTORY_SIZE;
  uint16_t rxHead = 0, rxCount = 0;
  uint16_t txHead = 0, txCount = 0;
  uint16_t dapnetHead = 0, dapnetCount = 0;
  uint32_t rxSequence = 0, txSequence = 0, dapnetSequence = 0;
  uint32_t crc32 = 0;
};

AppConfig cfg;
Preferences prefs;
WebServer server(80);
XPowersPMU pmu;
SX1278 radio = new Module(PIN_LORA_CS, PIN_LORA_DIO0, PIN_LORA_RST, PIN_LORA_DIO1);
PagerClient pager(&radio);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);
DapnetClient dapnet;

static constexpr size_t DAPNET_QUEUE_SIZE = 16;
DapnetMessage dapnetQueue[DAPNET_QUEUE_SIZE];
size_t dapnetQueueHead = 0;
size_t dapnetQueueTail = 0;
size_t dapnetQueueCount = 0;
uint32_t dapnetTxCount = 0;
uint32_t dapnetTxFailCount = 0;
uint32_t dapnetDropCount = 0;
uint32_t dapnetLastTxMillis = 0;
String dapnetLastMessage;
uint32_t dapnetLastRic = 0;

uint32_t lastStationIdMillis = 0;
bool stationIdQueued = false;
String generatedWebPassword;

static constexpr size_t DEBUG_LOG_SIZE = 96;
String debugLogLines[DEBUG_LOG_SIZE];
size_t debugLogHead = 0;
size_t debugLogCount = 0;
uint32_t debugLogSequence = 0;
uint32_t lastSchedulerLogMillis = 0;
int lastSchedulerSlot = -2;
String lastSchedulerReason;

uint32_t rxAddresses[MAX_RX_RICS] = {0};
uint32_t rxMasks[MAX_RX_RICS] = {0};
size_t rxAddressCount = 0;
bool receiverRunning = false;
uint32_t rxDecodeCount = 0;
uint32_t rxRestartCount = 0;
int16_t lastRxState = RADIOLIB_ERR_NONE;
bool fallbackApRunning = false;
bool displayAvailable = false;
bool gpsHasFix = false;
bool gpsHasTime = false;
double gpsLat = 0.0;
double gpsLon = 0.0;
double gpsAltM = 0.0;
uint32_t gpsSatellites = 0;
double gpsHdop = 0.0;
uint32_t gpsLastFixMillis = 0;
uint32_t gpsLastSyncMillis = 0;
String lastTimeSource = "unsynced";
bool rtcPresent = false;
bool rtcValid = false;
time_t rtcLastSyncEpoch = 0;
uint32_t rtcLastSyncMillis = 0;
volatile bool ntpSyncPending = false;
time_t ntpLastSyncEpoch = 0;
String activeNtpServer;
uint32_t ledMessageUntil = 0;

String lastRxMessage;
uint32_t lastRxRic = 0;
float lastRxRssi = 0;
String lastTxStatus = "not sent yet";

HistoryEntry rxHistory[HISTORY_SIZE];
HistoryEntry txHistory[HISTORY_SIZE];
HistoryEntry dapnetHistory[HISTORY_SIZE];
size_t rxHistoryHead = 0, rxHistoryCount = 0;
size_t txHistoryHead = 0, txHistoryCount = 0;
size_t dapnetHistoryHead = 0, dapnetHistoryCount = 0;
uint32_t rxSequence = 0, txSequence = 0, dapnetHistorySequence = 0;
bool littleFsAvailable = false;
String csrfToken;
uint32_t apiRequestTimes[10] = {0};
size_t apiRequestIndex = 0;

uint8_t displayPage = 0;
static constexpr uint8_t DISPLAY_PAGE_COUNT = 6;
uint32_t lastDisplayUpdate = 0;
int lastButtonState = HIGH;
uint32_t buttonDownAt = 0;
bool buttonLongHandled = false;

// Runtime health diagnostics. RTC no-init memory survives watchdog/software resets
// without being reinitialized, allowing the next boot to report the operation
// in which the main loop stalled.
static constexpr uint32_t HEALTH_MAGIC = 0x50444E54u;  // "PDNT"
RTC_NOINIT_ATTR uint32_t healthMagic;
RTC_NOINIT_ATTR char healthLastStage[40];
uint32_t loopLastMs = 0;
uint32_t loopMaxMs = 0;
uint32_t loopCounter = 0;
String bootPreviousStage;

static bool webUsesDefaultPassword();
static bool txBlocked();
static uint16_t dapnetBaud(uint8_t speedCode);

static void setHealthStage(const char *stage) {
  if (!stage) return;
  strncpy(healthLastStage, stage, sizeof(healthLastStage) - 1);
  healthLastStage[sizeof(healthLastStage) - 1] = '\0';
}

static void initLoopWatchdog() {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t config = {};
  config.timeout_ms = 15000;
  config.idle_core_mask = 0;
  config.trigger_panic = true;
  esp_err_t err = esp_task_wdt_init(&config);
  if (err == ESP_ERR_INVALID_STATE) err = esp_task_wdt_reconfigure(&config);
  esp_err_t addErr = esp_task_wdt_add(NULL);
  Serial.printf("[WATCHDOG] init=%d add=%d timeout=15s\n", (int)err, (int)addErr);
#else
  esp_err_t err = esp_task_wdt_init(15, true);
  esp_err_t addErr = esp_task_wdt_add(NULL);
  Serial.printf("[WATCHDOG] init=%d add=%d timeout=15s\n", (int)err, (int)addErr);
#endif
}

static void feedLoopWatchdog() {
  esp_task_wdt_reset();
}

static void initMemoryReservations() {
  // These strings are updated repeatedly for the lifetime of the device.
  // Reserving once avoids long-term heap fragmentation from log/history churn.
  for (size_t i = 0; i < DEBUG_LOG_SIZE; ++i) debugLogLines[i].reserve(224);
  lastRxMessage.reserve(256);
  lastTxStatus.reserve(96);
  dapnetLastMessage.reserve(256);
  lastSchedulerReason.reserve(128);
  activeNtpServer.reserve(96);
}

static void addDebugLog(const String &category, const String &message) {
  String stamp;
  if (time(nullptr) > 1700000000) {
    struct tm tmv;
    time_t now = time(nullptr);
    localtime_r(&now, &tmv);
    char b[16];
    strftime(b, sizeof(b), "%H:%M:%S", &tmv);
    stamp = b;
  } else {
    stamp = String("+") + String(millis() / 1000UL) + "s";
  }
  String line = String(++debugLogSequence) + " " + stamp + " [" + category + "] " + message;
  if (line.length() > 220) line = line.substring(0, 220);
  debugLogLines[debugLogHead] = line;
  debugLogHead = (debugLogHead + 1) % DEBUG_LOG_SIZE;
  if (debugLogCount < DEBUG_LOG_SIZE) debugLogCount++;
}

static void dapnetWebLog(const String &message) {
  addDebugLog("DAPNET", message);
}

static void clearDebugLog() {
  for (size_t i = 0; i < DEBUG_LOG_SIZE; ++i) debugLogLines[i] = "";
  debugLogHead = 0;
  debugLogCount = 0;
  addDebugLog("SYSTEM", "debug log cleared");
}

static String htmlEscape(const String &s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    switch (s[i]) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += s[i]; break;
    }
  }
  return out;
}

static String jsonEscape(const String &s) {
  // Encode arbitrary received bytes as valid JSON/UTF-8. POCSAG payloads can
  // contain control or non-ASCII bytes; emitting those bytes verbatim can make
  // response.json() fail in the browser and leave the UI stuck at Loading....
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    const uint8_t c = static_cast<uint8_t>(s[i]);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        // Keep printable 7-bit ASCII as-is. Escape controls, DEL, and all
        // high-bit bytes so malformed/non-UTF-8 payloads cannot break JSON.
        if (c < 0x20 || c >= 0x7F) {
          out += "\\u00";
          out += hex[(c >> 4) & 0x0F];
          out += hex[c & 0x0F];
        } else {
          out += static_cast<char>(c);
        }
        break;
    }
  }
  return out;
}

static String displaySafeText(const String &s) {
  // Human-readable representation for web message history. Keep normal ASCII
  // unchanged and render non-printable/raw bytes visibly as \xNN.
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(s[i]);
    if (c >= 0x20 && c <= 0x7E) {
      out += static_cast<char>(c);
    } else {
      out += "\\x";
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

static uint32_t parseRic(String s) {
  s.trim();
  if (!s.length()) return 0;
  uint32_t v = strtoul(s.c_str(), nullptr, 10);
  return (v <= RADIOLIB_PAGER_ADDRESS_MAX) ? v : 0;
}

static bool parseRxRics() {
  rxAddressCount = 0;
  String input = cfg.rxRics;
  input.replace(";", ",");
  input.replace("\n", ",");
  input.replace("\r", ",");
  int start = 0;
  while (start < input.length() && rxAddressCount < MAX_RX_RICS) {
    int comma = input.indexOf(',', start);
    if (comma < 0) comma = input.length();
    String token = input.substring(start, comma);
    token.trim();
    if (token.length()) {
      uint32_t ric = parseRic(token);
      if (ric > 0 || token == "0") {
        bool duplicate = false;
        for (size_t i = 0; i < rxAddressCount; i++) {
          if (rxAddresses[i] == ric) duplicate = true;
        }
        if (!duplicate) {
          rxAddresses[rxAddressCount] = ric;
          rxMasks[rxAddressCount] = EXACT_RIC_MASK;
          rxAddressCount++;
        }
      }
    }
    start = comma + 1;
  }
  return rxAddressCount > 0;
}

static String formatUptime(uint32_t seconds) {
  uint32_t h = seconds / 3600;
  uint32_t m = (seconds % 3600) / 60;
  uint32_t s = seconds % 60;
  char buf[20];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
  return String(buf);
}

static String ipInfo() {
  String s;
  if (WiFi.status() == WL_CONNECTED) s += "STA " + WiFi.localIP().toString();
  if (fallbackApRunning || cfg.wifiMode == "ap") {
    if (s.length()) s += " / ";
    s += "AP " + WiFi.softAPIP().toString();
  }
  if (!s.length()) s = "offline";
  return s;
}

static bool isConfiguredRic(uint32_t ric) {
  for (size_t i = 0; i < rxAddressCount; i++) {
    if (rxAddresses[i] == ric) return true;
  }
  return false;
}

static bool systemTimeValid() {
  return time(nullptr) > 1700000000;
}

static String formatLocalTime(time_t t) {
  if (t <= 0) return "--";
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
  return String(buf);
}

static String currentLocalTime() {
  return systemTimeValid() ? formatLocalTime(time(nullptr)) : String("unsynced");
}

static uint32_t crc32Bytes(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
  }
  return ~crc;
}

static void copyHistoryField(char *dst, size_t dstSize, const String &value) {
  if (!dstSize) return;
  const size_t len = min(value.length(), dstSize - 1);
  memcpy(dst, value.c_str(), len);
  dst[len] = '\0';
}

static uint32_t historyHeaderCrc(const HistoryFileHeader &header) {
  HistoryFileHeader copy = header;
  copy.crc32 = 0;
  return crc32Bytes(reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

static void buildHistoryHeader(HistoryFileHeader &h) {
  h = HistoryFileHeader();
  h.rxHead = rxHistoryHead; h.rxCount = rxHistoryCount;
  h.txHead = txHistoryHead; h.txCount = txHistoryCount;
  h.dapnetHead = dapnetHistoryHead; h.dapnetCount = dapnetHistoryCount;
  h.rxSequence = rxSequence; h.txSequence = txSequence; h.dapnetSequence = dapnetHistorySequence;
  h.crc32 = historyHeaderCrc(h);
}

static bool persistHistoryFull() {
  if (!littleFsAvailable) return false;
  File file = LittleFS.open("/history.bin.tmp", "w");
  if (!file) return false;
  HistoryFileHeader header; buildHistoryHeader(header);
  bool ok = file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  if (ok) ok = file.write(reinterpret_cast<const uint8_t*>(rxHistory), sizeof(rxHistory)) == sizeof(rxHistory);
  if (ok) ok = file.write(reinterpret_cast<const uint8_t*>(txHistory), sizeof(txHistory)) == sizeof(txHistory);
  if (ok) ok = file.write(reinterpret_cast<const uint8_t*>(dapnetHistory), sizeof(dapnetHistory)) == sizeof(dapnetHistory);
  file.flush(); file.close();
  if (!ok) { LittleFS.remove("/history.bin.tmp"); return false; }
  LittleFS.remove("/history.bin");
  return LittleFS.rename("/history.bin.tmp", "/history.bin");
}

static bool persistHistorySlot(uint8_t category, size_t index) {
  if (!littleFsAvailable || index >= HISTORY_SIZE) return false;
  if (!LittleFS.exists("/history.bin") && !persistHistoryFull()) return false;
  File file = LittleFS.open("/history.bin", "r+");
  if (!file) return false;
  HistoryFileHeader header; buildHistoryHeader(header);
  const HistoryEntry *entry = nullptr;
  if (category == 0) entry = &rxHistory[index];
  else if (category == 1) entry = &txHistory[index];
  else if (category == 2) entry = &dapnetHistory[index];
  const size_t recordIndex = (size_t)category * HISTORY_SIZE + index;
  const size_t offset = sizeof(HistoryFileHeader) + recordIndex * sizeof(HistoryEntry);
  bool ok = entry != nullptr && file.seek(offset, SeekSet) && file.write(reinterpret_cast<const uint8_t*>(entry), sizeof(HistoryEntry)) == sizeof(HistoryEntry);
  // Commit metadata last so a power loss cannot point the ring head at a record
  // that was never written completely.
  if (ok) ok = file.seek(0, SeekSet) && file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  file.flush(); file.close();
  return ok;
}

static bool restoreHistory() {
  if (!littleFsAvailable || !LittleFS.exists("/history.bin")) return false;
  File file = LittleFS.open("/history.bin", "r");
  if (!file) return false;
  HistoryFileHeader h{};
  bool ok = file.read(reinterpret_cast<uint8_t*>(&h), sizeof(h)) == sizeof(h);
  ok = ok && h.magic == HISTORY_MAGIC && h.version == HISTORY_FORMAT_VERSION &&
       h.recordSize == sizeof(HistoryEntry) && h.capacity == HISTORY_SIZE &&
       h.rxHead < HISTORY_SIZE && h.txHead < HISTORY_SIZE && h.dapnetHead < HISTORY_SIZE &&
       h.rxCount <= HISTORY_SIZE && h.txCount <= HISTORY_SIZE && h.dapnetCount <= HISTORY_SIZE &&
       h.crc32 == historyHeaderCrc(h);
  if (ok) ok = file.read(reinterpret_cast<uint8_t*>(rxHistory), sizeof(rxHistory)) == sizeof(rxHistory);
  if (ok) ok = file.read(reinterpret_cast<uint8_t*>(txHistory), sizeof(txHistory)) == sizeof(txHistory);
  if (ok) ok = file.read(reinterpret_cast<uint8_t*>(dapnetHistory), sizeof(dapnetHistory)) == sizeof(dapnetHistory);
  file.close();
  if (!ok) return false;
  rxHistoryHead=h.rxHead; rxHistoryCount=h.rxCount; txHistoryHead=h.txHead; txHistoryCount=h.txCount;
  dapnetHistoryHead=h.dapnetHead; dapnetHistoryCount=h.dapnetCount;
  rxSequence=h.rxSequence; txSequence=h.txSequence; dapnetHistorySequence=h.dapnetSequence;
  return true;
}

static void initHistoryStorage() {
  littleFsAvailable = LittleFS.begin(true);
  if (!littleFsAvailable) {
    Serial.println("[HISTORY] LittleFS unavailable; using RAM-only history");
    addDebugLog("HISTORY", "LittleFS unavailable; RAM-only");
    return;
  }
  const bool restored = restoreHistory();
  if (!restored) { LittleFS.remove("/history.bin"); persistHistoryFull(); }
  Serial.printf("[HISTORY] %s RX=%u TX=%u DAPNET=%u\n", restored ? "restored" : "empty", (unsigned)rxHistoryCount, (unsigned)txHistoryCount, (unsigned)dapnetHistoryCount);
}

static void fillHistoryEntry(HistoryEntry &e, uint32_t sequence, uint32_t ric, const String &message) {
  memset(&e, 0, sizeof(e));
  e.sequence = sequence;
  e.uptimeSeconds = millis() / 1000UL;
  if (systemTimeValid()) { e.timestamp = (int64_t)time(nullptr); e.flags |= HISTORY_FLAG_TIME_VALID; }
  e.ric = ric;
  copyHistoryField(e.message, sizeof(e.message), message);
}

static void addRxHistory(uint32_t ric, float rssi, const String &message) {
  const size_t slot = rxHistoryHead;
  HistoryEntry &e = rxHistory[slot];
  fillHistoryEntry(e, ++rxSequence, ric, message);
  e.rssi10 = (int16_t)lroundf(rssi * 10.0f);
  e.baud = cfg.baud;
  if (isConfiguredRic(ric)) e.flags |= HISTORY_FLAG_CONFIGURED;
  copyHistoryField(e.source, sizeof(e.source), "RF-RX");
  copyHistoryField(e.status, sizeof(e.status), "received");
  rxHistoryHead = (rxHistoryHead + 1) % HISTORY_SIZE;
  if (rxHistoryCount < HISTORY_SIZE) rxHistoryCount++;
  persistHistorySlot(0, slot);
}

static void addTxHistory(uint32_t ric, const String &message, uint16_t baud, const char *source, bool success, const String &status) {
  const size_t slot = txHistoryHead;
  HistoryEntry &e = txHistory[slot];
  fillHistoryEntry(e, ++txSequence, ric, message);
  e.baud = baud; e.txPowerDbm = cfg.txPowerDbm;
  if (success) e.flags |= HISTORY_FLAG_SUCCESS;
  copyHistoryField(e.source, sizeof(e.source), String(source ? source : "?"));
  copyHistoryField(e.status, sizeof(e.status), status);
  txHistoryHead = (txHistoryHead + 1) % HISTORY_SIZE;
  if (txHistoryCount < HISTORY_SIZE) txHistoryCount++;
  persistHistorySlot(1, slot);
}

static void addDapnetHistory(const DapnetMessage &msg, const String &status) {
  const size_t slot = dapnetHistoryHead;
  HistoryEntry &e = dapnetHistory[slot];
  fillHistoryEntry(e, ++dapnetHistorySequence, msg.ric, msg.text);
  e.type=msg.type; e.function=msg.function; e.speedCode=msg.speedCode; e.baud=dapnetBaud(msg.speedCode);
  copyHistoryField(e.source, sizeof(e.source), "DAPNET");
  copyHistoryField(e.status, sizeof(e.status), status);
  dapnetHistoryHead = (dapnetHistoryHead + 1) % HISTORY_SIZE;
  if (dapnetHistoryCount < HISTORY_SIZE) dapnetHistoryCount++;
  persistHistorySlot(2, slot);
}

static void updateDapnetHistoryStatus(uint32_t ric, const String &message, const String &status) {
  for (size_t i=0; i<dapnetHistoryCount; ++i) {
    const size_t pos=(dapnetHistoryHead+HISTORY_SIZE-1-i)%HISTORY_SIZE;
    HistoryEntry &e=dapnetHistory[pos];
    if (e.ric==ric && String(e.message)==message) { copyHistoryField(e.status,sizeof(e.status),status); persistHistorySlot(2, pos); return; }
  }
}

static const HistoryEntry* historyNewest(const HistoryEntry *entries, size_t head, size_t count, size_t index) {
  if (index >= count) return nullptr;
  const size_t pos=(head+HISTORY_SIZE-1-index)%HISTORY_SIZE;
  return &entries[pos];
}

static void clearAllHistory() {
  memset(rxHistory,0,sizeof(rxHistory)); memset(txHistory,0,sizeof(txHistory)); memset(dapnetHistory,0,sizeof(dapnetHistory));
  rxHistoryHead=rxHistoryCount=txHistoryHead=txHistoryCount=dapnetHistoryHead=dapnetHistoryCount=0;
  rxSequence=txSequence=dapnetHistorySequence=0;
  if (littleFsAvailable) LittleFS.remove("/history.bin");
}

// Persistence
static void loadConfig() {
  prefs.begin("tbdapnet", true);
  cfg.wifiMode = prefs.getString("wmode", DEFAULT_WIFI_MODE);
  cfg.staSsid = prefs.getString("wssid", "");
  cfg.staPassword = prefs.getString("wpass", "");
  cfg.apSsid = prefs.getString("apssid", DEFAULT_AP_SSID);
  cfg.apPassword = prefs.getString("appass", DEFAULT_AP_PASSWORD);
  cfg.txInhibit = prefs.getBool("txinhib", DEFAULT_TX_INHIBIT);
  cfg.webUsername = prefs.getString("webuser", DEFAULT_WEB_USERNAME);
  cfg.webPassword = prefs.getString("webpass", DEFAULT_WEB_PASSWORD);
  cfg.apiEnabled = prefs.getBool("apien", DEFAULT_API_ENABLED);
  cfg.apiToken = prefs.getString("apitoken", DEFAULT_API_TOKEN);
  cfg.stationIdEnabled = prefs.getBool("idenable", DEFAULT_STATION_ID_ENABLED);
  cfg.stationIdIntervalMin = prefs.getUShort("idint", DEFAULT_STATION_ID_INTERVAL_MIN);
  if (cfg.stationIdIntervalMin < 1 || cfg.stationIdIntervalMin > 60) cfg.stationIdIntervalMin = DEFAULT_STATION_ID_INTERVAL_MIN;
  cfg.frequencyMHz = prefs.getFloat("freq", DEFAULT_POCSAG_FREQUENCY_MHZ);
  // Migration from v0.3: old global correction becomes TX correction.
  float oldCorrection = prefs.getFloat("fcorr", DEFAULT_TX_FREQ_CORR_MHZ);
  cfg.rxCorrectionMHz = prefs.getFloat("rxcorr", DEFAULT_RX_FREQ_CORR_MHZ);
  cfg.txCorrectionMHz = prefs.getFloat("txcorr", oldCorrection);
  cfg.baud = prefs.getUShort("baud", DEFAULT_POCSAG_BAUD);
  cfg.shiftHz = prefs.getUShort("shift", DEFAULT_POCSAG_SHIFT_HZ);
  cfg.txPowerDbm = prefs.getChar("txpwr", DEFAULT_TX_POWER_DBM);
  if (!((cfg.txPowerDbm >= 2 && cfg.txPowerDbm <= 17) || cfg.txPowerDbm == 20)) cfg.txPowerDbm = DEFAULT_TX_POWER_DBM;
  cfg.invert = prefs.getBool("invert", DEFAULT_POCSAG_INVERT);
  cfg.ownRic = prefs.getUInt("ownric", DEFAULT_OWN_RIC);
  cfg.rxRics = prefs.getString("rxrics", DEFAULT_RX_RICS);
  cfg.debugAllRics = prefs.getBool("debugall", false);
  cfg.ledMode = prefs.getString("ledmode", DEFAULT_LED_MODE);
  if (cfg.ledMode != "off" && cfg.ledMode != "notifications" && cfg.ledMode != "status") cfg.ledMode = DEFAULT_LED_MODE;
  cfg.ntpProvider = prefs.getString("ntpprov", DEFAULT_NTP_PROVIDER);
  cfg.ntpCustomServer = prefs.getString("ntpcustom", DEFAULT_NTP_CUSTOM_SERVER);
  cfg.dapnetEnabled = prefs.getBool("denable", DEFAULT_DAPNET_ENABLED);
  cfg.dapnetHost = prefs.getString("dhost", DEFAULT_DAPNET_HOST);
  cfg.dapnetPort = prefs.getUShort("dport", DEFAULT_DAPNET_PORT);
  cfg.dapnetCallsign = prefs.getString("dcall", DEFAULT_DAPNET_CALLSIGN);
  cfg.dapnetAuthKey = prefs.getString("dkey", DEFAULT_DAPNET_AUTHKEY);
  cfg.dapnetTimeslots = prefs.getString("dslots", DEFAULT_DAPNET_TIMESLOTS);
  prefs.end();
}

static void saveConfig() {
  prefs.begin("tbdapnet", false);
  prefs.putString("wmode", cfg.wifiMode);
  prefs.putString("wssid", cfg.staSsid);
  prefs.putString("wpass", cfg.staPassword);
  prefs.putString("apssid", cfg.apSsid);
  prefs.putString("appass", cfg.apPassword);
  prefs.putBool("txinhib", cfg.txInhibit);
  prefs.putString("webuser", cfg.webUsername);
  prefs.putString("webpass", cfg.webPassword);
  prefs.putBool("apien", cfg.apiEnabled);
  prefs.putString("apitoken", cfg.apiToken);
  prefs.putBool("idenable", cfg.stationIdEnabled);
  prefs.putUShort("idint", cfg.stationIdIntervalMin);
  prefs.putFloat("freq", cfg.frequencyMHz);
  prefs.putFloat("rxcorr", cfg.rxCorrectionMHz);
  prefs.putFloat("txcorr", cfg.txCorrectionMHz);
  prefs.putUShort("baud", cfg.baud);
  prefs.putUShort("shift", cfg.shiftHz);
  prefs.putChar("txpwr", cfg.txPowerDbm);
  prefs.putBool("invert", cfg.invert);
  prefs.putUInt("ownric", cfg.ownRic);
  prefs.putString("rxrics", cfg.rxRics);
  prefs.putBool("debugall", cfg.debugAllRics);
  prefs.putString("ledmode", cfg.ledMode);
  prefs.putString("ntpprov", cfg.ntpProvider);
  prefs.putString("ntpcustom", cfg.ntpCustomServer);
  prefs.putBool("denable", cfg.dapnetEnabled);
  prefs.putString("dhost", cfg.dapnetHost);
  prefs.putUShort("dport", cfg.dapnetPort);
  prefs.putString("dcall", cfg.dapnetCallsign);
  prefs.putString("dkey", cfg.dapnetAuthKey);
  prefs.putString("dslots", cfg.dapnetTimeslots);
  prefs.end();
}

// GPS, time and status LED
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097LL + (int)doe - 719468LL;
}

static time_t gpsUtcEpoch(int y, int mo, int d, int h, int mi, int sec) {
  return (time_t)(daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400LL + h * 3600LL + mi * 60LL + sec);
}

static void restoreStatusLed() {
  if (cfg.ledMode == "off" || cfg.ledMode == "notifications") {
    pmu.setChargingLedMode(XPOWERS_CHG_LED_OFF);
  } else if (receiverRunning) {
    pmu.setChargingLedMode(XPOWERS_CHG_LED_BLINK_1HZ);
  } else {
    pmu.setChargingLedMode(XPOWERS_CHG_LED_OFF);
  }
}

static void setTxLed(bool active) {
  if (cfg.ledMode == "off") return;
  if (active) pmu.setChargingLedMode(XPOWERS_CHG_LED_ON);
  else restoreStatusLed();
}

static void signalConfiguredMessage() {
  if (cfg.ledMode == "off") return;
  pmu.setChargingLedMode(XPOWERS_CHG_LED_BLINK_4HZ);
  ledMessageUntil = millis() + 1800UL;
}

static bool i2cDevicePresent(uint8_t addr);

static uint8_t bcdToDec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t decToBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static bool plausibleEpoch(time_t t) {
  return t >= 1704067200LL && t < 4102444800LL; // 2024-01-01 .. 2099-12-31
}

static bool ds3231ReadReg(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)DS3231_ADDR, 1) != 1) return false;
  value = Wire.read();
  return true;
}

static bool ds3231WriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(reg); Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool ds3231ReadTime(time_t &out) {
  uint8_t status = 0;
  if (!ds3231ReadReg(0x0F, status) || (status & 0x80)) return false; // oscillator stopped
  Wire.beginTransmission(DS3231_ADDR); Wire.write((uint8_t)0x00);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)DS3231_ADDR, 7) != 7) return false;
  uint8_t sec=bcdToDec(Wire.read() & 0x7F);
  uint8_t min=bcdToDec(Wire.read() & 0x7F);
  uint8_t hrRaw=Wire.read();
  uint8_t hour;
  if (hrRaw & 0x40) {
    hour=bcdToDec(hrRaw & 0x1F);
    bool pm=hrRaw & 0x20;
    if (hour == 12) hour = 0;
    if (pm) hour += 12;
  } else hour=bcdToDec(hrRaw & 0x3F);
  (void)Wire.read(); // day of week
  uint8_t day=bcdToDec(Wire.read() & 0x3F);
  uint8_t monRaw=Wire.read();
  uint8_t month=bcdToDec(monRaw & 0x1F);
  int year=2000 + bcdToDec(Wire.read());
  out=gpsUtcEpoch(year, month, day, hour, min, sec);
  return plausibleEpoch(out);
}

static bool ds3231WriteTime(time_t utc) {
  if (!rtcPresent || !plausibleEpoch(utc)) return false;
  struct tm tmv; gmtime_r(&utc, &tmv);
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.write(decToBcd(tmv.tm_sec)); Wire.write(decToBcd(tmv.tm_min)); Wire.write(decToBcd(tmv.tm_hour));
  Wire.write(decToBcd((tmv.tm_wday == 0 ? 7 : tmv.tm_wday)));
  Wire.write(decToBcd(tmv.tm_mday)); Wire.write(decToBcd(tmv.tm_mon + 1)); Wire.write(decToBcd((tmv.tm_year + 1900) - 2000));
  if (Wire.endTransmission() != 0) return false;
  uint8_t status=0;
  if (ds3231ReadReg(0x0F, status)) ds3231WriteReg(0x0F, status & ~0x80);
  rtcValid = true; rtcLastSyncEpoch = utc; rtcLastSyncMillis = millis();
  addDebugLog("TIME", "DS3231 updated from " + lastTimeSource);
  return true;
}

static void initRtc() {
  rtcPresent = i2cDevicePresent(DS3231_ADDR);
  if (!rtcPresent) { addDebugLog("TIME", "DS3231 not detected"); return; }
  addDebugLog("TIME", "DS3231 detected at 0x68");
  time_t rt=0; rtcValid = ds3231ReadTime(rt);
  if (!rtcValid) { addDebugLog("TIME", "DS3231 time invalid/OSF set; not used"); return; }
  struct timeval tv={rt,0}; settimeofday(&tv,nullptr); lastTimeSource="RTC";
  addDebugLog("TIME", "system initialized from DS3231: " + formatLocalTime(rt));
}

static void updateRtcFromTrustedTime(bool force=false) {
  if (!rtcPresent || !systemTimeValid()) return;
  time_t now=time(nullptr), rt=0; bool have=ds3231ReadTime(rt);
  long long offset = have ? llabs((long long)now-(long long)rt) : 999999;
  if (force || !have || offset > 2 || millis()-rtcLastSyncMillis >= RTC_RESYNC_INTERVAL_MS) ds3231WriteTime(now);
}

static void ntpSyncCallback(struct timeval *tv) { (void)tv; ntpSyncPending = true; }

static String selectedNtpServer() {
  if (cfg.ntpProvider == "depool") return "de.pool.ntp.org";
  if (cfg.ntpProvider == "google") return "time.google.com";
  if (cfg.ntpProvider == "cloudflare") return "time.cloudflare.com";
  if (cfg.ntpProvider == "windows") return "time.windows.com";
  if (cfg.ntpProvider == "custom" && cfg.ntpCustomServer.length()) return cfg.ntpCustomServer;
  return "pool.ntp.org";
}

static void initGps() {
  gpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  Serial.println("[GPS] GNSS serial started at 9600 baud");
}

static void startNtp() {
  if (WiFi.status() != WL_CONNECTED) return;
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
  activeNtpServer = selectedNtpServer();
  sntp_set_time_sync_notification_cb(ntpSyncCallback);
  configTime(0, 0, activeNtpServer.c_str(), "pool.ntp.org", "time.cloudflare.com");
  addDebugLog("TIME", "NTP requested: " + activeNtpServer);
  Serial.printf("[TIME] NTP requested from %s; timezone Europe/Berlin\n", activeNtpServer.c_str());
}

static void processNtpSync() {
  if (!ntpSyncPending) return;
  ntpSyncPending = false;
  time_t now=time(nullptr);
  if (!plausibleEpoch(now)) { addDebugLog("TIME", "NTP callback received but timestamp implausible"); return; }
  ntpLastSyncEpoch=now; lastTimeSource="NTP";
  addDebugLog("TIME", "NTP synchronized via " + activeNtpServer + ": " + formatLocalTime(now));
  updateRtcFromTrustedTime(true);
}

static void processGps() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  if (gps.location.isValid()) { gpsHasFix=true; gpsLat=gps.location.lat(); gpsLon=gps.location.lng(); gpsLastFixMillis=millis(); }
  if (gps.altitude.isValid()) gpsAltM=gps.altitude.meters();
  if (gps.satellites.isValid()) gpsSatellites=gps.satellites.value();
  if (gps.hdop.isValid()) gpsHdop=gps.hdop.hdop();
  gpsHasTime = gps.date.isValid() && gps.time.isValid();
  if (gpsHasTime && gps.time.age() < 2000 && gps.date.age() < 2000) {
    time_t gt=gpsUtcEpoch(gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second());
    if (!plausibleEpoch(gt)) return;
    time_t now=time(nullptr);
    if (!systemTimeValid() || llabs((long long)gt-(long long)now) > 2 || millis()-gpsLastSyncMillis > 3600000UL) {
      struct timeval tv={gt,0}; settimeofday(&tv,nullptr); gpsLastSyncMillis=millis(); lastTimeSource="GPS";
      addDebugLog("TIME", "synchronized from GPS: " + formatLocalTime(gt));
      updateRtcFromTrustedTime(!rtcValid);
    }
  }
}

// Display
static bool i2cDevicePresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void initDisplay() {
  pinMode(PIN_USER_BUTTON, INPUT);
  if (!i2cDevicePresent(OLED_ADDR)) {
    Serial.println("[OLED] SSD1306 not detected at 0x3C");
    return;
  }
  display.setI2CAddress(OLED_ADDR << 1);
  display.begin();
  display.setFont(u8g2_font_6x10_tf);
  displayAvailable = true;
  Serial.println("[OLED] SSD1306 detected");
}

static void drawLine(uint8_t y, const String &text) {
  display.drawStr(0, y, text.substring(0, 21).c_str());
}

static String effectiveDapnetTimeslots();

static void drawDisplay() {
  setHealthStage("display");
  if (!displayAvailable) return;
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  if (generatedWebPassword.length() && millis() < 60000UL) {
    drawLine(9, "PocketDAPNET AUTH");
    display.drawHLine(0, 12, 128);
    drawLine(27, "User: " + cfg.webUsername);
    drawLine(40, "Pass:");
    drawLine(53, generatedWebPassword);
    display.sendBuffer();
    lastDisplayUpdate = millis();
    return;
  }
  String title = "PocketDAPNET " + String(displayPage + 1) + "/" + String(DISPLAY_PAGE_COUNT);
  drawLine(9, title);
  display.drawHLine(0, 12, 128);

  if (displayPage == 0) {
    drawLine(24, String("RX:") + (receiverRunning ? "on" : "off") + (txBlocked() ? " TX:INHIB" : " TX:ready"));
    drawLine(35, "F: " + String(cfg.frequencyMHz, 4));
    drawLine(46, "Baud: " + String(cfg.baud));
    drawLine(57, "RICs: " + String((unsigned)rxAddressCount));
  } else if (displayPage == 1) {
    drawLine(24, "Last RX");
    drawLine(35, "RIC: " + String(lastRxRic));
    drawLine(46, "RSSI: " + String(lastRxRssi, 1) + " dBm");
    drawLine(57, lastRxMessage.length() ? lastRxMessage : "-- none --");
  } else if (displayPage == 2) {
    drawLine(24, "WiFi: " + String(WiFi.status() == WL_CONNECTED ? "STA connected" : (fallbackApRunning || cfg.wifiMode == "ap" ? "AP active" : "offline")));
    drawLine(35, WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
    if (WiFi.status() == WL_CONNECTED) drawLine(46, "RSSI: " + String(WiFi.RSSI()) + " dBm");
    drawLine(57, "Uptime " + formatUptime(millis()/1000UL));
  } else if (displayPage == 3) {
    drawLine(24, "RX corr: " + String(cfg.rxCorrectionMHz, 4));
    drawLine(35, "TX osc: " + String(cfg.txCorrectionMHz, 4));
    drawLine(46, "Shift: " + String(cfg.shiftHz) + " Hz");
    drawLine(57, "TX power: " + String(cfg.txPowerDbm) + " dBm");
  } else if (displayPage == 4) {
    drawLine(24, gpsHasFix ? "GPS: FIX" : "GPS: no fix");
    if (gpsHasFix) {
      drawLine(35, String(gpsLat, 5) + "," + String(gpsLon, 5));
      drawLine(46, "Sat:" + String(gpsSatellites) + " HDOP:" + String(gpsHdop, 1));
    } else {
      drawLine(35, "Sat: " + String(gpsSatellites));
      drawLine(46, "Waiting for GNSS");
    }
    drawLine(57, systemTimeValid() ? currentLocalTime().substring(11) : "Time unsynced");
  } else {
    drawLine(24, "DAPNET: " + dapnet.status());
    drawLine(35, cfg.dapnetHost.length() ? cfg.dapnetHost : "No server");
    drawLine(46, "Cfg:" + cfg.dapnetTimeslots + " Srv:" + dapnet.timeslots());
    drawLine(57, "Eff:" + effectiveDapnetTimeslots() + " Q:" + String((unsigned)dapnetQueueCount));
  }
  display.sendBuffer();
  lastDisplayUpdate = millis();
}

static void handleButton() {
  int state = digitalRead(PIN_USER_BUTTON);
  uint32_t now = millis();
  if (lastButtonState == HIGH && state == LOW) {
    buttonDownAt = now;
    buttonLongHandled = false;
  }
  if (state == LOW && !buttonLongHandled && now - buttonDownAt > 800) {
    displayPage = (displayPage + DISPLAY_PAGE_COUNT - 1) % DISPLAY_PAGE_COUNT;
    buttonLongHandled = true;
    drawDisplay();
  }
  if (lastButtonState == LOW && state == HIGH) {
    if (!buttonLongHandled && now - buttonDownAt > 30) {
      displayPage = (displayPage + 1) % DISPLAY_PAGE_COUNT;
      drawDisplay();
    }
  }
  lastButtonState = state;
}

// Power + radio
static void initPower() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setTimeOut(50);  // never let an I2C peripheral stall the main loop indefinitely
  if (!pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL)) {
    Serial.println("[AXP2101] PMU not found");
    while (true) delay(1000);
  }
  pmu.setALDO2Voltage(3300);
  pmu.enableALDO2();
  pmu.setALDO3Voltage(3300);
  pmu.enableALDO3();
  delay(200);
  Serial.println("[AXP2101] SX1278 power enabled on ALDO2, GPS on ALDO3");
}

static float rxFrequency() { return cfg.frequencyMHz + cfg.rxCorrectionMHz; }
static float txFrequency() { return cfg.frequencyMHz + cfg.txCorrectionMHz; }

static int16_t configurePager(float frequency, uint16_t baud = 0) {
  if (!baud) baud = cfg.baud;
  int16_t state = pager.begin(frequency, baud, cfg.invert, cfg.shiftHz);
  Serial.printf("[POCSAG] pager.begin %.6f MHz @ %u -> %d\n", frequency, baud, state);
  return state;
}

static int16_t startReceiver() {
  if (!parseRxRics()) {
    receiverRunning = false;
    return RADIOLIB_ERR_INVALID_ADDRESS_WIDTH;
  }
  int16_t state = configurePager(rxFrequency());
  if (state != RADIOLIB_ERR_NONE) return state;
  if (cfg.debugAllRics) {
    // mask 0 matches every decoded POCSAG address in RadioLib
    state = pager.startReceive(PIN_LORA_DIO2, 0u, 0u);
    Serial.printf("[POCSAG] startReceive returned %d (DEBUG: ALL RICs)\n", state);
  } else {
    state = pager.startReceive(PIN_LORA_DIO2, rxAddresses, rxMasks, rxAddressCount);
    Serial.printf("[POCSAG] startReceive returned %d (%u RICs)\n", state, (unsigned)rxAddressCount);
  }
  receiverRunning = (state == RADIOLIB_ERR_NONE);
  lastRxState = state;
  if (receiverRunning && !ledMessageUntil) restoreStatusLed();
  return state;
}

static void stopReceiver() {
  radio.standby();
  receiverRunning = false;
  if (!ledMessageUntil) restoreStatusLed();
}

static bool sendPocsagAdvanced(uint32_t ric, const String &message, uint16_t baud,
                               uint8_t encoding, uint8_t function, const char *source) {
  if (txBlocked()) {
    lastTxStatus = webUsesDefaultPassword() ? "TX blocked: change default web password" : "TX inhibited";
    addDebugLog("TX", "blocked by TX inhibit");
    return false;
  }
  if (ric > RADIOLIB_PAGER_ADDRESS_MAX || !message.length()) {
    lastTxStatus = "invalid RIC or empty message";
    return false;
  }
  const uint8_t pocsagFrame = (uint8_t)(ric & 0x07u);
  const uint32_t addressField = ric >> 3;
  String ricHex = String(ric, HEX);
  ricHex.toUpperCase();
  const char *encodingName = encoding == RADIOLIB_PAGER_ASCII ? "ASCII" :
                             (encoding == RADIOLIB_PAGER_BCD ? "BCD" : "OTHER");
  String diag = "source=" + String(source ? source : "?") +
                " RIC=" + String(ric) + " (0x" + ricHex + ")" +
                " frame=" + String(pocsagFrame) +
                " addressField=" + String(addressField) +
                " baud=" + String(baud) +
                " encoding=" + String(encodingName) +
                " function=" + String(function) +
                " len=" + String(message.length()) +
                " base=" + String(cfg.frequencyMHz, 6) +
                " txCorr=" + String(cfg.txCorrectionMHz, 6) +
                " programmedCenter=" + String(txFrequency(), 6) +
                " toneLow=" + String(txFrequency() - (float(cfg.shiftHz) / 1000000.0f), 6) +
                " toneHigh=" + String(txFrequency() + (float(cfg.shiftHz) / 1000000.0f), 6) +
                " shift=" + String(cfg.shiftHz) +
                " power=" + String(cfg.txPowerDbm);
  addDebugLog("POCSAG", diag);
  Serial.printf("[POCSAG TX DIAG] %s\n", diag.c_str());

  bool resumeRx = true;
  stopReceiver();
  ledMessageUntil = 0;
  setTxLed(true);
  int16_t state = configurePager(txFrequency(), baud);
  if (state == RADIOLIB_ERR_NONE) {
    state = radio.setOutputPower(cfg.txPowerDbm);
    Serial.printf("[POCSAG TX] output power %d dBm, setOutputPower=%d\n", cfg.txPowerDbm, state);
  }
  if (state == RADIOLIB_ERR_NONE) {
    String msg = message;
    Serial.printf("[POCSAG TX] %.6f MHz @ %u RIC=%lu func=%u message=%s\n",
                  txFrequency(), baud, (unsigned long)ric, function, msg.c_str());
    state = pager.transmit(msg, ric, encoding, function);
  }
  lastTxStatus = "RadioLib code " + String(state);
  Serial.printf("[POCSAG TX] result=%d\n", state);
  if (resumeRx) {
    delay(20);
    int16_t rxState = startReceiver();
    if (rxState != RADIOLIB_ERR_NONE) Serial.printf("[POCSAG] RX resume failed: %d\n", rxState);
  }
  setTxLed(false);
  addTxHistory(ric, message, baud, source, state == RADIOLIB_ERR_NONE, lastTxStatus);
  if (state == RADIOLIB_ERR_NONE) {
    String idCall = cfg.dapnetCallsign;
    idCall.trim(); idCall.toUpperCase();
    String sentText = message; sentText.trim(); sentText.toUpperCase();
    if (ric == DEFAULT_STATION_ID_RIC && idCall.length() && sentText == idCall) {
      lastStationIdMillis = millis();
      stationIdQueued = false;
      addDebugLog("ID", "station identification transmitted");
    }
  }
  drawDisplay();
  return state == RADIOLIB_ERR_NONE;
}

static bool sendPocsag(uint32_t ric, const String &message) {
  return sendPocsagAdvanced(ric, message, cfg.baud, RADIOLIB_PAGER_ASCII, RADIOLIB_PAGER_FUNC_AUTO, "MANUAL");
}

static uint16_t dapnetBaud(uint8_t speedCode) {
  // The deployed DAPNET transmitter protocol documents 1 = 1200 bit/s.
  // 0/2 are accepted for compatibility with legacy 512/2400 implementations.
  if (speedCode == 0) return 512;
  if (speedCode == 1) return 1200;
  if (speedCode == 2) return 2400;
  return cfg.baud;
}

static int dapnetHexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static String normalizeDapnetTimeslots(String value) {
  value.trim();
  value.toUpperCase();
  bool seen[16] = {false};
  for (size_t i = 0; i < value.length(); ++i) {
    int n = dapnetHexNibble(value[i]);
    if (n >= 0 && n < 16) seen[n] = true;
  }
  String out;
  for (int n = 0; n < 16; ++n) {
    if (seen[n]) out += "0123456789ABCDEF"[n];
  }
  return out;
}

static bool configuredDapnetSlotAllowed(uint8_t slot) {
  if (slot >= 16 || !cfg.dapnetTimeslots.length()) return false;
  char needle = "0123456789ABCDEF"[slot];
  return cfg.dapnetTimeslots.indexOf(needle) >= 0;
}

static String effectiveDapnetTimeslots() {
  String out;
  for (uint8_t slot = 0; slot < 16; ++slot) {
    if (configuredDapnetSlotAllowed(slot) && dapnet.slotAllowed(slot)) {
      out += "0123456789ABCDEF"[slot];
    }
  }
  return out.length() ? out : "--";
}

static bool enqueueDapnetMessage(const DapnetMessage &message) {
  if (dapnetQueueCount >= DAPNET_QUEUE_SIZE) {
    dapnetDropCount++;
    addDebugLog("QUEUE", "DROP queue full RIC=" + String(message.ric));
    addDapnetHistory(message, "dropped: queue full");
    return false;
  }
  dapnetQueue[dapnetQueueHead] = message;
  dapnetQueueHead = (dapnetQueueHead + 1) % DAPNET_QUEUE_SIZE;
  dapnetQueueCount++;
  dapnetLastMessage = message.text;
  dapnetLastRic = message.ric;
  addDebugLog("QUEUE", "enqueue RIC=" + String(message.ric) + " depth=" + String((unsigned)dapnetQueueCount));
  addDapnetHistory(message, "queued");
  return true;
}

static bool currentDapnetSlotAllowed() {
  if (!systemTimeValid() || !dapnet.isOnline()) return false;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  uint64_t epochMs = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
  uint8_t slot = (uint8_t)((epochMs / 6400ULL) % 16ULL);
  return configuredDapnetSlotAllowed(slot) && dapnet.slotAllowed(slot);
}

static int currentDapnetSlot() {
  if (!systemTimeValid()) return -1;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  uint64_t epochMs = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
  return (int)((epochMs / 6400ULL) % 16ULL);
}

static uint32_t currentDapnetSlotElapsedMs() {
  if (!systemTimeValid()) return 0;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  uint64_t epochMs = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
  return (uint32_t)(epochMs % 6400ULL);
}

static uint32_t estimatePocsagTxMs(const DapnetMessage &msg) {
  const uint16_t baud = dapnetBaud(msg.speedCode);
  if (!baud) return 6400;

  // Conservative estimate based on the POCSAG preamble plus complete 544-bit batches.
  // Address codeword position is determined by the low 3 RIC bits (frame 0..7).
  const uint32_t bitsPerChar = (msg.type == 5) ? 4u : 7u;
  const uint32_t messageWords = (msg.text.length() * bitsPerChar + 19u) / 20u;
  const uint32_t addressSlot = (msg.ric & 0x07u) * 2u;
  const uint32_t occupiedWords = addressSlot + 1u + messageWords;
  uint32_t batches = (occupiedWords + 15u) / 16u;
  if (batches < 1u) batches = 1u;

  const uint32_t bits = 576u + batches * 544u;
  return (bits * 1000u + baud - 1u) / baud;
}

static uint32_t currentDapnetSlotRemainingMs() {
  const uint32_t elapsed = currentDapnetSlotElapsedMs();
  return elapsed < 6400u ? (6400u - elapsed) : 0u;
}

static String dapnetSchedulerReason() {
  if (!dapnetQueueCount) return "queue empty";
  if (txBlocked()) return webUsesDefaultPassword() ? "TX blocked: default password" : "TX inhibited";
  if (!dapnet.isOnline()) return "DAPNET not online";
  if (!systemTimeValid()) return "time not synchronized";
  int slot = currentDapnetSlot();
  if (slot < 0) return "slot unavailable";
  if (!configuredDapnetSlotAllowed((uint8_t)slot)) return "slot " + String(slot, HEX) + " not configured";
  if (!dapnet.slotAllowed((uint8_t)slot)) return "slot " + String(slot, HEX) + " not allowed by server";
  if (dapnetQueueCount) {
    const DapnetMessage &next = dapnetQueue[dapnetQueueTail];
    const uint32_t needMs = estimatePocsagTxMs(next) + 250u;
    const uint32_t remainMs = currentDapnetSlotRemainingMs();
    if (remainMs < needMs) return "not enough slot time (need " + String(needMs) + "ms, have " + String(remainMs) + "ms)";
  }
  return "TX permitted";
}

static void logSchedulerState(bool force = false) {
  if (!dapnetQueueCount) return;
  int slot = currentDapnetSlot();
  String reason = dapnetSchedulerReason();
  if (force || slot != lastSchedulerSlot || reason != lastSchedulerReason || millis() - lastSchedulerLogMillis >= 5000UL) {
    String slotText = slot < 0 ? "--" : String(slot, HEX);
    slotText.toUpperCase();
    addDebugLog("SCHED", "slot=" + slotText + " elapsed=" + String(currentDapnetSlotElapsedMs()) + "ms remain=" + String(currentDapnetSlotRemainingMs()) + "ms cfg=" + cfg.dapnetTimeslots + " srv=" + dapnet.timeslots() + " eff=" + effectiveDapnetTimeslots() + " queue=" + String((unsigned)dapnetQueueCount) + " -> " + reason);
    lastSchedulerSlot = slot;
    lastSchedulerReason = reason;
    lastSchedulerLogMillis = millis();
  }
}

static void processDapnetQueue() {
  if (!dapnetQueueCount) return;
  logSchedulerState();
  if (txBlocked()) return;
  if (!dapnet.isOnline()) return;
  if (!systemTimeValid()) return;  // slot scheduling requires synchronized time
  if (!currentDapnetSlotAllowed()) return;
  const DapnetMessage &nextMsg = dapnetQueue[dapnetQueueTail];
  const uint32_t estimatedTxMs = estimatePocsagTxMs(nextMsg);
  const uint32_t remainingMs = currentDapnetSlotRemainingMs();
  const uint32_t slotGuardMs = 250u;
  if (remainingMs < estimatedTxMs + slotGuardMs) {
    logSchedulerState(true);
    return;
  }
  // Avoid starting two pages back-to-back at the same instant in a busy loop.
  if (millis() - dapnetLastTxMillis < 250UL) return;

  DapnetMessage msg = dapnetQueue[dapnetQueueTail];
  dapnetQueueTail = (dapnetQueueTail + 1) % DAPNET_QUEUE_SIZE;
  dapnetQueueCount--;

  uint8_t encoding = msg.type == 5 ? RADIOLIB_PAGER_BCD : RADIOLIB_PAGER_ASCII;
  uint8_t function = msg.function <= 3 ? msg.function : RADIOLIB_PAGER_FUNC_AUTO;
  uint16_t baud = dapnetBaud(msg.speedCode);
  addDebugLog("TX", "start slot=" + String(currentDapnetSlot(), HEX) + " RIC=" + String(msg.ric) + " frame=" + String(msg.ric & 7u) + " addr=" + String(msg.ric >> 3) + " baud=" + String(baud) + " est=" + String(estimatedTxMs) + "ms remain=" + String(remainingMs) + "ms queue=" + String((unsigned)dapnetQueueCount));
  Serial.printf("[DAPNET TX] slot=%d RIC=%lu type=%u baud=%u func=%u queue=%u\n",
                currentDapnetSlot(), (unsigned long)msg.ric, msg.type, baud, function,
                (unsigned)dapnetQueueCount);
  bool ok = sendPocsagAdvanced(msg.ric, msg.text, baud, encoding, function, "DAPNET");
  dapnetLastTxMillis = millis();
  updateDapnetHistoryStatus(msg.ric, msg.text, ok ? "rf tx success" : "rf tx failed");
  if (ok) { dapnetTxCount++; addDebugLog("TX", "success RIC=" + String(msg.ric)); }
  else { dapnetTxFailCount++; addDebugLog("TX", "FAILED RIC=" + String(msg.ric) + " status=" + lastTxStatus); }
}

static void initRadio() {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_CS);
  pinMode(PIN_LORA_CS, OUTPUT);
  digitalWrite(PIN_LORA_CS, HIGH);
  int16_t state = radio.beginFSK(rxFrequency(), (float)cfg.baud / 1000.0f,
                                 (float)cfg.shiftHz / 1000.0f, 25.0, cfg.txPowerDbm, 16, false);
  Serial.printf("[SX1278] beginFSK %.6f MHz returned %d\n", rxFrequency(), state);
  if (state != RADIOLIB_ERR_NONE) while (true) delay(1000);
  startReceiver();
}

// WiFi
static void startAccessPoint(bool fallback) {
  WiFi.mode(fallback ? WIFI_AP_STA : WIFI_AP);
  String ssid = cfg.apSsid.length() ? cfg.apSsid : DEFAULT_AP_SSID;
  bool ok = cfg.apPassword.length() >= 8 ? WiFi.softAP(ssid.c_str(), cfg.apPassword.c_str()) : WiFi.softAP(ssid.c_str());
  fallbackApRunning = ok;
  Serial.printf("[WiFi] %s AP %s, IP %s\n", fallback ? "fallback" : "local", ok ? "started" : "FAILED", WiFi.softAPIP().toString().c_str());
}

static void initWifi() {
  fallbackApRunning = false;
  if (cfg.wifiMode == "ap") { startAccessPoint(false); return; }
  if (!cfg.staSsid.length()) { startAccessPoint(true); return; }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.staSsid.c_str(), cfg.staPassword.c_str());
  Serial.printf("[WiFi] connecting to %s", cfg.staSsid.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < DEFAULT_STA_TIMEOUT_MS) { delay(250); Serial.print('.'); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) Serial.printf("[WiFi] connected, IP %s\n", WiFi.localIP().toString().c_str());
  else startAccessPoint(true);
}

// Authentication, API and local station identification
static String randomToken(size_t len) {
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  String out;
  out.reserve(len);
  for (size_t i = 0; i < len; ++i) out += alphabet[esp_random() % (sizeof(alphabet) - 1)];
  return out;
}

static void prepareResponseHeaders(bool html=false) {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("Referrer-Policy", "no-referrer");
  server.sendHeader("X-Frame-Options", "DENY");
  server.sendHeader("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
  server.sendHeader("Cross-Origin-Resource-Policy", "same-origin");
  if (html) server.sendHeader("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; object-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
}
static void sendHtml(int code,const String &body){prepareResponseHeaders(true);server.send(code,"text/html; charset=utf-8",body);}
static void sendJson(int code,const String &body){prepareResponseHeaders(false);server.send(code,"application/json; charset=utf-8",body);}
static void sendText(int code,const String &body){prepareResponseHeaders(false);server.send(code,"text/plain; charset=utf-8",body);}
static void redirectTo(const String &where){prepareResponseHeaders(false);server.sendHeader("Location",where);server.send(303);}

static bool requireCsrf(){
  if(server.hasArg("csrf") && server.arg("csrf")==csrfToken) return true;
  addDebugLog("SECURITY","CSRF validation failed for "+server.uri()); sendText(403,"CSRF validation failed"); return false;
}
static bool apiRateAllowed(){
  const uint32_t now=millis(); size_t recent=0;
  for(size_t i=0;i<10;++i) if(apiRequestTimes[i] && (uint32_t)(now-apiRequestTimes[i])<10000UL) recent++;
  if(recent>=10) return false; apiRequestTimes[apiRequestIndex]=now; apiRequestIndex=(apiRequestIndex+1)%10; return true;
}
static bool printableAscii(const String &v,size_t maxLen,bool allowEmpty=false){
  if((!allowEmpty&&!v.length())||v.length()>maxLen)return false; for(size_t i=0;i<v.length();++i){uint8_t c=(uint8_t)v[i];if(c<0x20||c>0x7E)return false;}return true;
}
static bool parseRicStrict(String v,uint32_t &ric){v.trim();if(!v.length()||v.length()>7)return false;for(size_t i=0;i<v.length();++i)if(!isDigit(v[i]))return false;unsigned long n=strtoul(v.c_str(),nullptr,10);if(n>RADIOLIB_PAGER_ADDRESS_MAX)return false;ric=(uint32_t)n;return true;}
static bool validHostname(String v,bool allowEmpty=true){v.trim();if(!v.length())return allowEmpty;if(v.length()>253)return false;for(size_t i=0;i<v.length();++i){char c=v[i];if(!(isalnum((unsigned char)c)||c=='.'||c=='-'))return false;}return true;}
static bool validRicList(String input){
  if(input.length()>512)return false;input.replace(";",",");input.replace("\n",",");input.replace("\r",",");int start=0,count=0;
  while(start<input.length()){int comma=input.indexOf(',',start);if(comma<0)comma=input.length();String token=input.substring(start,comma);token.trim();if(token.length()){uint32_t ric=0;if(!parseRicStrict(token,ric))return false;if(++count>32)return false;}start=comma+1;}return true;
}
static String validateSettingsInput(){
  String v=server.arg("wssid");if(v.length()>32)return "Client SSID exceeds 32 characters";
  v=server.arg("wpass");if(v.length()>64)return "Client password is too long";
  v=server.arg("apssid");if(!v.length()||v.length()>32)return "AP SSID must be 1-32 characters";
  v=server.arg("appass");if(v.length()&&(v.length()<8||v.length()>63))return "AP password must be 8-63 characters";
  v=server.arg("webuser");v.trim();if(!printableAscii(v,32))return "Web username must be 1-32 printable ASCII characters";
  v=server.arg("webpass");if(v.length()&&(!printableAscii(v,64)||v.length()<8))return "Web password must be 8-64 printable ASCII characters";
  v=server.arg("apitoken");if(v.length()&&(!printableAscii(v,128)||v.length()<16))return "API token must be 16-128 printable ASCII characters";
  float f=server.arg("freq").toFloat();if(f<400.0f||f>510.0f)return "Base frequency must be within 400-510 MHz";
  float rc=server.arg("rxcorr").toFloat(),tc=server.arg("txcorr").toFloat();if(rc<-.1f||rc>.1f||tc<-.1f||tc>.1f)return "Frequency correction must be within +/-0.1 MHz";
  int b=server.arg("baud").toInt();if(!(b==512||b==1200||b==2400))return "Baud must be 512, 1200 or 2400";
  int sh=server.arg("shift").toInt();if(sh<1000||sh>10000)return "FSK shift must be 1000-10000 Hz";
  int pw=server.arg("txpower").toInt();if(!((pw>=2&&pw<=17)||pw==20))return "Unsupported TX power";
  uint32_t r=0;if(!parseRicStrict(server.arg("ownric"),r))return "Invalid own RIC";
  if(!validRicList(server.arg("rxrics")))return "Receive RIC list contains invalid values or more than 32 RICs";
  if(!validHostname(server.arg("dhost")))return "Invalid DAPNET host";int port=server.arg("dport").toInt();if(port<1||port>65535)return "Invalid DAPNET port";
  v=server.arg("dcall");v.trim();if(v.length()>16||(v.length()&&!printableAscii(v,16)))return "Invalid callsign/node name";
  if(!validHostname(server.arg("ntpcustom")))return "Invalid custom NTP host";
  int ident=server.arg("idint").toInt();if(ident<1||ident>60)return "Station ID interval must be 1-60 minutes";
  if(server.hasArg("denable")){if(!server.arg("dhost").length())return "DAPNET server is required when DAPNET is enabled";if(!server.arg("dcall").length())return "DAPNET callsign/node is required when DAPNET is enabled";if(!server.arg("dkey").length()&&!cfg.dapnetAuthKey.length())return "DAPNET AuthKey is required when DAPNET is enabled";if(!normalizeDapnetTimeslots(server.arg("dslots")).length())return "Assigned DAPNET timeslots are required when DAPNET is enabled";}
  return "";
}

static bool webUsesDefaultPassword() {
  return cfg.webUsername == DEFAULT_WEB_USERNAME && cfg.webPassword == DEFAULT_WEB_PASSWORD;
}

static bool txBlocked() {
  // A publicly-known default password must never permit RF transmission.
  return cfg.txInhibit || webUsesDefaultPassword();
}

static void ensureWebCredentials() {
  bool changed = false;
  if (!cfg.webUsername.length()) { cfg.webUsername = DEFAULT_WEB_USERNAME; changed = true; }
  if (!cfg.webPassword.length()) { cfg.webPassword = DEFAULT_WEB_PASSWORD; changed = true; }
  if (webUsesDefaultPassword()) {
    cfg.txInhibit = true;
    generatedWebPassword = DEFAULT_WEB_PASSWORD;  // shown briefly on OLED for first-time users
    changed = true;
    Serial.printf("[AUTH] default credentials active: %s / %s\n", cfg.webUsername.c_str(), cfg.webPassword.c_str());
    Serial.println("[AUTH] RF TX remains blocked until the default web password is changed");
  }
  if (changed) saveConfig();
}

static bool requireWebAuth() {
  if (server.authenticate(cfg.webUsername.c_str(), cfg.webPassword.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "PocketDAPNET");
  return false;
}

static bool constantTimeEquals(const String &a, const String &b) {
  const size_t maxLen = max(a.length(), b.length());
  uint8_t diff = (uint8_t)(a.length() ^ b.length());
  for (size_t i = 0; i < maxLen; ++i) {
    const uint8_t av = i < a.length() ? (uint8_t)a[i] : 0;
    const uint8_t bv = i < b.length() ? (uint8_t)b[i] : 0;
    diff |= av ^ bv;
  }
  return diff == 0;
}

static bool apiAuthorized() {
  if (!cfg.apiEnabled || cfg.apiToken.length() < 16) return false;
  if (!server.hasHeader("Authorization")) return false;
  const String auth = server.header("Authorization");
  const String expected = "Bearer " + cfg.apiToken;
  return constantTimeEquals(auth, expected);
}

static String stationIdCallsign() {
  String call = cfg.dapnetCallsign;
  call.trim(); call.toUpperCase();
  return call;
}

static void processStationIdentification() {
  // Automatic station identification is only relevant while operating as a
  // DAPNET transmitter. In RX-only operation PocketDAPNET must never key TX.
  if (!cfg.dapnetEnabled) return;
  if (!cfg.stationIdEnabled || txBlocked()) return;
  String call = stationIdCallsign();
  if (!call.length()) return;
  const uint32_t intervalMs = (uint32_t)cfg.stationIdIntervalMin * 60UL * 1000UL;
  if (!lastStationIdMillis) lastStationIdMillis = millis();
  uint32_t dueMs = intervalMs;
  // Give the DAPNET core a short grace period to deliver its regular RIC-8 ID
  // before generating a local fallback, avoiding duplicate identification pages.
  if (cfg.dapnetEnabled && dapnet.isOnline()) dueMs += 30000UL;
  if ((uint32_t)(millis() - lastStationIdMillis) < dueMs || stationIdQueued) return;

  // Queue the local fallback ID so it uses the assigned DAPNET timeslot.
  if (!dapnet.isOnline()) return;
  DapnetMessage id;
  id.type = 6; id.speedCode = 1; id.ric = DEFAULT_STATION_ID_RIC;
  id.function = 3; id.text = call;
  if (enqueueDapnetMessage(id)) {
    stationIdQueued = true;
    addDebugLog("ID", "local fallback identification queued: " + call);
  }
}

// Web UI
static String checked(bool b) { return b ? " checked" : ""; }
static String selected(const String &value, const char *candidate) { return value == candidate ? " selected" : ""; }

static String pageHeader(const String &title) {
  String h;
  h.reserve(5000);
  h += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  h += "<meta name='csrf-token' content='" + htmlEscape(csrfToken) + "'>";
  h += "<title>" + htmlEscape(title) + F("</title><style>");
  h += F("body{font-family:system-ui,sans-serif;max-width:980px;margin:0 auto;padding:0 14px 30px;background:#101114;color:#eee}header{display:flex;align-items:center;justify-content:space-between;position:sticky;top:0;background:#101114;padding:14px 0;z-index:10;border-bottom:1px solid #333}h1{font-size:1.25rem;margin:0}.headStatus{font-size:.82rem;color:#bbb;margin-left:12px;white-space:nowrap}.brand{display:flex;align-items:center;min-width:0}details.menu{position:relative}details.menu summary{list-style:none;font-size:1.7rem;cursor:pointer;padding:2px 10px}details.menu nav{position:absolute;right:0;top:42px;min-width:190px;background:#202228;border:1px solid #444;border-radius:8px;padding:8px;box-shadow:0 8px 24px #0008}details.menu a{display:block;color:#fff;text-decoration:none;padding:10px;border-radius:5px}details.menu a:hover{background:#333}fieldset,.card{margin:16px 0;padding:16px;border:1px solid #45474f;border-radius:9px;background:#181a1f}label{display:block;margin:9px 0 3px}input,select,textarea,button{box-sizing:border-box;width:100%;padding:9px;border-radius:5px;border:1px solid #60636d;background:#22252b;color:#eee}button{cursor:pointer;font-weight:600}.topActions{display:flex;align-items:center;gap:8px}.rebootBtn{width:auto;padding:7px 10px;background:#5b2020;border-color:#8a3a3a}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.statusgrid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.metric{padding:12px;background:#202228;border-radius:7px}.metric small{display:block;color:#aaa}.notice{margin-top:12px;padding:10px;background:#263238;border-radius:6px}.msg{padding:10px 0;border-bottom:1px solid #333}.msg:last-child{border-bottom:0}.msg.new{animation:flash 1.4s ease}.meta{color:#aaa;font-size:.85rem}.ok{color:#9ee6a8}.bad{color:#ff9b9b}@keyframes flash{0%{background:#38533a}100%{background:transparent}}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:8px;border-bottom:1px solid #333;vertical-align:top}small{color:#aaa}@media(max-width:700px){.grid,.statusgrid{grid-template-columns:1fr}table{font-size:.85rem}}");
  h += "</style></head><body><header><div class='brand'><h1>PocketDAPNET</h1><span class='headStatus'><b>v" + String(POCKETDAPNET_VERSION) + " by DM1PWN <small>(" + String(POCKETDAPNET_BUILD_ID) + ")</small></b> &middot; <span id='headTime'>" + htmlEscape(currentLocalTime()) + "</span> &middot; <span id='headSource'>" + htmlEscape(lastTimeSource) + "</span></span></div><div class='topActions'><details class='menu'><summary aria-label='Menu'>&#9776;</summary><nav><a href='/'>Dashboard</a><a href='/messages'>Messages</a><a href='/settings'>Settings</a><a href='/nvs'>NVS / Config</a><a href='/debug'>Debug log</a><a href='/status'>Status JSON</a></nav></details><form method='post' action='/reboot' onsubmit=\"return confirm('Reboot PocketDAPNET now?')\"><button class='rebootBtn' type='submit'>Reboot</button></form></div></header>";
  return h;
}

static String pageFooter() {
  return F("<script>(()=>{const m=document.querySelector('meta[name=csrf-token]'),t=m?m.content:'';document.querySelectorAll('form').forEach(f=>{if((f.method||'').toLowerCase()==='post'&&!f.querySelector('input[name=csrf]')){let i=document.createElement('input');i.type='hidden';i.name='csrf';i.value=t;f.appendChild(i);}});})();async function refreshHeader(){try{let s=await (await fetch('/status',{cache:'no-store'})).json();let t=document.getElementById('headTime'),q=document.getElementById('headSource');if(t)t.textContent=s.localTime;if(q)q.textContent=s.timeSource;}catch(e){console.error('PocketDAPNET header refresh failed:',e);}}refreshHeader();setInterval(refreshHeader,2500);</script></body></html>");
}

static String buildDashboard(const String &notice = "") {
  String h = pageHeader("PocketDAPNET Dashboard");
  if (notice.length()) h += "<div class='notice'>" + htmlEscape(notice) + "</div>";
  h += F("<div class='statusgrid'>");
  h += "<div class='metric'><small>Network</small><span id='stWifi'>" + htmlEscape(ipInfo()) + "</span></div>";
  h += "<div class='metric'><small>Radio</small><span id='stRadio'>" + String(receiverRunning ? "RX active" : "RX idle") + (txBlocked() ? " / TX INHIBITED" : " / TX ready") + "</span></div>";
  h += "<div class='metric'><small>GPS / Time</small><span id='stGps'>" + String(gpsHasFix ? "FIX" : "no fix") + " / " + htmlEscape(lastTimeSource) + "</span></div></div>";

  h += F("<div class='card'><h2>Manual POCSAG send</h2>");
  if (txBlocked()) h += F("<div class='notice'>RF TX is blocked. Clear TX Inhibit and change the default web password before transmitting.</div>");
  h += F("<form method='post' action='/send'>");
  h += "<label>Destination RIC</label><input name='ric' value='" + String(cfg.ownRic) + "'>";
  h += F("<label>Message</label><textarea name='msg' rows='4' required></textarea><button type='submit'>Send message</button></form>");
  h += "<small>TX: " + String(txFrequency(),6) + " MHz / " + String(cfg.txPowerDbm) + " dBm &middot; Last TX: <span id='stTx'>" + htmlEscape(lastTxStatus) + "</span></small></div>";

  h += "<div class='card'><h2>DAPNET transmitter link</h2><div class='statusgrid'>";
  h += "<div class='metric'><small>Status</small><strong id='stDapnet'>" + htmlEscape(dapnet.status()) + "</strong></div>";
  h += "<div class='metric'><small>Configured slots</small><strong id='stCfgSlots'>" + htmlEscape(cfg.dapnetTimeslots) + "</strong></div>";
  h += "<div class='metric'><small>Server slots</small><strong id='stSrvSlots'>" + htmlEscape(dapnet.timeslots()) + "</strong></div>";
  h += "<div class='metric'><small>Effective slots</small><strong id='stEffSlots'>" + htmlEscape(effectiveDapnetTimeslots()) + "</strong></div>";
  h += "<div class='metric'><small>Queue</small><strong id='stDQueue'>" + String((unsigned)dapnetQueueCount) + "</strong></div><div class='metric'><small>Current slot</small><strong id='stDSlot'>--</strong></div><div class='metric'><small>Scheduler</small><strong id='stDReason'>--</strong></div></div>";
  h += "<small>Received from DAPNET: " + String(dapnet.messagesReceived()) + " &middot; RF TX: " + String(dapnetTxCount) + " &middot; TX failures: " + String(dapnetTxFailCount) + "</small>";
  h += "<br><small>System time: <span id='stSysTime'>" + htmlEscape(currentLocalTime()) + "</span> / <span id='stTimeSync'>" + String(systemTimeValid() ? "synchronized" : "UNSYNCED") + "</span> / source <span id='stTimeSource'>" + htmlEscape(lastTimeSource) + "</span></small>";
  h += "<br><small>DAPNET time frame: <span id='stDapTime'>" + htmlEscape(dapnet.masterTimeRaw()) + "</span> &middot; correction: <span id='stDapCorr'>" + htmlEscape(dapnet.clockCorrectionRaw()) + "</span></small></div>";

  h += F("<div class='card'><h2>Recent received messages</h2><div id='recentMessages'><p><small>Loading...</small></p></div>");
  h += F("<p><a href='/messages' style='color:#9ecbff'>Show full message history</a></p></div>");
  h += F("<script>let lastSeq=0;function esc(s){return String(s == null ? '' : s).replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]));}async function refresh(){try{let st=await (await fetch('/status',{cache:'no-store'})).json();document.getElementById('headTime').textContent=st.localTime;document.getElementById('headSource').textContent=st.timeSource;document.getElementById('stWifi').textContent=st.wifi;document.getElementById('stRadio').textContent=(st.receiverRunning?'RX active':'RX idle')+(st.txBlocked?' / TX INHIBITED':' / TX ready');document.getElementById('stGps').textContent=(st.gpsFix?('FIX '+st.gpsLat.toFixed(5)+', '+st.gpsLon.toFixed(5)+' / '):'no fix ')+(st.gpsSatellites||0)+' sat / '+st.timeSource;document.getElementById('stTx').textContent=st.lastTxStatus;document.getElementById('stDapnet').textContent=st.dapnetStatus;document.getElementById('stCfgSlots').textContent=st.dapnetConfiguredSlots;document.getElementById('stSrvSlots').textContent=st.dapnetServerSlots;document.getElementById('stEffSlots').textContent=st.dapnetEffectiveSlots;document.getElementById('stDQueue').textContent=st.dapnetQueue;document.getElementById('stDSlot').textContent=(st.dapnetCurrentSlot<0?'--':st.dapnetCurrentSlotHex+' / '+st.dapnetSlotElapsedMs+' ms');document.getElementById('stDReason').textContent=st.dapnetSchedulerReason;document.getElementById('stSysTime').textContent=st.localTime;document.getElementById('stTimeSync').textContent=st.timeSynchronized?'synchronized':'UNSYNCED';document.getElementById('stTimeSource').textContent=st.timeSource;document.getElementById('stDapTime').textContent=st.dapnetMasterTimeRaw;document.getElementById('stDapCorr').textContent=st.dapnetClockCorrectionRaw;let m=await (await fetch('/api/messages?limit=8',{cache:'no-store'})).json();let box=document.getElementById('recentMessages');if(!m.messages.length){box.innerHTML='<p><small>No messages received since boot.</small></p>';return;}let newest=m.messages[0].sequence;box.innerHTML=m.messages.map((x,i)=>`<div class='msg ${newest>lastSeq&&i==0?'new':''}'><div class='meta'>#${x.sequence} &middot; ${esc(x.time)} &middot; RIC ${x.ric} &middot; ${x.rssi.toFixed(1)} dBm ${x.configuredRic?'&middot; <span class=ok>listed RIC</span>':''}</div><div>${esc(x.message)}</div></div>`).join('');lastSeq=Math.max(lastSeq,newest);}catch(e){console.error('PocketDAPNET dashboard refresh failed:',e);let box=document.getElementById('recentMessages');if(box)box.innerHTML='<p class=\"bad\">Failed to load message history.</p>';}}refresh();setInterval(refresh,2500);</script>");
  return h + pageFooter();
}

static String buildMessages() {
  String h=pageHeader("Message History");
  h+=F("<div class='card'><h2>Message history</h2><p><small>Up to 30 RX, TX and DAPNET entries are persisted across reboots.</small></p><div class='grid'><button type='button' onclick=\"showHistory('rx')\">Received</button><button type='button' onclick=\"showHistory('tx')\">Transmitted</button><button type='button' onclick=\"showHistory('dapnet')\">DAPNET</button></div><h3 id='histTitle'>Received</h3><div id='messageTable'>Loading...</div><form method='post' action='/messages/clear' style='margin-top:16px' onsubmit=\"return confirm('Clear all persistent message histories?')\"><button type='submit'>Clear all history</button></form></div>");
  h+=F("<script>let ht='rx',lastSeq=0;function esc(s){return String(s==null?'':s).replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]));}async function showHistory(t){ht=t;lastSeq=0;document.getElementById('histTitle').textContent=t==='rx'?'Received':t==='tx'?'Transmitted':'DAPNET';await refreshMessages();}async function refreshMessages(){try{let d=await(await fetch('/api/history?type='+encodeURIComponent(ht),{cache:'no-store'})).json(),b=document.getElementById('messageTable');if(!d.messages.length){b.innerHTML='<p>No stored messages.</p>';return;}let newest=d.messages[0].sequence;b.innerHTML='<table><thead><tr><th>#</th><th>Time</th><th>RIC</th><th>Source / status</th><th>Message</th></tr></thead><tbody>'+d.messages.map((x,i)=>`<tr class='${newest>lastSeq&&i===0?'new':''}'><td>${x.sequence}</td><td>${esc(x.time)}</td><td>${x.ric}</td><td>${esc(x.source)}<br><small>${esc(x.status)}${x.baud?' / '+x.baud+' baud':''}</small></td><td>${esc(x.message)}</td></tr>`).join('')+'</tbody></table>';lastSeq=Math.max(lastSeq,newest);}catch(e){console.error('PocketDAPNET history refresh failed:',e);document.getElementById('messageTable').innerHTML='<p class=bad>Failed to load message history.</p>';}}refreshMessages();setInterval(refreshMessages,2500);</script>");
  return h+pageFooter();
}

static String buildDebugPage() {
  String h = pageHeader("Debug log");
  h += F("<div class='card'><h2>Runtime debug log</h2><p><small>Event-based ring buffer. It logs DAPNET frames, queue changes, scheduler decisions and RF TX events; it does not log ISR/bit-level traffic.</small></p><div style='display:flex;gap:10px'><form method='post' action='/debug/clear'><button type='submit'>Clear log</button></form><button onclick=\"refreshLog()\">Refresh</button></div><pre id='debugText' style='white-space:pre-wrap;overflow-wrap:anywhere;background:#0b0c0e;padding:12px;border-radius:7px;max-height:70vh;overflow:auto'>Loading...</pre></div>");
  h += F("<script>async function refreshLog(){try{let d=await (await fetch('/api/debug',{cache:'no-store'})).json();document.getElementById('debugText').textContent=d.lines.join('\\n');}catch(e){console.error('PocketDAPNET debug refresh failed:',e);}}refreshLog();setInterval(refreshLog,1500);</script>");
  return h + pageFooter();
}

static String buildSettings(const String &notice = "") {
  String h = pageHeader("Settings");
  if (notice.length()) h += "<div class='notice'>" + htmlEscape(notice) + "</div>";
  h += F("<form method='post' action='/save'><fieldset><legend>WiFi</legend><label>Mode</label><select name='wmode'>");
  h += "<option value='client'" + selected(cfg.wifiMode,"client") + ">Client (fallback AP on failure)</option>";
  h += "<option value='ap'" + selected(cfg.wifiMode,"ap") + ">Access Point</option></select>";
  h += "<label>Client SSID</label><input name='wssid' value='" + htmlEscape(cfg.staSsid) + "'>";
  h += "<label>Client password</label><input type='password' name='wpass' value='' placeholder='leave blank to keep current'>";
  h += "<label>AP SSID</label><input name='apssid' value='" + htmlEscape(cfg.apSsid) + "'>";
  h += "<label>AP password</label><input type='password' name='appass' value='' placeholder='leave blank to keep current'></fieldset>";

  h += F("<fieldset><legend>POCSAG / Radio</legend><div class='grid'>");
  h += "<div><label>TX Inhibit</label><label><input style='width:auto' type='checkbox' name='txinhib' value='1'" + checked(cfg.txInhibit) + "> Disable all RF transmission</label><small>Safety interlock. RX remains active. Enable before disconnecting or changing the antenna/RF cabling.</small></div>";
  h += "<div><label>Own RIC</label><input name='ownric' value='" + String(cfg.ownRic) + "'></div>";
  h += "<div><label>Base frequency MHz</label><input name='freq' value='" + String(cfg.frequencyMHz,6) + "'></div>";
  h += "<div><label>RX correction MHz</label><input name='rxcorr' value='" + String(cfg.rxCorrectionMHz,6) + "'><small>Applied only while receiving.</small></div>";
  h += "<div><label>TX oscillator correction MHz</label><input name='txcorr' value='" + String(cfg.txCorrectionMHz,6) + "'><small>Compensates measured oscillator error only. Do not enter the POCSAG deviation here; that is configured separately as FSK shift.</small></div>";
  h += "<div><label>Baud</label><select name='baud'><option" + selected(String(cfg.baud),"512") + ">512</option><option" + selected(String(cfg.baud),"1200") + ">1200</option><option" + selected(String(cfg.baud),"2400") + ">2400</option></select></div>";
  h += "<div><label>FSK deviation / shift Hz</label><input name='shift' value='" + String(cfg.shiftHz) + "'></div>";
  h += "<div><label>TX output power</label><select name='txpower'>";
  for (int pwr = 2; pwr <= 17; pwr++) h += "<option value='" + String(pwr) + "'" + (cfg.txPowerDbm == pwr ? " selected" : "") + ">" + String(pwr) + " dBm</option>";
  h += "<option value='20'" + String(cfg.txPowerDbm == 20 ? " selected" : "") + ">20 dBm (high power)</option></select><small>SX1278 PA_BOOST output.</small></div></div>";
  h += "<label><input style='width:auto' type='checkbox' name='invert' value='1'" + checked(cfg.invert) + "> Invert FSK polarity</label>";
  h += "<label><input style='width:auto' type='checkbox' name='debugall' value='1'" + checked(cfg.debugAllRics) + "> Debug RX: receive all RICs</label><small>Promiscuous POCSAG receive. Every decoded RIC is shown and stored in message history.</small>";
  h += "<label>Receive RIC list</label><textarea name='rxrics' rows='5'>" + htmlEscape(cfg.rxRics) + "</textarea><small>Comma, semicolon or newline separated; max. 32 exact RICs.</small></fieldset>";

  h += F("<fieldset><legend>Web / API security</legend>");
  h += "<div class='grid'><div><label>Web username</label><input name='webuser' value='" + htmlEscape(cfg.webUsername) + "'></div>";
  h += "<div><label>New web password</label><input type='password' name='webpass' value='' placeholder='leave blank to keep current'><small>HTTP Basic Authentication protects the complete web UI.</small></div></div>";
  h += "<label><input style='width:auto' type='checkbox' name='apien' value='1'" + checked(cfg.apiEnabled) + "> Enable send API</label>";
  h += "<label>API bearer token</label>"
       "<div style='display:flex;gap:8px'>"
       "<input id='apitoken' type='password' name='apitoken' value='' placeholder='leave blank to keep current'>"
       "<button type='button' style='width:auto' "
       "onclick=\"let a='ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789',"
       "b=new Uint32Array(24);"
       "crypto.getRandomValues(b);"
       "document.getElementById('apitoken').value="
       "Array.from(b,x=>a[x%a.length]).join('')\">"
       "Generate</button>"
       "</div>"
       "<small>Minimum 16 characters. POST /api/send uses Authorization: Bearer &lt;token&gt;.</small>";
  h += F("</fieldset>");

  h += F("<fieldset><legend>Station identification</legend>");
  h += "<label><input style='width:auto' type='checkbox' name='idenable' value='1'" + checked(cfg.stationIdEnabled) + "> Enable local callsign identification fallback</label>";
  h += "<label>Interval (minutes)</label><input name='idint' type='number' min='1' max='60' value='" + String(cfg.stationIdIntervalMin) + "'><small>Uses DAPNET callsign and RIC 8. A received/sent DAPNET RIC-8 identification with the same callsign resets the timer, avoiding duplicate beacons.</small>";
  h += F("</fieldset>");

  h += F("<fieldset><legend>System / LED</legend>");
  h += "<label>Status LED mode</label><select name='ledmode'><option value='off'" + selected(cfg.ledMode,"off") + ">Off</option><option value='notifications'" + selected(cfg.ledMode,"notifications") + ">Notifications only</option><option value='status'" + selected(cfg.ledMode,"status") + ">Status + notifications</option></select>";
  h += F("<small>Notifications only: LED is normally off, on during TX and flashes rapidly for a received configured RIC. Status mode additionally blinks slowly while RX is active.</small></fieldset>");

  h += F("<fieldset><legend>GPS / Time</legend><p>The onboard GNSS receiver is enabled automatically. A DS3231 at I2C address 0x68 is auto-detected and used as boot-time fallback. GPS/NTP update the RTC when available.</p>");
  h += "<label>NTP server</label><select name='ntpprov'>";
  h += "<option value='pool'" + selected(cfg.ntpProvider,"pool") + ">pool.ntp.org</option>";
  h += "<option value='depool'" + selected(cfg.ntpProvider,"depool") + ">de.pool.ntp.org</option>";
  h += "<option value='google'" + selected(cfg.ntpProvider,"google") + ">time.google.com</option>";
  h += "<option value='cloudflare'" + selected(cfg.ntpProvider,"cloudflare") + ">time.cloudflare.com</option>";
  h += "<option value='windows'" + selected(cfg.ntpProvider,"windows") + ">time.windows.com</option>";
  h += "<option value='custom'" + selected(cfg.ntpProvider,"custom") + ">Custom</option></select>";
  h += "<label>Custom NTP server</label><input name='ntpcustom' value='" + htmlEscape(cfg.ntpCustomServer) + "' placeholder='ntp.example.org'>";
  h += F("<small>Selected server is primary; pool.ntp.org and time.cloudflare.com remain fallbacks. Local display time uses Europe/Berlin including DST. GNSS: GPIO34/12, 9600 baud.</small></fieldset>");

  h += F("<fieldset><legend>DAPNET</legend>");
  h += "<label><input style='width:auto' type='checkbox' name='denable' value='1'" + checked(cfg.dapnetEnabled) + "> Enable DAPNET client</label>";
  h += "<div class='grid'><div><label>Server</label><input name='dhost' value='" + htmlEscape(cfg.dapnetHost) + "'></div>";
  h += "<div><label>Port</label><input name='dport' value='" + String(cfg.dapnetPort) + "'></div>";
  h += "<div><label>Callsign / Node</label><input name='dcall' value='" + htmlEscape(cfg.dapnetCallsign) + "'></div>";
  h += "<div><label>Auth key</label><input type='password' name='dkey' value='' placeholder='leave blank to keep current'></div>";
  h += "<div><label>Assigned timeslots</label><input name='dslots' maxlength='16' value='" + htmlEscape(cfg.dapnetTimeslots) + "'><small>Hex slot list from DAPNET registration, e.g. 159D.</small></div></div>";
  h += F("<small>RF transmission is permitted only in the intersection of configured and server-assigned timeslots. If no valid server schedule is received, DAPNET RF TX remains blocked.</small></fieldset>");
  h += F("<button type='submit'>Save configuration and reboot</button></form><form method='post' action='/reboot' style='margin-top:14px'><button type='submit'>Reboot without changes</button></form>");
  h += F("<div class='card'><h2>Maintenance</h2><p><a href='/nvs'>View stored PocketDAPNET configuration (secrets masked)</a></p><form method='post' action='/factory-reset' onsubmit=\"return confirm('Factory reset PocketDAPNET? All stored WiFi, radio and DAPNET settings will be erased.');\"><button type='submit' style='background:#8b1e1e'>Reset to factory defaults</button></form></div>");
  return h + pageFooter();
}

static String maskedSecret(const String &value) {
  if (!value.length()) return "(empty)";
  return "******** (" + String(value.length()) + " chars)";
}

static String buildNvsPage() {
  String h = pageHeader("NVS / Configuration");
  h += F("<div class='card'><h2>Stored PocketDAPNET configuration</h2><p><small>This view shows the known keys stored in the PocketDAPNET NVS namespace. Passwords and authentication keys are intentionally masked.</small></p><table><thead><tr><th>Key</th><th>Value</th></tr></thead><tbody>");
  auto row = [&](const String &key, const String &value) { h += "<tr><td>" + htmlEscape(key) + "</td><td>" + htmlEscape(value) + "</td></tr>"; };
  row("wmode", cfg.wifiMode); row("wssid", cfg.staSsid); row("wpass", maskedSecret(cfg.staPassword));
  row("apssid", cfg.apSsid); row("appass", maskedSecret(cfg.apPassword));
  row("txinhib", cfg.txInhibit ? "true" : "false"); row("defaultWebPassword", webUsesDefaultPassword() ? "true" : "false");
  row("webuser", cfg.webUsername); row("webpass", maskedSecret(cfg.webPassword));
  row("apien", cfg.apiEnabled ? "true" : "false"); row("apitoken", maskedSecret(cfg.apiToken));
  row("idenable", cfg.stationIdEnabled ? "true" : "false"); row("idint", String(cfg.stationIdIntervalMin));
  row("freq", String(cfg.frequencyMHz, 6)); row("rxcorr", String(cfg.rxCorrectionMHz, 6)); row("txcorr", String(cfg.txCorrectionMHz, 6));
  row("baud", String(cfg.baud)); row("shift", String(cfg.shiftHz)); row("txpwr", String(cfg.txPowerDbm)); row("invert", cfg.invert ? "true" : "false");
  row("ownric", String(cfg.ownRic)); row("rxrics", cfg.rxRics); row("debugall", cfg.debugAllRics ? "true" : "false"); row("ledmode", cfg.ledMode);
  row("ntpprov", cfg.ntpProvider); row("ntpcustom", cfg.ntpCustomServer);
  row("denable", cfg.dapnetEnabled ? "true" : "false"); row("dhost", cfg.dapnetHost); row("dport", String(cfg.dapnetPort));
  row("dcall", cfg.dapnetCallsign); row("dkey", maskedSecret(cfg.dapnetAuthKey)); row("dslots", cfg.dapnetTimeslots);
  h += F("</tbody></table><p><small>DAPNET transmit queue and debug log are RAM-only. RX/TX/DAPNET histories are stored in bounded LittleFS ring buffers (30 entries each).</small></p></div>");
  h += F("<div class='card'><h2>Factory reset</h2><p>This erases the entire PocketDAPNET NVS namespace. After reboot the device starts with repository defaults and, without configured WiFi credentials, exposes the fallback setup AP.</p><form method='post' action='/factory-reset' onsubmit=\"return confirm('Factory reset PocketDAPNET? This cannot be undone.');\"><button type='submit' style='background:#8b1e1e'>Reset to factory defaults</button></form></div>");
  return h + pageFooter();
}

static void handleNvs(){sendHtml(200,buildNvsPage());}
static void handleFactoryReset() {
  addDebugLog("SYSTEM", "factory reset requested");
  prefs.begin("tbdapnet", false);
  bool ok = prefs.clear();
  prefs.end();
  clearAllHistory();
  sendHtml(200,pageHeader("Factory reset")+String("<div class='notice'>")+(ok?"NVS and persistent history cleared. Rebooting into factory defaults...":"NVS clear failed. Rebooting...")+"</div>"+pageFooter());
  delay(800);
  ESP.restart();
}

static void handleRoot(){sendHtml(200,buildDashboard());}
static void handleMessages(){sendHtml(200,buildMessages());}
static void handleSettings(){sendHtml(200,buildSettings());}
static void handleDebug(){sendHtml(200,buildDebugPage());}

static void handleSave() {
  String validationError=validateSettingsInput();
  if(validationError.length()){sendHtml(400,buildSettings("Validation error: "+validationError));return;}
  cfg.wifiMode = server.arg("wmode") == "ap" ? "ap" : "client";
  cfg.staSsid = server.arg("wssid"); if(server.arg("wpass").length()) cfg.staPassword = server.arg("wpass");
  cfg.apSsid = server.arg("apssid"); if(server.arg("appass").length()) cfg.apPassword = server.arg("appass");
  cfg.txInhibit = server.hasArg("txinhib");
  cfg.webUsername = server.arg("webuser");
  cfg.webUsername.trim(); if (!cfg.webUsername.length()) cfg.webUsername = DEFAULT_WEB_USERNAME;
  if (server.arg("webpass").length()) cfg.webPassword = server.arg("webpass");
  if (webUsesDefaultPassword()) cfg.txInhibit = true;
  cfg.apiEnabled = server.hasArg("apien");
  if (server.arg("apitoken").length()) cfg.apiToken = server.arg("apitoken");
  if (cfg.apiEnabled && cfg.apiToken.length() < 16) cfg.apiEnabled = false;
  cfg.stationIdEnabled = server.hasArg("idenable");
  cfg.stationIdIntervalMin = (uint16_t)server.arg("idint").toInt();
  if (cfg.stationIdIntervalMin < 1 || cfg.stationIdIntervalMin > 60) cfg.stationIdIntervalMin = DEFAULT_STATION_ID_INTERVAL_MIN;
  cfg.ownRic = parseRic(server.arg("ownric"));
  cfg.frequencyMHz = server.arg("freq").toFloat();
  cfg.rxCorrectionMHz = server.arg("rxcorr").toFloat();
  cfg.txCorrectionMHz = server.arg("txcorr").toFloat();
  cfg.baud = (uint16_t)server.arg("baud").toInt();
  if (cfg.baud != 512 && cfg.baud != 1200 && cfg.baud != 2400) cfg.baud = 1200;
  cfg.shiftHz = (uint16_t)server.arg("shift").toInt();
  if (!cfg.shiftHz) cfg.shiftHz = DEFAULT_POCSAG_SHIFT_HZ;
  cfg.txPowerDbm = (int8_t)server.arg("txpower").toInt();
  if (!((cfg.txPowerDbm >= 2 && cfg.txPowerDbm <= 17) || cfg.txPowerDbm == 20)) cfg.txPowerDbm = DEFAULT_TX_POWER_DBM;
  cfg.invert = server.hasArg("invert");
  cfg.debugAllRics = server.hasArg("debugall");
  cfg.ledMode = server.arg("ledmode");
  cfg.ntpProvider = server.arg("ntpprov");
  cfg.ntpCustomServer = server.arg("ntpcustom");
  if (cfg.ntpProvider != "pool" && cfg.ntpProvider != "depool" && cfg.ntpProvider != "google" && cfg.ntpProvider != "cloudflare" && cfg.ntpProvider != "windows" && cfg.ntpProvider != "custom") cfg.ntpProvider = DEFAULT_NTP_PROVIDER;
  if (cfg.ledMode != "off" && cfg.ledMode != "notifications" && cfg.ledMode != "status") cfg.ledMode = DEFAULT_LED_MODE;
  cfg.rxRics = server.arg("rxrics");
  cfg.dapnetEnabled = server.hasArg("denable");
  cfg.dapnetHost = server.arg("dhost");
  cfg.dapnetPort = (uint16_t)server.arg("dport").toInt();
  cfg.dapnetCallsign = server.arg("dcall"); if(server.arg("dkey").length()) cfg.dapnetAuthKey = server.arg("dkey");
  cfg.dapnetTimeslots = normalizeDapnetTimeslots(server.arg("dslots"));
  if (!cfg.dapnetTimeslots.length()) cfg.dapnetTimeslots = DEFAULT_DAPNET_TIMESLOTS;
  saveConfig();
  sendHtml(200,buildSettings("Configuration saved. Rebooting..."));
  delay(500); ESP.restart();
}

static void handleSend() {
  uint32_t ric=0;String msg=server.arg("msg");
  if(!parseRicStrict(server.arg("ric"),ric)||!printableAscii(msg,240)){sendHtml(400,buildDashboard("Invalid RIC or message. Use 1-240 printable ASCII characters."));return;}
  bool ok=sendPocsag(ric,msg);sendHtml(ok?200:500,buildDashboard(ok?"POCSAG message transmitted.":"POCSAG transmission failed: "+lastTxStatus));
}

static void handleSendApi() {
  if(!apiAuthorized()){sendJson(401,"{\"ok\":false,\"error\":\"unauthorized\"}");return;}
  String contentType=server.header("Content-Type");if(!contentType.startsWith("application/x-www-form-urlencoded")){sendJson(415,"{\"ok\":false,\"error\":\"unsupported_media_type\"}");return;}
  if(!apiRateAllowed()){server.sendHeader("Retry-After","10");sendJson(429,"{\"ok\":false,\"error\":\"rate_limited\"}");return;}
  if(txBlocked()){sendJson(423,"{\"ok\":false,\"error\":\"tx_inhibited\"}");return;}
  uint32_t ric=0;String msg=server.arg("message");if(!msg.length())msg=server.arg("msg");
  if(!parseRicStrict(server.arg("ric"),ric)||!printableAscii(msg,240)){sendJson(400,"{\"ok\":false,\"error\":\"invalid_ric_or_message\"}");return;}
  bool ok=sendPocsag(ric,msg);addDebugLog("API",String(ok?"TX success RIC=":"TX failed RIC=")+String(ric));
  String body=String("{\"ok\":")+(ok?"true":"false")+",\"ric\":"+String(ric)+",\"status\":\""+jsonEscape(lastTxStatus)+"\"}";sendJson(ok?200:500,body);
}

static String historyTimeText(const HistoryEntry &e){return(e.flags&HISTORY_FLAG_TIME_VALID)?formatLocalTime((time_t)e.timestamp):(String("uptime ")+formatUptime(e.uptimeSeconds));}
static void appendHistoryJson(String &j,const HistoryEntry &e,const char *kind){j+="{\"sequence\":"+String(e.sequence)+",\"time\":\""+jsonEscape(historyTimeText(e))+"\",\"ric\":"+String(e.ric)+",\"rssi\":"+String((float)e.rssi10/10.0f,1)+",\"configuredRic\":"+String((e.flags&HISTORY_FLAG_CONFIGURED)?"true":"false")+",\"success\":"+String((e.flags&HISTORY_FLAG_SUCCESS)?"true":"false")+",\"baud\":"+String(e.baud)+",\"txPowerDbm\":"+String(e.txPowerDbm)+",\"type\":"+String(e.type)+",\"function\":"+String(e.function)+",\"speedCode\":"+String(e.speedCode)+",\"source\":\""+jsonEscape(String(e.source))+"\",\"status\":\""+jsonEscape(String(e.status))+"\",\"message\":\""+jsonEscape(displaySafeText(String(e.message)))+"\",\"kind\":\""+String(kind)+"\"}";}
static void handleHistoryApi(String type){const HistoryEntry *a=rxHistory;size_t h=rxHistoryHead,c=rxHistoryCount;const char *kind="rx";if(type=="tx"){a=txHistory;h=txHistoryHead;c=txHistoryCount;kind="tx";}else if(type=="dapnet"){a=dapnetHistory;h=dapnetHistoryHead;c=dapnetHistoryCount;kind="dapnet";}int lim=server.hasArg("limit")?server.arg("limit").toInt():(int)c;if(lim<=0||lim>(int)c)lim=(int)c;String j;j.reserve(1024+lim*180);j="{\"type\":\""+String(kind)+"\",\"messages\":[";for(int i=0;i<lim;++i){const HistoryEntry *e=historyNewest(a,h,c,(size_t)i);if(!e)continue;if(i)j+=',';appendHistoryJson(j,*e,kind);}j+="]}";sendJson(200,j);}

static void handleMessagesApi(){handleHistoryApi("rx");}

static void handleDebugApi() {
  String json;
  json.reserve(4096);
  json = "{\"lines\":[";
  for (size_t i = 0; i < debugLogCount; ++i) {
    size_t idx = (debugLogHead + DEBUG_LOG_SIZE - debugLogCount + i) % DEBUG_LOG_SIZE;
    if (i) json += ',';
    json += "\"" + jsonEscape(debugLogLines[idx]) + "\"";
  }
  json += "]}";
  sendJson(200,json);
}

static void handleStatus() {
  String json;
  json.reserve(2300);
  json = "{";
  json += "\"version\":\"" + String(POCKETDAPNET_VERSION) + "\",";
  json += "\"buildId\":\"" + jsonEscape(String(POCKETDAPNET_BUILD_ID)) + "\",";
  json += "\"wifi\":\"" + jsonEscape(ipInfo()) + "\",";
  json += "\"receiverRunning\":" + String(receiverRunning ? "true" : "false") + ",";
  json += "\"txInhibit\":" + String(cfg.txInhibit ? "true" : "false") + ",";
  json += "\"txBlocked\":" + String(txBlocked() ? "true" : "false") + ",";
  json += "\"defaultWebPassword\":" + String(webUsesDefaultPassword() ? "true" : "false") + ",";
  json += "\"apiEnabled\":" + String(cfg.apiEnabled ? "true" : "false") + ",";
  json += "\"stationIdEnabled\":" + String(cfg.stationIdEnabled ? "true" : "false") + ",";
  json += "\"rxRicCount\":" + String((unsigned)rxAddressCount) + ",";
  json += "\"debugAllRics\":" + String(cfg.debugAllRics ? "true" : "false") + ",";
  json += "\"rxDecodeCount\":" + String(rxDecodeCount) + ",";
  json += "\"rxRestartCount\":" + String(rxRestartCount) + ",";
  json += "\"lastRxState\":" + String(lastRxState) + ",";
  json += "\"historyCount\":" + String((unsigned)rxHistoryCount) + ",";
  json += "\"txHistoryCount\":" + String((unsigned)txHistoryCount) + ",";
  json += "\"dapnetHistoryCount\":" + String((unsigned)dapnetHistoryCount) + ",";
  json += "\"littleFsAvailable\":" + String(littleFsAvailable?"true":"false") + ",";
  json += "\"txPowerDbm\":" + String(cfg.txPowerDbm) + ",";
  json += "\"lastRxRic\":" + String(lastRxRic) + ",";
  json += "\"lastRxRssi\":" + String(lastRxRssi,1) + ",";
  json += "\"lastRxMessage\":\"" + jsonEscape(lastRxMessage) + "\",";
  json += "\"gpsFix\":" + String(gpsHasFix ? "true" : "false") + ",";
  json += "\"gpsLat\":" + String(gpsLat,6) + ",";
  json += "\"gpsLon\":" + String(gpsLon,6) + ",";
  json += "\"gpsAltitudeM\":" + String(gpsAltM,1) + ",";
  json += "\"gpsSatellites\":" + String(gpsSatellites) + ",";
  json += "\"gpsHdop\":" + String(gpsHdop,1) + ",";
  json += "\"localTime\":\"" + jsonEscape(currentLocalTime()) + "\",";
  json += "\"timeSource\":\"" + jsonEscape(lastTimeSource) + "\",";
  json += "\"ntpServer\":\"" + jsonEscape(activeNtpServer.length() ? activeNtpServer : selectedNtpServer()) + "\",";
  json += "\"ntpLastSync\":\"" + jsonEscape(ntpLastSyncEpoch ? formatLocalTime(ntpLastSyncEpoch) : String("--")) + "\",";
  json += "\"rtcPresent\":" + String(rtcPresent ? "true" : "false") + ",";
  json += "\"rtcValid\":" + String(rtcValid ? "true" : "false") + ",";
  json += "\"timeSynchronized\":" + String(systemTimeValid() ? "true" : "false") + ",";
  json += "\"lastTxStatus\":\"" + jsonEscape(lastTxStatus) + "\",";
  json += "\"dapnetEnabled\":" + String(cfg.dapnetEnabled ? "true" : "false") + ",";
  json += "\"dapnetConnected\":" + String(dapnet.isConnected() ? "true" : "false") + ",";
  json += "\"dapnetOnline\":" + String(dapnet.isOnline() ? "true" : "false") + ",";
  json += "\"dapnetStatus\":\"" + jsonEscape(dapnet.status()) + "\",";
  json += "\"dapnetMasterTimeRaw\":\"" + jsonEscape(dapnet.masterTimeRaw()) + "\",";
  json += "\"dapnetClockCorrectionRaw\":\"" + jsonEscape(dapnet.clockCorrectionRaw()) + "\",";
  json += "\"dapnetConfiguredSlots\":\"" + jsonEscape(cfg.dapnetTimeslots) + "\",";
  json += "\"dapnetServerSlots\":\"" + jsonEscape(dapnet.timeslots()) + "\",";
  json += "\"dapnetEffectiveSlots\":\"" + jsonEscape(effectiveDapnetTimeslots()) + "\",";
  json += "\"dapnetCurrentSlot\":" + String(currentDapnetSlot()) + ",";
  { int ds = currentDapnetSlot(); String hx = ds < 0 ? "--" : String(ds, HEX); hx.toUpperCase(); json += "\"dapnetCurrentSlotHex\":\"" + hx + "\","; }
  json += "\"dapnetSlotElapsedMs\":" + String(currentDapnetSlotElapsedMs()) + ",";
  json += "\"dapnetSchedulerReason\":\"" + jsonEscape(dapnetSchedulerReason()) + "\",";
  json += "\"dapnetQueue\":" + String((unsigned)dapnetQueueCount) + ",";
  json += "\"dapnetRxCount\":" + String(dapnet.messagesReceived()) + ",";
  json += "\"dapnetTxCount\":" + String(dapnetTxCount) + ",";
  json += "\"dapnetTxFailCount\":" + String(dapnetTxFailCount) + ",";
  json += "\"dapnetDropCount\":" + String(dapnetDropCount) + ",";
  json += "\"dapnetLastRic\":" + String(dapnetLastRic) + ",";
  json += "\"dapnetLastMessage\":\"" + jsonEscape(dapnetLastMessage) + "\",";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"minFreeHeap\":" + String(ESP.getMinFreeHeap()) + ",";
  json += "\"maxAllocHeap\":" + String(ESP.getMaxAllocHeap()) + ",";
  json += "\"loopLastMs\":" + String(loopLastMs) + ",";
  json += "\"loopMaxMs\":" + String(loopMaxMs) + ",";
  json += "\"loopCounter\":" + String(loopCounter) + ",";
  json += "\"healthStage\":\"" + jsonEscape(String(healthLastStage)) + "\",";
  json += "\"previousHealthStage\":\"" + jsonEscape(bootPreviousStage) + "\",";
  json += "\"resetReason\":" + String((int)esp_reset_reason()) + "}";
  sendJson(200,json);
}

static void initWeb(){
  const char *authHeaders[]={"Authorization","Content-Type"};server.collectHeaders(authHeaders,2);
  server.on("/",HTTP_GET,[](){if(!requireWebAuth())return;handleRoot();});
  server.on("/messages",HTTP_GET,[](){if(!requireWebAuth())return;handleMessages();});
  server.on("/api/messages",HTTP_GET,[](){if(!requireWebAuth())return;handleMessagesApi();});
  server.on("/api/history",HTTP_GET,[](){if(!requireWebAuth())return;String t=server.arg("type");if(t!="tx"&&t!="dapnet")t="rx";handleHistoryApi(t);});
  server.on("/messages/clear",HTTP_POST,[](){if(!requireWebAuth()||!requireCsrf())return;clearAllHistory();redirectTo("/messages");});
  server.on("/settings",HTTP_GET,[](){if(!requireWebAuth())return;handleSettings();});
  server.on("/nvs",HTTP_GET,[](){if(!requireWebAuth())return;handleNvs();});
  server.on("/factory-reset",HTTP_POST,[](){if(!requireWebAuth()||!requireCsrf())return;handleFactoryReset();});
  server.on("/debug",HTTP_GET,[](){if(!requireWebAuth())return;handleDebug();});
  server.on("/api/debug",HTTP_GET,[](){if(!requireWebAuth())return;handleDebugApi();});
  server.on("/debug/clear",HTTP_POST,[](){if(!requireWebAuth()||!requireCsrf())return;clearDebugLog();redirectTo("/debug");});
  server.on("/save",HTTP_POST,[](){if(!requireWebAuth()||!requireCsrf())return;handleSave();});
  server.on("/send",HTTP_POST,[](){if(!requireWebAuth()||!requireCsrf())return;handleSend();});
  server.on("/api/send",HTTP_POST,handleSendApi);
  server.on("/status",HTTP_GET,[](){if(!requireWebAuth())return;handleStatus();});
  server.on("/reboot",HTTP_POST,[](){if(!requireWebAuth()||!requireCsrf())return;sendText(200,"Rebooting");delay(300);ESP.restart();});
  server.onNotFound([](){if(!requireWebAuth())return;prepareResponseHeaders(false);server.sendHeader("Location","/");server.send(302);});
  server.begin();Serial.println("[WEB] authenticated HTTP server started");
}


void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println();
  Serial.printf("PocketDAPNET v%s by DM1PWN\n", POCKETDAPNET_VERSION);
  if (healthMagic == HEALTH_MAGIC) {
    healthLastStage[sizeof(healthLastStage) - 1] = '\0';
    bootPreviousStage = healthLastStage;
  } else {
    bootPreviousStage = "cold boot";
  }
  healthMagic = HEALTH_MAGIC;
  Serial.printf("[HEALTH] reset reason=%d previous stage=%s\n", (int)esp_reset_reason(), bootPreviousStage.c_str());
  setHealthStage("setup");
  initMemoryReservations();
  loadConfig();
  initHistoryStorage();
  ensureWebCredentials();
  csrfToken=randomToken(32);
  cfg.dapnetTimeslots = normalizeDapnetTimeslots(cfg.dapnetTimeslots);
  if (!cfg.dapnetTimeslots.length()) cfg.dapnetTimeslots = DEFAULT_DAPNET_TIMESLOTS;
  initPower();
  initRtc();
  initDisplay();
  initGps();
  initRadio();
  initWifi();
  startNtp();
  dapnet.setMessageHandler(enqueueDapnetMessage);
  dapnet.setLogHandler(dapnetWebLog);
  dapnet.configure(cfg.dapnetHost, cfg.dapnetPort, cfg.dapnetCallsign, cfg.dapnetAuthKey, cfg.dapnetEnabled);
  initWeb();
  initLoopWatchdog();
  lastStationIdMillis = millis();
  addDebugLog("SYSTEM", String("v") + POCKETDAPNET_VERSION + " boot complete; reset=" + String((int)esp_reset_reason()) + " previousStage=" + bootPreviousStage);
  drawDisplay();
  Serial.printf("[WEB] open http://%s/\n", (WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP()).toString().c_str());
}

void loop() {
  const uint32_t loopStarted = millis();
  feedLoopWatchdog();
  setHealthStage("gps");
  processGps();
  setHealthStage("ntp");
  processNtpSync();
  setHealthStage("web");
  server.handleClient();
  setHealthStage("dapnet.socket");
  dapnet.loop(WiFi.status() == WL_CONNECTED);
  setHealthStage("dapnet.queue");
  processDapnetQueue();
  setHealthStage("station.id");
  processStationIdentification();
  setHealthStage("button");
  handleButton();
  if (ledMessageUntil && (int32_t)(millis() - ledMessageUntil) >= 0) {
    ledMessageUntil = 0;
    restoreStatusLed();
  }

  setHealthStage("pocsag.rx");
  if (receiverRunning && pager.available() > 0) {
    // PagerClient direct RX is fed from the SX1278 DIO1 ISR while readData()
    // consumes the same software buffer. After long runtimes this can race
    // with PhysicalLayer::dropSync()/read() and leave readData() spinning
    // until the task watchdog fires. Freeze the direct-RX producer before
    // consuming the already buffered telegram, then fully re-arm RX below.
    setHealthStage("pocsag.detected");
    float capturedRssi = radio.getRSSI();

    setHealthStage("pocsag.freeze-isr");
    radio.clearDio1Action();
    int16_t standbyState = radio.standby();
    receiverRunning = false;
    if (standbyState != RADIOLIB_ERR_NONE) {
      Serial.printf("[POCSAG RX] standby before read failed: %d\n", standbyState);
      addDebugLog("POCSAG", "standby before read failed: " + String(standbyState));
    }

    // RadioLib PagerClient::read() always consumes four bytes per POCSAG
    // codeword. PhysicalLayer::read() does not guard bufferWritePos against
    // underflow, while readData() loops on available() != 0. If Direct RX is
    // stopped with a 1..3-byte tail, readData() can therefore underflow the
    // counter and loop indefinitely. Only hand complete 32-bit codewords to
    // PagerClient. A partial tail is discarded by re-arming the receiver.
    const int16_t rawRxBytes = radio.available();
    if (rawRxBytes < 4 || (rawRxBytes & 0x03) != 0) {
      Serial.printf("[POCSAG RX] incomplete direct buffer: %d bytes; discard/re-arm\n", rawRxBytes);
      addDebugLog("POCSAG", "incomplete direct buffer: " + String(rawRxBytes) + " bytes; discard/re-arm");
      setHealthStage("pocsag.rearm");
      delay(5);
      int16_t restartState = startReceiver();
      rxRestartCount++;
      lastRxState = restartState;
      Serial.printf("[POCSAG RX] re-arm #%lu -> %d\n", (unsigned long)rxRestartCount, restartState);
      if (restartState != RADIOLIB_ERR_NONE) {
        addDebugLog("POCSAG", "RX re-arm failed: " + String(restartState));
      }
      setHealthStage("idle");
      esp_task_wdt_reset();
      return;
    }

    uint8_t message[256] = {0};
    size_t messageLen = sizeof(message) - 1;
    uint32_t address = 0;
    setHealthStage("pocsag.read");
    int16_t state = pager.readData(message, &messageLen, &address);
    lastRxState = state;

    setHealthStage("pocsag.process");
    if (state == RADIOLIB_ERR_NONE) {
      if (messageLen >= sizeof(message)) messageLen = sizeof(message) - 1;
      message[messageLen] = '\0';
      lastRxRic = address;
      lastRxRssi = capturedRssi;
      lastRxMessage = reinterpret_cast<char *>(message);
      rxDecodeCount++;
      addRxHistory(lastRxRic, lastRxRssi, lastRxMessage);
      if (isConfiguredRic(lastRxRic)) signalConfiguredMessage();
      Serial.printf("[POCSAG RX] #%lu RIC=%lu len=%u RSSI=%.1f dBm\n", (unsigned long)rxDecodeCount, (unsigned long)address, (unsigned)messageLen, lastRxRssi);
      Serial.printf("[POCSAG RX] message: %s%s\n", lastRxMessage.c_str(), isConfiguredRic(lastRxRic) ? " [listed RIC]" : "");
      displayPage = 1;
      drawDisplay();
    } else if (state == RADIOLIB_ERR_ADDRESS_NOT_FOUND) {
      // Normal on a shared POCSAG channel: a valid batch was received but none
      // of the configured RICs was present. This is not a decoder failure.
      Serial.println("[POCSAG RX] batch ignored: no configured RIC found");
    } else {
      Serial.printf("[POCSAG RX] decode error: %d\n", state);
      addDebugLog("POCSAG", "decode error: " + String(state));
    }

    // Re-arm direct-mode receive after every completed read. The previous
    // DIO1 action was intentionally detached before readData(), so this call
    // also installs a fresh ISR and clears stale direct-receive state.
    setHealthStage("pocsag.rearm");
    delay(5);
    int16_t restartState = startReceiver();
    rxRestartCount++;
    lastRxState = restartState;
    Serial.printf("[POCSAG RX] re-arm #%lu -> %d\n", (unsigned long)rxRestartCount, restartState);
    if (restartState != RADIOLIB_ERR_NONE) {
      addDebugLog("POCSAG", "RX re-arm failed: " + String(restartState));
    }
  }

  if (displayAvailable && millis() - lastDisplayUpdate > 3000) drawDisplay();
  loopLastMs = millis() - loopStarted;
  if (loopLastMs > loopMaxMs) loopMaxMs = loopLastMs;
  loopCounter++;
  setHealthStage("idle");
  feedLoopWatchdog();
  delay(2);
}
