# Frequency calibration

SX1278 modules can exhibit a device-specific oscillator error. PocketDAPNET therefore has separate RX and TX oscillator-correction settings.

**Do not confuse oscillator correction with the POCSAG FSK deviation.**

For a nominal center frequency of `439.9875 MHz` and the standard PocketDAPNET POCSAG shift of `4500 Hz`, the expected two FSK tones are approximately:

```text
Center:     439.987500 MHz
Low tone:   439.983000 MHz
High tone:  439.992000 MHz
Deviation:  ±4.500 kHz
Tone span:  9.000 kHz
```

The **TX oscillator correction** should normally start at `0.000000 MHz`. Only change it when measurement shows that the midpoint between the two tones is offset from the intended center frequency.

## Practical TX calibration

1. Set the desired RF center frequency.
2. Set **TX oscillator correction to `0.000000 MHz`**.
3. Keep the POCSAG FSK shift at `4500 Hz` unless you have a specific reason to change the protocol deviation.
4. Send a sufficiently long manual test message.
5. Measure both FSK tones with suitable equipment.
6. Calculate:

```text
measured_center = (f_low + f_high) / 2
measured_shift  = (f_high - f_low) / 2
correction      = target_center - measured_center
```

7. Enter only the calculated oscillator correction.
8. Repeat the measurement to confirm that the midpoint is centered correctly and the tone spacing remains about 9 kHz.

Example:

```text
Target center:    439.987500 MHz
Measured low:     439.982800 MHz
Measured high:    439.991800 MHz
Measured center:  439.987300 MHz
Measured shift:   4.500 kHz
TX correction:   +0.000200 MHz
```

Do **not** enter `+0.004500 MHz` merely because the POCSAG deviation is 4.5 kHz. Doing so shifts the entire modulation center by 4.5 kHz and can prevent normal POCSAG decoders from recognizing the signal.

Suitable instruments include a calibrated SDR (preferably with TCXO/GPSDO reference), frequency counter, spectrum analyzer, communications test set or other sufficiently accurate receiver.

RX correction can be calibrated separately using a known-good POCSAG signal source.

When using an external PA/LNA, calibrate the radio first without the amplifier, then re-check the complete RF chain for output power, center frequency and unwanted emissions.
