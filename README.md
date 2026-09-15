# PocketDAPNET

**Open-source POCSAG transceiver and DAPNET node firmware.**  
Current target: **LilyGO T-Beam AXP2101 V1.2 / SX1278 433 MHz**.

Firmware version **v0.6.9 by DM1PWN**.

> PocketDAPNET is an independent amateur-radio project. It is not an official DAPNET or LilyGO product.

## Features

- POCSAG RX and TX at 512, 1200 and 2400 baud using the SX1278.
- Up to 32 exact receive RICs plus optional receive-all debug mode.
- Automatic RX re-arm after decoded pages.
- Classic DAPNET transmitter TCP client with reconnect, authentication, queueing and timeslot gating.
- Configurable locally assigned DAPNET timeslots combined with the server-provided schedule.
- Manual POCSAG transmission from the Web UI.
- Separate RX and TX frequency correction and configurable SX1278 TX power.
- WiFi station mode with automatic fallback access point.
- Responsive Web UI with dashboard, separate settings, live status, message history and event debug log.
- OLED status pages and user-button navigation.
- GNSS position/time, selectable NTP servers and optional DS3231 RTC fallback.
- LED notification/status modes.
- Runtime configuration stored in ESP32 NVS.
- TX Inhibit safety interlock, NVS configuration viewer and factory-reset function.

## Hardware

PocketDAPNET currently supports the **LilyGO T-Beam AXP2101 V1.2 with SX1278 433 MHz**. The SX1278 DIO2 connection is used for POCSAG receive in direct FSK mode.

See [docs/HARDWARE.md](docs/HARDWARE.md) for pin assignments, optional DS3231 wiring and PA/LNA notes.

## Build

