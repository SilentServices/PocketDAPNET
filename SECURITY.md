# Security notes

PocketDAPNET stores runtime configuration such as WiFi and DAPNET transmitter credentials in ESP32 NVS. Persistent RX/TX/DAPNET message histories are stored separately in a bounded LittleFS ring store.

- Do not commit credentials, NVS dumps or history dumps containing sensitive traffic to the repository.
- The fallback access point and first web login use documented defaults; change them during setup.
- RF transmission is forcibly blocked while the documented default web credentials remain active.
- The embedded HTTP server does not provide TLS. Use a trusted LAN/VPN or a trusted TLS reverse proxy.

## Web hardening

PocketDAPNET v0.7.0 uses HTTP Basic Authentication for the web UI, per-boot CSRF tokens for state-changing browser forms, centralized input validation, HTML/JSON output escaping, and response headers including:

- `Cache-Control: no-store, no-cache, must-revalidate, max-age=0`
- `Pragma: no-cache`
- `Expires: 0`
- `X-Content-Type-Options: nosniff`
- `X-Frame-Options: DENY`
- `Referrer-Policy: no-referrer`
- `Permissions-Policy` disabling camera, microphone and geolocation
- `Cross-Origin-Resource-Policy: same-origin`
- a Content Security Policy restricting content to the device origin

The optional send API uses a separate bearer token, validates request content and is rate-limited. TX Inhibit is the final RF interlock for Web UI, API, DAPNET and station-identification transmissions.

## Reporting

Report security-sensitive issues privately to the maintainer rather than publishing active credentials or exploit details in a public issue.
