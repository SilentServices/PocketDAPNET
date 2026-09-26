# PocketDAPNET v0.7.2-rc1

Release candidate focused on robust multi-batch POCSAG reception.

## Fixed

- Long alphanumeric POCSAG pages are no longer truncated at the first 16-codeword batch.
- SX1278 RSSI sampling during pending pages uses `getRSSI(false, true)`, preventing RSSI reads from stopping Direct-RX after the first batch.
- Continuation batches are collected across POCSAG frame-sync boundaries.
- PocketDAPNET records the exact page-end boundary and trims any bytes received after the terminator before handing the buffer to RadioLib.
- This fixes both observed frame-dependent cases:
  - frame-0 RICs truncating at roughly 42 characters;
  - frame-7 RICs returning only the first two characters.
- Existing protection against the RadioLib Direct-RX watchdog freeze remains in place.

## Diagnostics

Verbose `[POCSAG RAW]` codeword dumps are disabled by default (`POCKETDAPNET_POCSAG_RAW_DEBUG=0`). They can be enabled temporarily in `include/app_info.h` when diagnosing RX edge cases.

## Verification

Verified with a 48-character page spanning multiple batches on both a frame-0 RIC and frame-7 RIC. The complete message was decoded without trailing garbage.

## Build

- Version: `0.7.2-rc1`
- Build ID: `20260926-05`
