# PocketDAPNET

**Open-source POCSAG transceiver and DAPNET node firmware.**  
Current target: **LilyGO T-Beam AXP2101 V1.2 / SX1278 433 MHz**.

Firmware version **v0.7.0 by DM1PWN**.

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
- Responsive authenticated Web UI with dashboard, separate settings, live status, persistent RX/TX/DAPNET history and event debug log.
- OLED status pages and user-button navigation.
- GNSS position/time, selectable NTP servers and optional DS3231 RTC fallback.
- LED notification/status modes.
- Runtime configuration stored in ESP32 NVS; bounded message histories stored in LittleFS.
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

The **NVS / Config** page shows PocketDAPNET's stored configuration values. WiFi passwords, web passwords, API tokens and DAPNET authentication keys are deliberately masked. **Reset to factory defaults** clears the PocketDAPNET NVS namespace and the persistent message-history store, then reboots the device. The live DAPNET transmit queue and debug log remain RAM-only.

## Message history and debug log

PocketDAPNET stores bounded ring histories for the **last 30 received**, **last 30 transmitted**, and **last 30 DAPNET-handled** messages. These histories are persisted in LittleFS and survive reboot. Writes are bounded and update only the current ring slot plus metadata. A factory reset or the **Clear all history** action removes the stored histories.

The event debug log and live DAPNET transmit queue are intentionally RAM-only and are cleared by reboot.

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

Version 0.7.0 keeps the detailed event-log diagnostics for every manual and DAPNET-triggered transmission. The log includes the decimal/hex RIC, POCSAG frame (`RIC & 7`), address field (`RIC >> 3`), baud rate, encoding, function bits, message length, target center frequency, oscillator correction, programmed center, expected low/high FSK tones, shift and configured TX power. This is intended to diagnose decoder interoperability issues without logging ISR/bit-level activity.

DAPNET scheduling also checks that enough time remains in the current 6.4-second slot before starting the next POCSAG page. If insufficient time remains, the queued page waits for the next effective slot.

## Web authentication and API

PocketDAPNET protects the complete web interface with HTTP Basic Authentication. On a fresh installation or after a factory reset, the default login is **`admin` / `pocketdapnet`**, so initial setup does not require a serial console. For safety, RF transmission is forcibly blocked while these public default credentials are still in use. Change the web password in **Settings -> Web / API security** before disabling TX Inhibit. The default login is also shown briefly on the OLED after boot.

The username and password can be changed under **Settings -> Web / API security**. Passwords and API tokens are masked in the NVS viewer.

An optional authenticated send API can be enabled in the same settings section. Configure an API bearer token of at least 16 characters, then send a message using an HTTP POST request:

```bash
curl -X POST \
  -H 'Authorization: Bearer YOUR_API_TOKEN' \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  --data-urlencode 'ric=123456' \
  --data-urlencode 'message=PocketDAPNET API test' \
  http://DEVICE_IP/api/send
```

A successful request returns JSON. **TX Inhibit always has priority**; when it is active, API transmission is rejected even with a valid token.

> HTTP Basic Authentication and bearer tokens do not encrypt traffic. Use PocketDAPNET only on a trusted LAN/VPN, or place it behind a trusted TLS reverse proxy if remote access is required. Do not expose the device web server directly to the public Internet.

## Station identification

PocketDAPNET can provide a local callsign-identification fallback using **RIC 8** and the configured DAPNET callsign. The default interval is 10 minutes and is configurable under **Settings -> Station identification**.

When the DAPNET core supplies and PocketDAPNET successfully transmits a matching RIC-8 identification page, the local timer is reset. This prevents the firmware from unnecessarily sending an additional identification. When DAPNET is online, a short grace period is allowed for the core-provided identification before a local fallback is queued. The fallback uses the normal assigned-timeslot scheduler.

Operators remain responsible for complying with the applicable amateur-radio identification and operating requirements in their jurisdiction.


## Default web login

The first-boot login is `admin` / `pocketdapnet`. RF transmission remains blocked until this default password is changed.


## Runtime watchdog and diagnostics

PocketDAPNET v0.7.0 monitors the main Arduino loop with a 15-second task watchdog. If the application loop stalls while the ESP32 networking stack remains alive, the device automatically reboots. The previous runtime stage, reset reason, heap metrics, and maximum loop latency are exposed in `/status` and the debug log to help locate blocking operations. I2C transactions also use a finite timeout.

## Web security hardening

PocketDAPNET validates configuration and send inputs before use, escapes dynamic HTML/JSON output, protects state-changing web forms with a per-boot CSRF token, and applies no-cache/security response headers including `Cache-Control`, `Pragma`, `X-Content-Type-Options`, `Referrer-Policy`, `X-Frame-Options`, `Permissions-Policy` and a Content Security Policy.

The optional send API requires a bearer token, accepts only `application/x-www-form-urlencoded`, validates RIC/message input, and is rate-limited to 10 accepted requests per 10 seconds. **TX Inhibit always overrides every transmission path.**
