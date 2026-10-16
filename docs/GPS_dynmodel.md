# GPS: dynamic platform model (u-blox)

The dynamic platform model of the u-blox module (for example *Airborne 2G*) is set by the firmware at every start of the GPS
task and saved to the module.

## Two configuration interfaces
| Module | Interface | Message |
|---|---|---|
| u-blox 8 and older | legacy | `UBX-CFG-NAV5` (0x06 0x24), mask 0x0001, `dynModel` |
| u-blox 9 / 10 | key-value | `UBX-CFG-VALSET` (0x06 0x8A), key `CFG-NAVSPG-DYNMODEL` (0x20110021), layers RAM + BBR + Flash (0x07) |

The firmware sends **both**. M8 modules ignore VALSET, M9/M10 modules apply VALSET.
On a new HGLRC M100 (M10) the legacy message alone left the model at *0 - Portable*; with VALSET it is set.

## Saving
VALSET writes to RAM, BBR (battery-backed RAM) and Flash directly. `UBX-CFG-CFG` (save mask 0xFFFF, devices BBR + Flash)
follows for older modules. The setting is also sent again at every start.

## Checking with u-center
- u-center 1: *UBX - CFG - NAV5*, field *Dynamic Model*.
- u-center 2: *CFG-NAVSPG-DYNMODEL*, shows the stored value per layer.
