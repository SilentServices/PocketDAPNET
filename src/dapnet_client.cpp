/*
 * PocketDAPNET - open-source POCSAG transceiver and DAPNET node firmware
 * Copyright (C) 2026 DM1PWN and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dapnet_client.h"
#include "app_info.h"

void DapnetClient::logEvent(const String &message) {
  Serial.printf("[DAPNET] %s\n", message.c_str());
  if (logHandler) logHandler(message);
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

void DapnetClient::configure(const String &newHost, uint16_t newPort,
                             const String &newCallsign, const String &newAuthKey,
                             bool newEnabled) {
  host = newHost;
  port = newPort ? newPort : 43434;
  callsign = newCallsign;
  callsign.toLowerCase();
  authKey = newAuthKey;
  enabled = newEnabled;
  if (!enabled) {
    disconnect("disabled");
  } else if (!host.length() || !callsign.length() || !authKey.length()) {
    statusText = "configuration incomplete";
  } else {
    statusText = "waiting for network";
  }
}

void DapnetClient::disconnect(const String &reason) {
  if (client.connected()) client.stop();
  online = false;
  loginSeen = false;
  rxBuffer = "";
  statusText = reason;
}

void DapnetClient::fail(const String &message, bool closeSocket) {
  errorText = message;
  errorCount++;
  statusText = message;
  logEvent(message);
  if (closeSocket) {
    client.stop();
    online = false;
    loginSeen = false;
  }
}

bool DapnetClient::sendRaw(const String &text) {
  if (!client.connected()) return false;
  size_t written = client.print(text);
  if (written != text.length()) {
    fail("TCP write failed", true);
    return false;
  }
  return true;
}

void DapnetClient::connectNow() {
  lastConnectAttempt = millis();
  if (!enabled || !host.length() || !callsign.length() || !authKey.length()) return;

  logEvent("connecting to " + host + ":" + String(port) + " ...");
  statusText = "connecting";
  client.stop();
  client.setTimeout(50);
  if (!client.connect(host.c_str(), port)) {
    fail("connection failed", false);
    reconnectDelayMs = reconnectDelayMs * 2;
    if (reconnectDelayMs > 60000UL) reconnectDelayMs = 60000UL;
    return;
  }

  reconnectDelayMs = 5000;
  online = false;
  loginSeen = false;
  rxBuffer = "";
  String login = "[" POCKETDAPNET_USER_AGENT " " + callsign + " " + authKey + "]\r\n";
  if (!sendRaw(login)) return;
  statusText = "login sent";
  logEvent("TCP connected, login sent for " + callsign);
}

void DapnetClient::loop(bool networkAvailable) {
  if (!enabled) return;
  if (!host.length() || !callsign.length() || !authKey.length()) {
    statusText = "configuration incomplete";
    return;
  }
  if (!networkAvailable) {
    if (client.connected()) disconnect("network unavailable");
    else statusText = "waiting for network";
    return;
  }

  if (!client.connected()) {
    online = false;
    if (millis() - lastConnectAttempt >= reconnectDelayMs) connectNow();
    return;
  }
  processInput();
}

void DapnetClient::processInput() {
  while (client.connected() && client.available()) {
    char c = (char)client.read();
    if (c == '\n') {
      String line = rxBuffer;
      rxBuffer = "";
      line.trim();
      if (line.length()) processLine(line);
    } else if (c != '\r') {
      if (rxBuffer.length() < 512) rxBuffer += c;
      else {
        rxBuffer = "";
        fail("input line too long", true);
        return;
      }
    }
  }
}

void DapnetClient::parseSchedule(const String &line) {
  for (int i = 0; i < 16; ++i) allowedSlots[i] = false;
  int colon = line.indexOf(':');
  slotText = colon >= 0 ? line.substring(colon + 1) : "";
  slotText.trim();
  slotText.toUpperCase();
  for (size_t i = 0; i < slotText.length(); ++i) {
    int n = hexNibble(slotText[i]);
    if (n >= 0 && n < 16) allowedSlots[n] = true;
  }
}

bool DapnetClient::parseMessageLine(const String &line, DapnetMessage &message, uint8_t &nextId) {
  if (line.length() < 7 || line[0] != '#') return false;
  int hi = hexNibble(line[1]);
  int lo = hexNibble(line[2]);
  if (hi < 0 || lo < 0) return false;
  uint8_t id = (uint8_t)((hi << 4) | lo);
  nextId = (uint8_t)(id + 1);

  int pos = 3;
  while (pos < line.length() && line[pos] == ' ') pos++;
  int c1 = line.indexOf(':', pos);
  int c2 = c1 >= 0 ? line.indexOf(':', c1 + 1) : -1;
  int c3 = c2 >= 0 ? line.indexOf(':', c2 + 1) : -1;
  int c4 = c3 >= 0 ? line.indexOf(':', c3 + 1) : -1;
  if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0) return false;

  message.type = (uint8_t)line.substring(pos, c1).toInt();
  message.speedCode = (uint8_t)line.substring(c1 + 1, c2).toInt();
  String ricHex = line.substring(c2 + 1, c3);
  message.ric = strtoul(ricHex.c_str(), nullptr, 16);
  message.function = (uint8_t)line.substring(c3 + 1, c4).toInt();
  message.text = line.substring(c4 + 1);

  if ((message.type != 5 && message.type != 6) || message.ric > 0x1FFFFF || message.function > 3) return false;
  return true;
}

void DapnetClient::processLine(String line) {
  if (logHandler) logHandler("RX " + line);
  Serial.printf("[DAPNET RX] %s\n", line.c_str());
  if (line == "+") return;
  if (line == "-") {
    fail("server reported protocol error", false);
    return;
  }

  if (line.startsWith("2:")) {
    masterTimeText = line.substring(2);
    int extraColon = masterTimeText.indexOf(':');
    if (extraColon >= 0) masterTimeText = masterTimeText.substring(0, extraColon);
    masterTimeText.trim();
    if (!loginSeen) {
      loginSeen = true;
      statusText = "authenticated / synchronizing";
      logEvent("credentials accepted, time handshake started");
    }
    sendRaw(line + ":0000\r\n+\r\n");
    return;
  }

  if (line.startsWith("3:")) {
    clockCorrectionText = line.substring(2);
    clockCorrectionText.trim();
    logEvent("server clock correction frame: " + clockCorrectionText + " (diagnostic only; GPS/NTP remains authoritative)");
    sendRaw("+\r\n");
    return;
  }

  if (line.startsWith("4:")) {
    parseSchedule(line);
    online = true;
    statusText = "online";
    sendRaw("+\r\n");
    logEvent("online, assigned timeslots: " + slotText);
    return;
  }

  if (line.startsWith("7 ") || line.startsWith("7:")) {
    String why = line.substring(2);
    why.trim();
    sendRaw("+\r\n");
    fail("login failed: " + why, true);
    reconnectDelayMs = 60000UL;
    return;
  }

  if (line[0] == '#') {
    DapnetMessage msg;
    uint8_t nextId = 0;
    bool parsed = parseMessageLine(line, msg, nextId);
    bool accepted = parsed && messageHandler && messageHandler(msg);
    char ack[12];
    snprintf(ack, sizeof(ack), "#%02X %c\r\n", nextId, accepted ? '+' : '-');
    sendRaw(String(ack));
    if (accepted) {
      receivedCount++;
      logEvent("queued RIC=" + String(msg.ric) + " type=" + String(msg.type) + " func=" + String(msg.function) + " text=" + msg.text);
    } else {
      fail(parsed ? "message queue full" : "malformed message", false);
    }
    return;
  }

  fail("unknown server frame: " + line, false);
  sendRaw("-\r\n");
}
