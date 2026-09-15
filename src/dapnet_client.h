/*
 * PocketDAPNET - open-source POCSAG transceiver and DAPNET node firmware
 * Copyright (C) 2026 DM1PWN and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <Arduino.h>
#include <WiFiClient.h>

struct DapnetMessage {
  uint8_t type = 0;       // 5 = numeric time sync, 6 = alphanumeric text
  uint8_t speedCode = 1;  // DAPNET: 1 = 1200 baud
  uint32_t ric = 0;
  uint8_t function = 3;
  String text;
};

typedef bool (*DapnetMessageHandler)(const DapnetMessage &message);
typedef void (*DapnetLogHandler)(const String &message);

class DapnetClient {
public:
  void configure(const String &host, uint16_t port, const String &callsign,
                 const String &authKey, bool enabled);
  void setMessageHandler(DapnetMessageHandler handler) { messageHandler = handler; }
  void setLogHandler(DapnetLogHandler handler) { logHandler = handler; }
  void loop(bool networkAvailable);
  void disconnect(const String &reason = "disconnected");

  bool isConnected() { return client.connected(); }
  bool isOnline() const { return online; }
  String status() const { return statusText; }
  String timeslots() const { return slotText; }
  bool slotAllowed(uint8_t slot) const { return slot < 16 && allowedSlots[slot]; }
  uint32_t messagesReceived() const { return receivedCount; }
  uint32_t protocolErrors() const { return errorCount; }
  String lastError() const { return errorText; }
  String masterTimeRaw() const { return masterTimeText; }
  String clockCorrectionRaw() const { return clockCorrectionText; }

private:
  WiFiClient client;
  String host;
  uint16_t port = 43434;
  String callsign;
  String authKey;
  bool enabled = false;
  bool online = false;
  bool loginSeen = false;
  bool allowedSlots[16] = {false};
  String slotText = "--";
  String rxBuffer;
  String statusText = "disabled";
  String errorText;
  String masterTimeText = "--";
  String clockCorrectionText = "--";
  uint32_t receivedCount = 0;
  uint32_t errorCount = 0;
  uint32_t lastConnectAttempt = 0;
  uint32_t reconnectDelayMs = 5000;
  DapnetMessageHandler messageHandler = nullptr;
  DapnetLogHandler logHandler = nullptr;

  void connectNow();
  void processInput();
  void processLine(String line);
  bool sendRaw(const String &text);
  bool parseMessageLine(const String &line, DapnetMessage &message, uint8_t &nextId);
  void parseSchedule(const String &line);
  void fail(const String &message, bool closeSocket = false);
  void logEvent(const String &message);
};
