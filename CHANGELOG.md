# Changelog

All notable changes to PocketDAPNET are documented here.

## [0.6.9] - 2026-09-15

### Changed
- Clarified RF terminology: TX correction is now documented as **TX oscillator correction** and is distinct from the POCSAG FSK shift/deviation.
- TX diagnostics now report `programmedCenter`, `toneLow` and `toneHigh` instead of the ambiguous `actual` field.
- Calibration documentation now uses the midpoint of the two FSK tones to determine oscillator correction.

### Verified
- Manual POCSAG TX successfully decoded across multiple low and high RIC ranges with TX oscillator correction set to `0.000000 MHz`.
- DAPNET-originated POCSAG transmissions successfully decoded by OpenWebRX and HackRF-based monitoring.

## [0.6.8] - 2026-09-15

### Added
- Detailed POCSAG TX diagnostics for manual and DAPNET transmissions: RIC decimal/hex, frame, address field, encoding, function bits, baud, RF parameters and message length.
- Conservative DAPNET slot-time budget check before starting each queued RF transmission.

### Changed
- Scheduler debug records now include remaining time in the active 6.4-second DAPNET slot.

## [0.6.7] - 2026-09-15

### Added
- TX Inhibit safety interlock; RX remains active while all manual and DAPNET RF transmissions are blocked.
- NVS / Configuration viewer with masked secrets.
- Factory-reset action that clears the PocketDAPNET NVS namespace and reboots.
- RF-safety documentation for antenna/RF-cable work.

### Changed
- Removed the redundant permanent Receive/Transmit mode selector. PocketDAPNET now normally receives continuously and switches to TX only for an actual transmission.
- Fresh installations and factory-reset devices start with TX Inhibit enabled.

## [0.6.6] - 2026-09-15

### Added
- Project renamed to **PocketDAPNET**.
- POCSAG RX/TX on LilyGO T-Beam AXP2101 V1.2 with SX1278.
- Multi-RIC receive filtering and optional receive-all debug mode.
- DAPNET transmitter client with authentication, queueing, server schedule handling and configurable local timeslots.
- Responsive Web UI with live status, message history, settings, debug log and reboot control.
- WiFi station mode with automatic fallback access point.
- OLED status pages and user-button navigation.
- GPS position/time support, selectable NTP servers and optional DS3231 RTC fallback.
- Separate RX/TX frequency correction and configurable TX power.
- LED notification/status modes.
- Optional external PA/LNA and frequency-calibration documentation.

### Changed
- Installation-specific RICs, DAPNET timeslots, credentials and frequency corrections are no longer repository defaults.
- Firmware identification is centralized in `include/app_info.h`.

### Notes
- DAPNET queue and received-message history are RAM-only and are cleared on reboot.
- Current primary supported target is the LilyGO T-Beam AXP2101 V1.2 / SX1278 433 MHz.
