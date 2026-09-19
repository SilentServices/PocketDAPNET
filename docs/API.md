# PocketDAPNET Send API

PocketDAPNET v0.7.x provides an optional authenticated HTTP endpoint for event-driven POCSAG transmission from other systems.

## Enable

Open **Settings -> Web / API security**, enable **Send API**, and configure a bearer token with 16-128 printable ASCII characters.

## Endpoint

`POST /api/send`

Required headers:

- `Authorization: Bearer <token>`
- `Content-Type: application/x-www-form-urlencoded`

Parameters:

- `ric` - destination RIC as a decimal number in the valid POCSAG range
- `message` - 1-240 printable ASCII characters

Example:

```bash
curl -X POST \
  -H 'Authorization: Bearer YOUR_API_TOKEN' \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  --data-urlencode 'ric=123456' \
  --data-urlencode 'message=PocketDAPNET API test' \
  http://DEVICE_IP/api/send
```

Example response:

```json
{"ok":true,"ric":123456,"status":"RadioLib code 0"}
```

## Status codes

- `200` - message transmitted successfully
- `400` - invalid RIC or message
- `401` - API disabled, missing token, or invalid token
- `415` - unsupported content type
- `423` - RF transmission blocked by TX Inhibit/default credentials
- `429` - API rate limit reached; retry later
- `500` - radio transmission failed

The API is limited to 10 accepted requests in any 10-second window.

## Security

The embedded HTTP server does not provide TLS. Do not expose PocketDAPNET directly to the public Internet. Use it on a trusted LAN/VPN, or place it behind a trusted TLS reverse proxy. TX Inhibit remains authoritative even when a valid API token is supplied.


## Receiving messages from automation systems

`/api/messages` and `/api/history?type=rx` remain available for authenticated polling. For event-driven delivery, PocketDAPNET v0.7.1 also provides an optional outbound RX webhook. See [WEBHOOK.md](WEBHOOK.md).
