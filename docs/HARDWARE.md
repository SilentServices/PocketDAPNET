# Hardware

## Supported board

The current supported target is the **LilyGO T-Beam AXP2101 V1.2 with SX1278 433 MHz**.

Key connections used by PocketDAPNET:

| Function | ESP32 GPIO |
|---|---:|
| I2C SDA | 21 |
| I2C SCL | 22 |
| SX1278 SCK | 5 |
| SX1278 MISO | 19 |
| SX1278 MOSI | 27 |
| SX1278 NSS/CS | 18 |
| SX1278 RESET | 23 |
| SX1278 DIO0 | 26 |
| SX1278 DIO1 | 33 |
| SX1278 DIO2 | 32 |
| User button | 38 |
| GNSS RX at ESP32 | 34 |
| GNSS TX at ESP32 | 12 |

DIO2 is important for POCSAG receive operation using the SX1278 direct FSK mode.

## Optional DS3231 RTC

A DS3231 can share the existing I2C bus:

| T-Beam | DS3231 |
|---|---|
| GPIO21 | SDA |
| GPIO22 | SCL |
| 3.3 V | VCC |
| GND | GND |

The firmware probes I2C address `0x68` at boot. A valid RTC is used immediately as a fallback time source and is corrected when valid GPS or NTP time becomes available.

## Optional AB-IOT-433 PA/LNA

An AB-IOT-433 PA/LNA may be placed between the T-Beam and antenna for suitable 70-cm applications. Use a separately dimensioned supply, begin with low SX1278 drive power, measure actual output power and spectral quality, and consider a suitable band-pass filter. Observe local amateur-radio regulations.
