# PocketDAPNET v0.6.9

Verified RF-calibration release.

## Highlights
- Clarifies the difference between RF center frequency, oscillator correction and POCSAG FSK deviation.
- Keeps TX oscillator correction at a neutral `0.000000 MHz` default.
- TX diagnostics now show the programmed center and expected low/high FSK tones.
- Documents a measurement-based calibration procedure using the midpoint of both tones.
- Retains the v0.6.8 DAPNET slot-time budget check and detailed TX diagnostics.

## Verified behavior
- Manual POCSAG messages were decoded successfully at 1200 baud for multiple RIC ranges when the RF center was correctly calibrated.
- DAPNET-originated queued messages were transmitted in assigned slots and decoded successfully by external POCSAG monitoring tools.

## Important calibration note
The standard POCSAG shift of 4500 Hz means ±4.5 kHz around the center frequency. It must not be entered as a +0.0045 MHz oscillator correction.
