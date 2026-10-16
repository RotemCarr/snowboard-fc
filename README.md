# Snowboard

Snowboard is a custom RP2350A flight controller, built to replace the flight controller in a SpeedyBee F405 V4 stack and drive its 4-in-1 ESC. This repository holds everything for it: the KiCad hardware project, the Zephyr board definition, and the firmware.

| | |
| --- | --- |
| Hardware | Rev 3, ordered from JLCPCB (fabrication and full assembly) |
| Firmware | Board definition stage 1 (Pico 2 pin mapping, see below) |
| Zephyr | v4.4.2 |
| Hardware spec | [RFC 0001 / SNB-HW-001](docs/rfcs/0001-flight-controller-hardware.md) |
| Firmware architecture | [RFC 0002](docs/rfcs/0002-firmware-architecture.md) |

## Repository layout

```
snowboard-fc/
├── west.yml                # west manifest: pins Zephyr and its modules
├── zephyr/module.yml       # registers this repo as a Zephyr module
├── boards/snowboard/       # Zephyr board definition (snowboard/rp2350a/m33)
├── app/                    # firmware application (becomes apps/fc, RFC 0002 §2.2)
├── hardware/               # KiCad project, fab outputs, pin map, schematic PDF
└── docs/rfcs/              # design records
```

RFC 0002 adds `apps/bringup/` (self-test firmware), `drivers/` (DShot, CRSF), `dts/bindings/` and `lib/fc/` (flight logic) as they are written.

## Getting started

### Prerequisites

- [Zephyr's host dependencies](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) and the Zephyr SDK
- `west` (`pip install west`)

### Set up a workspace

This repository is the manifest of a west workspace. Zephyr and its modules are fetched next to it and are not part of the repository.

```sh
west init -m <this-repo-url> snowboard-workspace
cd snowboard-workspace
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
```

The result:

```
snowboard-workspace/
├── snowboard-fc/     # this repository
├── zephyr/
├── modules/
└── build/
```

### Build

From the workspace root:

```sh
west build -b snowboard/rp2350a/m33 snowboard-fc/app
```

### Flash

Hold BOOTSEL while connecting USB, so the RP2350 mounts as a drive, then:

```sh
west flash -r uf2
```

With a Raspberry Pi Debug Probe on the SWD connector (J4), use `west flash -r probe-rs` or `west flash -r openocd`; `west debug` works the same way.

> **Stage 1 board definition:** `boards/snowboard/` currently uses the Raspberry Pi Pico 2's pin assignments, so the firmware runs on a Pico 2 dev board. Do not flash it onto Snowboard hardware until stage 2 adds Snowboard's pin map: on Snowboard, the console UART would land on the receiver connector.

## Hardware

Open `hardware/snowboard_fc.kicad_pro` in KiCad 7 or newer. `hardware/README.md` covers the board, its RP2350 core (copied from Raspberry Pi's Minimal design), ordering, and planned changes for the next revision.

| File | Content |
| --- | --- |
| `hardware/docs/schematic-rev3.pdf` | All schematic sheets |
| `hardware/docs/pinmap.md` | RP2350A pin map, generated from the schematic |
| `hardware/fab/rev3/` | Files exactly as ordered from JLCPCB |

## Conventions

- **Pin changes** update the schematic, `EXPECTED` in `hardware/tools/gen_pinmap.py`, the board devicetree and SNB-HW-001 section 7 in one commit. Regenerate the pin map with `python3 tools/gen_pinmap.py` from `hardware/`; it fails if the schematic and the expected assignments disagree.
- **Design decisions** go into a new RFC in `docs/rfcs/`, numbered sequentially. Accepted RFCs are not rewritten; a later RFC supersedes them.
- **Tags** are prefixed by kind: `hw-rev3` for a hardware revision as ordered, `fw-v0.1.0` for a firmware release.
- **Zephyr is never modified in place.** Patches go to a fork referenced from `west.yml`, or into this repository as patch files applied with `west patch`.
- **Flight safety:** firmware never writes flash while armed, and test-only capabilities (such as spinning motors from the shell) exist only in the bring-up app.

## Licence

The RP2350 core block in the hardware design is derived from Raspberry Pi's RP2350A Minimal design under the MIT licence; see `hardware/LICENSE-RaspberryPi-RP2350-Minimal.txt`. No licence has been chosen yet for the rest of this repository.
