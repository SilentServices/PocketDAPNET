# PocketDAPNET v0.6.7

PocketDAPNET v0.6.7 is the first GitHub-ready release candidate of the ESP32/SX1278 POCSAG transceiver and DAPNET node firmware.

## Highlights

- POCSAG RX/TX with multi-RIC receive filtering and receive-all debug mode.
- DAPNET transmitter client with queueing and timeslot gating.
- TX Inhibit safety interlock; enabled by default on a fresh/factory-reset configuration.
- WiFi client/fallback AP, responsive Web UI, message history and runtime debug log.
- GPS/NTP time synchronization plus optional DS3231 fallback.
- NVS configuration viewer with masked secrets and factory-reset function.
- OLED status pages, LED notifications and configurable RF parameters.

## Important RF safety

Enable **TX Inhibit** before disconnecting or changing the antenna/RF cabling. Do not transmit into an open or unsuitable load.

## Upgrade note

Existing NVS settings are retained across a normal firmware update. Because v0.6.7 introduces the new TX Inhibit key, devices upgrading from an earlier release will initially start with TX Inhibit enabled until the operator explicitly disables it in Settings.
