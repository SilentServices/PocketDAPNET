# PocketDAPNET v0.7.1

PocketDAPNET v0.7.1 adds event-driven receive integration for Home Assistant and other local automation systems while retaining the stable POCSAG RX/TX, DAPNET, persistent history and web-security behavior introduced in v0.7.0.

## Highlights

- Optional outbound RX webhook integration.
- Only configured RIC subscriptions are forwarded; debug-all-RIC traffic is never pushed externally.
- Dedicated bounded FreeRTOS webhook worker to keep slow HTTP endpoints away from the main RX/web loop.
- Webhook delivery counters and last HTTP result available in `/status`.
- Atomic webhook counters for safe cross-task statistics and warning-free builds.
- PlatformIO pinned to pioarduino 55.03.38-1 with `min_spiffs.csv` for sufficient application space and LittleFS history storage.
- Complete Home Assistant setup guide with webhook automation, RIC filtering, `choose` examples and `curl` testing.

## Home Assistant

See [`docs/HOME_ASSISTANT.md`](docs/HOME_ASSISTANT.md).

PocketDAPNET sends JSON events to a Home Assistant webhook immediately after a subscribed POCSAG page is decoded. Delivery is best-effort and uses a bounded RAM queue so Home Assistant cannot block the radio receive loop.

## Security

- Webhook delivery is disabled by default.
- The webhook URL is treated as a secret and masked in PocketDAPNET configuration views.
- v0.7.1 accepts local `http://` webhook destinations only; it does not bypass TLS certificate verification.
- Use a trusted LAN or VPN and keep Home Assistant webhook IDs private.

## Upgrade notes

Existing v0.7.0 NVS configuration and persistent message histories remain in place during a normal firmware update. The new RX webhook integration is disabled until explicitly configured and enabled.
