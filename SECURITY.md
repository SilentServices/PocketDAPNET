# Security notes

PocketDAPNET stores runtime configuration such as WiFi credentials and DAPNET transmitter credentials in ESP32 NVS.

- Do not commit credentials or NVS dumps to the repository.
- The configuration Web UI is intended for trusted/local networks. Review your deployment before exposing it beyond a trusted LAN.
- The fallback access point uses a public default password on a fresh installation; change it during setup.
- Report security-sensitive issues privately to the maintainer rather than publishing active credentials in a public issue.