Install [PlatformIO](https://platformio.org/) and run:

```bash
pio run
pio run -t upload
pio device monitor
```

The default upload speed is 115200 baud for reliable programming of the T-Beam.

If macOS reports that the serial device is busy, close any active serial monitor before uploading.

## First start and WiFi

On a fresh installation, no personal WiFi, RIC or DAPNET credentials are compiled into the firmware. If no configured station network is available, PocketDAPNET starts a fallback AP:

```text
SSID: PocketDAPNET
Password: dapnet433
Web UI: http://192.168.4.1/
```

Change the AP password during setup if the device will be used outside a trusted environment.

## POCSAG configuration

### TX Inhibit safety interlock

PocketDAPNET normally remains in receive mode and only switches to transmit for a manual page or an eligible DAPNET timeslot. There is no permanent RX/TX mode switch. Instead, **TX Inhibit** blocks all RF transmission while keeping receive and network functions active.

> **RF safety:** Enable **TX Inhibit** before disconnecting, replacing or working on the antenna or RF cabling. Never transmit without a suitable 50-ohm antenna or dummy load connected. An open, unsuitable or badly mismatched load can cause excessive reflected power and may damage the radio output stage or an attached external PA/LNA.

On a fresh installation or after factory reset, TX Inhibit defaults to **enabled** and must be deliberately released after the RF setup has been checked.


Default RF values are intentionally generic:

```text
Base frequency:      439.9875 MHz
RX correction:       0.0000 MHz
TX oscillator corr.: 0.0000 MHz
POCSAG baud:          1200
FSK shift/deviation: 4500 Hz
TX power:             10 dBm
Own/RX RICs:          unconfigured
```

Frequency correction is **device-specific** and is only for compensating a measured oscillator error. It is **not** the POCSAG FSK shift/deviation. For standard PocketDAPNET operation the FSK shift remains 4500 Hz (±4.5 kHz about the RF center). Do not copy the correction value from another board. See [docs/CALIBRATION.md](docs/CALIBRATION.md).

## DAPNET configuration

Under **Settings → DAPNET**, configure the transmitter parameters issued for your node:

- DAPNET client enabled/disabled
- server hostname or IP
- TCP port (classic transmitter protocol default: `43434`)
- transmitter callsign/node name
- transmitter AuthKey
- assigned timeslots

The transmitter credentials are separate from normal DAPNET user credentials.

PocketDAPNET keeps three schedules visible:

```text
Configured slots   locally configured assigned slots
Server slots       schedule received from the DAPNET core
Effective slots    intersection of configured and server slots
```

RF transmission is permitted only in an effective slot and only when a trustworthy system time is available.

The local DAPNET queue contains up to 16 messages and is deliberately **RAM-only**. A reboot clears it so stale pages are not transmitted later.

## Time sources

At boot PocketDAPNET automatically checks for a DS3231 at I2C address `0x68`. If present, its oscillator-stop flag is clear and its timestamp is plausible, the RTC initializes the system clock immediately.

GPS and NTP can subsequently correct the system clock and update the RTC. NTP presets include:

- `pool.ntp.org`
- `de.pool.ntp.org`
- `time.google.com`
- `time.cloudflare.com`
- `time.windows.com`
- custom hostname

The DAPNET clock handshake is exposed for diagnostics, but GPS/NTP/RTC are the trusted system-time sources used by the scheduler.

## Web interface

The hamburger menu provides:

- Dashboard
- Messages
- Settings
- Debug log
- Status JSON
- NVS / Config viewer

The dashboard and message list update automatically without a full page reload. The top bar shows the firmware version, current system time and active time source, and includes a reboot button.

The **NVS / Config** page shows PocketDAPNET's stored configuration values. WiFi passwords and DAPNET authentication keys are deliberately masked. A **Reset to factory defaults** action clears the PocketDAPNET NVS namespace and reboots the device. Runtime queues, received-message history and debug logs are RAM-only and are already cleared by a reboot.

## Message history and debug log

Received-message history and the debug event log are stored in RAM to avoid unnecessary flash wear. Both are cleared on reboot.

The debug log records high-level DAPNET, queue, scheduler, time and RF events rather than bit-level ISR traffic, keeping runtime overhead low.

## Optional PA/LNA

An **AB-IOT-433** or similar suitable 70-cm PA/LNA may be used externally. Begin with a low SX1278 drive level, use an adequately dimensioned external supply, measure actual output power and spectral purity, and consider a suitable band-pass filter.

See [docs/HARDWARE.md](docs/HARDWARE.md) for more detail.

## Frequency calibration

SX1278 boards can have a measurable oscillator-related frequency offset. Check the transmitted signal with suitable measurement equipment such as a calibrated/TCXO-referenced SDR, frequency counter, spectrum analyzer or communications test set. RX and TX correction are configured separately.

See [docs/CALIBRATION.md](docs/CALIBRATION.md).

## Configuration and privacy

The repository intentionally contains no personal/default RICs, assigned DAPNET timeslots, WiFi credentials, DAPNET AuthKeys or device-specific frequency corrections. Runtime values are stored in ESP32 NVS.

Do not publish NVS dumps, screenshots or debug logs containing active credentials.

## Project status

PocketDAPNET is under active development. The current release is intended for technical evaluation and amateur-radio experimentation. DAPNET scheduler behavior should be validated carefully on the operator's actual node before unattended operation.

## Amateur-radio and RF notice

The operator is responsible for complying with applicable amateur-radio rules, frequency allocations, station identification requirements, permitted power levels and spectral-emission limits. Verify RF output with appropriate test equipment, particularly when using an external PA.

## Credits and licenses

PocketDAPNET uses RadioLib, XPowersLib, TinyGPSPlus, U8g2 and the Arduino/ESP32 ecosystem, and was developed with reference to several existing POCSAG and DAPNET open-source projects.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for detailed credits and upstream license information.

PocketDAPNET project source is distributed under the **GNU General Public License v3.0 or later**. See [LICENSE](LICENSE).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Please never include active passwords, AuthKeys, personal RICs or assigned node credentials in public issues or pull requests.


## POCSAG TX diagnostics

Version 0.6.9 keeps the detailed event-log diagnostics for every manual and DAPNET-triggered transmission. The log includes the decimal/hex RIC, POCSAG frame (`RIC & 7`), address field (`RIC >> 3`), baud rate, encoding, function bits, message length, target center frequency, oscillator correction, programmed center, expected low/high FSK tones, shift and configured TX power. This is intended to diagnose decoder interoperability issues without logging ISR/bit-level activity.

DAPNET scheduling also checks that enough time remains in the current 6.4-second slot before starting the next POCSAG page. If insufficient time remains, the queued page waits for the next effective slot.
