# SPL06-001 barometer (vario)

Goertek SPL06-001 on I²C, selectable as vario sensor in MSRC Link (`SPL06`). The address (0x77 or 0x76) is detected automatically.

## Measurement settings (fixed in the firmware)
| | Rate | Oversampling |
|---|---|---|
| Pressure | 32 / s | 8x |
| Temperature | 8 / s | 4x |

Temperature uses the external (MEMS) sensor (TMP_EXT = 1), as required for the calibration coefficients. The sensor has
no IIR filter and a maximum rate of 128 / s.

## Filtering
The SPL06 driver has two filter stages. Other vario drivers (BMP180/BMP280/MS5611) are not changed.

1. **Pressure EMA** (`Averaging Elements Vario` in MSRC Link, alpha = 2 / (N + 1)). Value 1 = no smoothing.
   **Recommended: 4** (8 for an even calmer signal). The first 5 readings after start are discarded so the reference
   pressure (P0) starts from a valid value.
2. **Vertical speed as a sliding window.** `Vario vspeed interval` is the length of the window. Every read cycle (40 ms)
   the current altitude is compared with the altitude from at least one interval ago. The value is refreshed at the
   sensor rate instead of once per interval: same noise as a block difference over the same interval, but about
   250-300 ms less delay at the receiver.

Measured with raw logs of one sensor at rest (standard deviation of the vertical speed, interval 500 ms):

| Setting | vspeed noise |
|---|---|
| 8x @ 32/s, Averaging 4 | 0.036 - 0.038 m/s |
| 8x @ 32/s, Averaging 8 | 0.027 m/s |
| 16x @ 16/s, Averaging >= 4 | about the same as 8x |

Averaging single 1x samples in software is **not** equivalent to the hardware oversampling (noise is 3-4 times worse),
which is why the sensor oversamples internally.

## Fields without effect
`vario_auto_offset` has no effect (the reference is the first valid pressure). See
[MSRC_config_fields_without_effect.md](MSRC_config_fields_without_effect.md) for other fields that the firmware ignores.
