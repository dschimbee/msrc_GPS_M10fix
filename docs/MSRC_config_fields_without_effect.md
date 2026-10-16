# Config fields that have no (or only partial) effect in the firmware

Static code review of `dgatf/msrc` master, commit `08a6c65` (2026-10-15; only `smart_esc.c` changed since `0cc61cf`, so all findings still hold). Not verified on hardware.
Method: for every field of `config_t` (91 fields without `spare*`) I searched for reads in `board/project`, ignoring
`config.c` (defaults) and the Lua transport in `smartport.c` (`packet.value = config->...` / `config_lua->... = value`).
Fields that are only copied into a parameter struct were followed into the driver.

## A. No effect at all

| Field | Evidence |
|---|---|
| `vario_auto_offset` | Passed to the baro drivers (`crsf.c:978`, `:1003`, `:1022` and the same block in the other protocol files), but `bmp280.c`, `bmp180.c`, `ms5611.c` never read `parameter->auto_offset` (only `#include "auto_offset.h"`, line 5). Zero reference is already set by the first valid pressure (P0). The Qt GUI hides the checkbox (`mainwindow.cpp:99`), the web GUI still shows it. |
| `bmp280_filter` | Passed as `bmp280_parameters_t.filter` (`crsf.c:980`), never read in `bmp280.c`. |
| `esc_hw4_current_thresold` | Passed as `esc_hw4_parameters_t.current_thresold` (`crsf.c:541`, `esc_hw4.h:11`), never read in `esc_hw4.c`. |
| `esc_hw4_init_delay_duration` | Only Lua read/write (`smartport.c:304`, `:547`). The delay is a fixed `vTaskDelay(15000 ...)` in `esc_hw4.c:51`; only the boolean `enable_esc_hw4_init_delay` has an effect. |
| `smartport_data_id` | Only Lua write (`smartport.c:538`). The data id is a constant: `parameter->data_id = 0x5100` (`smartport.c:998`). |
| `analog_current_type` | Only Lua read/write (`smartport.c:259`, `:499`). `current_parameters_t` (`sensor/current.h`) has no such member. |
| `analog_current_quiescent_voltage` | Only Lua read/write (`smartport.c:265`, `:505`), same as above. |

## B. Effect only in some configurations

| Field | Effect |
|---|---|
| `alpha_vario` | Not applied by the BMP280, BMP180 and MS5611 drivers (member never read). Applied only by the SPL06 driver (EMA on the pressure), if that driver is merged. |
| `alpha_rpm` | No effect with the ESC types Castle, HW5, PWM (`esc_pwm.c:48`, the call is commented out) and Smart ESC. Works with HW3 (`esc_hw3.c:41`), APD, HW4, Kontronik, OMP M4, OpenYGE, ZTW. |
| `alpha_voltage`, `alpha_current`, `alpha_temperature` | Same ESC types without effect: Castle, HW5, Smart ESC (no `alpha` use in `esc_castle.c`, `esc_hw5.c`, `smart_esc.c`). The analog sensors are not affected. |
| `refresh_rate_*` (rpm, voltage, current, temperature, gps, consumption, vario, airspeed, default) | Read only in `smartport.c` (e.g. `:1009`) and `frsky_d.c` (e.g. `:1020`). Other receiver protocols (Jeti, CRSF, HoTT, ...) ignore them. `refresh_rate_consumption` is read in `smartport.c` and `frsky_d.c` only. |
| `rpm_multiplier` | Recalculated from `pairOfPoles`, `mainTeeth`, `pinionTeeth` when the config is written (`usb.c:47`), so a value entered directly is overwritten. |

## Not a config field, but related
`mpu6050_parameters_t.alpha_gyro`, `ina3221_parameters_t.conversion_time` and `distance_parameters_t.altitude` are parameter members
that the drivers never read. They are not backed by a `config_t` field.

## Suggestion
Hide or label these fields in the Qt GUI and the web GUI, or implement them (e.g. `bmp280_filter` through the BMP280 IIR register).
Our standalone `msrc_web.html` already marks them in the UI with "not supported".
