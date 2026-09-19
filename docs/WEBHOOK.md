# PocketDAPNET RX Webhook

PocketDAPNET v0.7.1 can deliver received POCSAG messages to Home Assistant, Node-RED or another HTTP endpoint.

## Configuration

Open **Settings -> Integrations / RX webhook** and enable **RX webhook notifications**. Enter the destination URL.

Only pages matching the configured RIC subscription list are forwarded. Messages seen only because **Debug RX: receive all RICs** is enabled are not forwarded.

The webhook URL is treated as a secret: it is not rendered back into the settings page and is masked in the NVS viewer.

## Home Assistant

For a complete step-by-step setup, filtering examples and a test command, see [HOME_ASSISTANT.md](HOME_ASSISTANT.md).


Create a Home Assistant automation with a webhook trigger and use its local URL, for example:

```text
http://homeassistant.local:8123/api/webhook/YOUR_WEBHOOK_ID
```

Example automation:

```yaml
automation:
  - alias: PocketDAPNET RX
    triggers:
      - trigger: webhook
        webhook_id: YOUR_WEBHOOK_ID
        allowed_methods:
          - POST
        local_only: true
    actions:
      - action: persistent_notification.create
        data:
          title: "POCSAG RIC {{ trigger.json.ric }}"
          message: "{{ trigger.json.message }}"
```

## Payload

```json
{
  "event": "pocsag_rx",
  "device": "PocketDAPNET",
  "version": "0.7.1",
  "buildId": "20260919-01",
  "sequence": 42,
  "timestamp": "2026-09-19 13:20:12",
  "ric": 123456,
  "rssi": -88.5,
  "message": "TEST MESSAGE"
}
```

POCSAG control/non-ASCII bytes are converted to a safe printable representation before JSON encoding.

## Delivery model

Webhook delivery uses a dedicated FreeRTOS worker and an 8-entry RAM queue. This prevents a slow HTTP endpoint from blocking the main PocketDAPNET loop.

Delivery is best-effort. Failed events are counted but are not retried automatically, avoiding duplicate pager events and unbounded queues. `/status` exposes:

- `webhookEnabled`
- `webhookConfigured`
- `webhookQueue`
- `webhookDelivered`
- `webhookFailed`
- `webhookDropped`
- `webhookLastHttpCode`

## Security

The current implementation supports `http://` destinations only. HTTPS is intentionally rejected rather than using insecure TLS certificate bypass. Use a trusted LAN or VPN.

A Home Assistant webhook ID should be treated as a secret. Avoid publishing the URL in screenshots, logs or issue reports.
