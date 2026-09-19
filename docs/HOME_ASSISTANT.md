# Home Assistant integration

PocketDAPNET can push received POCSAG messages to Home Assistant using the optional RX webhook integration. This is preferable to polling because Home Assistant receives the event immediately after PocketDAPNET decodes a page for one of the configured RIC subscriptions.

Only messages matching the normal PocketDAPNET RIC subscription list are forwarded. Traffic seen only because **Debug RX: receive all RICs** is enabled is not sent to Home Assistant.

## 1. Create a Home Assistant webhook automation

In Home Assistant open **Settings -> Automations & scenes -> Create automation -> Create new automation** and add a **Webhook** trigger.

Use a long, random webhook ID and treat it like a secret. For example:

```text
pocketdapnet_rx_7f3a9d2b8c4e4a1f
```

Keep **Local only** enabled when PocketDAPNET and Home Assistant are on the same trusted network.

A complete YAML example is:

```yaml
alias: PocketDAPNET - received POCSAG message
description: Notify when PocketDAPNET receives a subscribed RIC
mode: queued

triggers:
  - trigger: webhook
    webhook_id: pocketdapnet_rx_7f3a9d2b8c4e4a1f
    allowed_methods:
      - POST
    local_only: true

conditions:
  - condition: template
    value_template: "{{ trigger.json.event == 'pocsag_rx' }}"

actions:
  - action: persistent_notification.create
    data:
      title: "POCSAG RIC {{ trigger.json.ric }}"
      message: >-
        {{ trigger.json.message }}

        RSSI: {{ trigger.json.rssi }} dBm
        Time: {{ trigger.json.timestamp }}
```

## 2. Configure PocketDAPNET

Open **Settings -> Integrations / RX webhook** in PocketDAPNET and configure:

```text
Enable RX webhook notifications: ON
Webhook URL: http://HOME_ASSISTANT_IP:8123/api/webhook/pocketdapnet_rx_7f3a9d2b8c4e4a1f
```

For example:

```text
http://192.168.20.10:8123/api/webhook/pocketdapnet_rx_7f3a9d2b8c4e4a1f
```

The webhook URL is stored as a secret and is masked in the PocketDAPNET NVS/configuration viewer.

## 3. Payload

PocketDAPNET sends JSON similar to:

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

Home Assistant exposes these values as `trigger.json.<field>`.

## Filtering by RIC

PocketDAPNET already forwards only subscribed RICs. If Home Assistant should react differently to individual RICs, add a condition or `choose` action.

Single RIC example:

```yaml
conditions:
  - condition: template
    value_template: "{{ trigger.json.ric | int == 123456 }}"
```

Multiple RICs:

```yaml
conditions:
  - condition: template
    value_template: "{{ trigger.json.ric | int in [123456, 234567, 345678] }}"
```

Different actions per RIC:

```yaml
actions:
  - choose:
      - conditions:
          - condition: template
            value_template: "{{ trigger.json.ric | int == 123456 }}"
        sequence:
          - action: persistent_notification.create
            data:
              title: "Personal pager message"
              message: "{{ trigger.json.message }}"

      - conditions:
          - condition: template
            value_template: "{{ trigger.json.ric | int == 234567 }}"
        sequence:
          - action: light.turn_on
            target:
              entity_id: light.hallway
```

## Test Home Assistant before enabling PocketDAPNET

Test the Home Assistant webhook independently with `curl`:

```bash
curl -X POST \
  -H 'Content-Type: application/json' \
  -d '{
    "event":"pocsag_rx",
    "device":"PocketDAPNET",
    "version":"0.7.1",
    "buildId":"test",
    "sequence":1,
    "timestamp":"2026-09-19 13:30:00",
    "ric":123456,
    "rssi":-70.0,
    "message":"TEST WEBHOOK"
  }' \
  http://HOME_ASSISTANT_IP:8123/api/webhook/YOUR_WEBHOOK_ID
```

If the automation fires, enable the RX webhook in PocketDAPNET.

## Diagnostics

PocketDAPNET exposes webhook statistics in `/status`:

- `webhookEnabled`
- `webhookConfigured`
- `webhookQueue`
- `webhookDelivered`
- `webhookFailed`
- `webhookDropped`
- `webhookLastHttpCode`

A successful Home Assistant webhook normally returns HTTP `200`.

Webhook delivery uses a dedicated FreeRTOS worker and an 8-entry RAM queue so a slow Home Assistant endpoint does not block POCSAG receive, the embedded web server or user-button handling.

## Security

PocketDAPNET v0.7.1 accepts `http://` webhook destinations only. It deliberately does not disable TLS certificate validation to make arbitrary HTTPS URLs work.

Use the integration only on a trusted LAN or VPN. Keep the Home Assistant webhook ID secret; possession of the webhook URL is sufficient to trigger the Home Assistant automation. Do not publish it in screenshots, logs or issue reports.
