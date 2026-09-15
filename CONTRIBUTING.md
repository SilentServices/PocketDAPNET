# Contributing to PocketDAPNET

Contributions, bug reports and hardware test results are welcome.

## Before submitting a change

1. Build the project with PlatformIO using the environment in `platformio.ini`.
2. Do not commit WiFi credentials, DAPNET AuthKeys, personal RICs, assigned timeslots or device-specific frequency-correction values.
3. Keep hardware-specific changes clearly documented.
4. When adding support for another board, describe its radio chip, DIO wiring, display, power-management and time-source capabilities.
5. Preserve third-party copyright and license notices when incorporating upstream code.

## Useful bug-report information

Please include firmware version, board revision, SX127x variant, serial/debug-log output, relevant RF settings and whether the issue occurs in RX, TX, DAPNET, WiFi or time synchronization.

Do not include passwords or DAPNET AuthKeys in public issues.
