# Third-party projects and acknowledgements

PocketDAPNET uses libraries and was developed with reference to several open-source projects. Their copyrights and licenses remain with their respective authors.

## Runtime/build dependencies

- **RadioLib** by Jan Gromeš — MIT License — SX1278 support and PagerClient/POCSAG functionality.
  https://github.com/jgromes/RadioLib
- **XPowersLib** by Lewis He — MIT License — AXP2101 power management.
  https://github.com/lewisxhe/XPowersLib
- **TinyGPSPlus** by Mikal Hart — GNU LGPL v2.1 or later — NMEA/GNSS parsing.
  https://github.com/mikalhart/TinyGPSPlus
- **U8g2** by olikraus — see upstream repository for license terms — OLED rendering.
  https://github.com/olikraus/u8g2
- **Arduino-ESP32 / PlatformIO** — see their respective upstream projects and licenses.

## Reference/inspiration projects

The following projects were important technical references while developing PocketDAPNET. They are not vendored into this repository as source trees.

- **ESP32-Pocsag-Pager** by ManoDaSilva — GPL-3.0 — ESP32/SX1278 POCSAG reception and frequency-calibration reference.
  https://github.com/ManoDaSilva/ESP32-Pocsag-Pager
- **RPS (Restaurant Paging Service)** by baycom — GPL-3.0 — ESP32/SX127x POCSAG transmission reference.
  https://github.com/baycom/rps
- **DAPNETGateway** by G4KLX — GPL-2.0 — classic DAPNET transmitter protocol, queueing and timeslot behavior reference.
  https://github.com/g4klx/DAPNETGateway
- **LilyGo-LoRa-Series** by LilyGO/Xinyuan-LilyGO — board definitions, AXP2101 power handling and T-Beam examples.
  https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series

When incorporating code from any upstream project in future changes, preserve the applicable copyright and license notices and verify license compatibility before redistribution.
