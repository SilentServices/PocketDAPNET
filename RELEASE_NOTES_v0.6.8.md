# PocketDAPNET v0.6.8

Diagnostic release focused on POCSAG interoperability and DAPNET slot scheduling.

## Highlights
- Logs RIC decimal/hex, POCSAG frame and address field for every TX.
- Logs encoding/function/baud/frequency correction/shift/power/message length.
- Refuses to start a queued DAPNET page when the estimated transmission cannot finish inside the current 6.4-second slot, including a 250 ms guard interval.
- Existing v0.6.7 TX Inhibit, NVS tools, GPS/NTP/RTC and RX/TX functionality remain in place.
