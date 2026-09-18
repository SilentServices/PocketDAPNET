# PocketDAPNET v0.7.0

This release builds on the verified v0.6.9 radio/DAPNET baseline and adds security, automation and long-runtime stability improvements.

## Highlights

- HTTP Basic Authentication for the complete Web UI.
- First-login credentials `admin` / `pocketdapnet`; RF TX remains blocked until the password is changed.
- Optional authenticated `POST /api/send` bearer-token API with input validation and rate limiting.
- Local RIC-8 station-identification fallback, only while DAPNET operation is enabled.
- Main-loop watchdog diagnostics and retained health-stage information across watchdog resets.
- POCSAG Direct-RX fix that freezes the SX1278 Direct Mode producer before reading and discards incomplete non-32-bit-aligned tails, preventing the observed RadioLib `PagerClient::readData()` watchdog freeze.
- Persistent bounded histories for the last 30 RX, TX and DAPNET-handled messages.
- Centralized HTML/JSON escaping and input validation.
- CSRF protection for state-changing Web UI actions.
- Security/no-cache headers including `Pragma: no-cache`.
- Factory reset now clears both NVS configuration and persistent history.
- Build identifier exposed in the Web UI and `/status`.

## API

See `docs/API.md` for the send API, authentication, validation rules, rate limit and status codes.

## Upgrade notes

Existing NVS configuration is retained during a normal firmware update. Persistent histories begin empty the first time v0.7.0 creates its LittleFS history store. TX Inhibit and the default-password safety interlock remain authoritative.
