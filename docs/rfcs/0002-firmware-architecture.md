# RFC 0002: Firmware Architecture

| Field | Value |
| --- | --- |
| RFC | 0002 |
| Title | Firmware architecture for the Snowboard flight controller |
| Status | Accepted |
| Date | 2026-10-16 |
| Owner | Rotem Carr |
| Applies to | Snowboard Rev 3 hardware (RFC 0001, SNB-HW-001) |
| Zephyr | v4.4.2 |

## 1. Summary

This RFC records the initial firmware architecture: repository and workspace layout, board definition, sensor sampling and control-loop timing, threading, drivers, safety rules, flash layout, persistent data, bootloader policy, and the bring-up firmware. Later changes are recorded in new RFCs; this one is not edited except to correct errors.

## 2. Repository and workspace

### 2.1 Workspace

The project uses a west T2 workspace. The `snowboard-fc` repository is the manifest repository and a Zephyr module. Zephyr and its modules are fetched by `west update` and are not committed.

```
snowboard-workspace/          # not a git repository
├── .west/
├── snowboard-fc/             # this repository
├── zephyr/                   # fetched, pinned by west.yml
├── modules/                  # fetched, pinned by Zephyr's manifest
└── build/
```

`west.yml` pins Zephyr to `v4.4.2` and restricts the imported modules with a `name-allowlist` (`cmsis_6`, `hal_rpi_pico`, `picolibc`). A new machine is set up with `west init -m <repo-url> snowboard-workspace && west update`. Release builds record exact revisions with `west manifest --freeze`.

Zephyr itself is never modified in place. A required patch goes to a fork referenced from `west.yml`, or is kept as a patch file in this repository and applied with `west patch`.

### 2.2 Repository layout

```
snowboard-fc/
├── west.yml
├── zephyr/module.yml          # board_root, dts_root (cmake/kconfig once drivers/ exists)
├── boards/snowboard/          # board definition, shared by all apps
├── dts/bindings/              # custom bindings (DShot, CRSF)
├── drivers/                   # custom drivers (DShot PIO, CRSF)
├── lib/fc/                    # flight logic: pure C, no Zephyr dependencies
├── apps/
│   ├── fc/                    # flight firmware
│   └── bringup/               # self-test firmware
├── hardware/                  # KiCad project, fab outputs, pin map
└── docs/rfcs/
```

The repository is out of tree because it keeps Zephyr upgrades to a one-line manifest change, lets custom drivers evolve without Zephyr's upstreaming requirements, and keeps the Zephyr checkout pristine. The hardware lives in the same repository so that a pin change, the pin map and the devicetree change together in one commit. Tags are prefixed by kind: `hw-rev3`, `fw-v0.1.0`.

### 2.3 Separation of flight logic

`lib/fc/` contains filters, PID controllers, the mixer, the attitude estimator and the arming state machine as plain C. It has no Zephyr includes, so it is unit-tested with ztest on `native_sim` and can be driven by recorded sensor logs.

## 3. Board definition

### 3.1 Target

The board target is `snowboard/rp2350a/m33`. Zephyr 4.5 renames the RP2350 cluster qualifier to `m33_0`; when the manifest moves to 4.5, the board files are renamed to `*_m33_0*` and `Kconfig.snowboard` selects `SOC_RP2350A_M33_0`.

### 3.2 Stages

| Stage | Content | Runs on |
| --- | --- | --- |
| 1 | Copy of the Raspberry Pi Pico 2 board with Pico 2 pin assignments | Pico 2 dev board only |
| 2 | Snowboard pinctrl and devices from `hardware/docs/pinmap.md`; flash partitions; retained RAM | Snowboard |

Stage 1 must not be flashed onto Snowboard: its console UART lands on the receiver connector and its I2C1 claims IMU_INT1.

### 3.3 What goes where

Hardware facts live in the board devicetree: pinctrl, peripheral nodes, the IMU node with `int-gpios`, LEDs, ADC channels, the flash partition table and the retained RAM region. Each app adds `apps/<app>/boards/snowboard_rp2350a_m33.overlay` and `.conf` for its own choices: enabled peripherals, `chosen` console and shell nodes, and app-only nodes. Partitions and retained RAM are board-level because every image on the board must agree on them.

The pin map is generated from the schematic by `hardware/tools/gen_pinmap.py`, which fails when a net no longer matches its expected assignment. A pin change updates the schematic, the generator's expected table, the board DTS and SNB-HW-001 section 7 in one commit.

## 4. Sensing and control loop

### 4.1 Rates

| Item | Value |
| --- | --- |
| Gyro and accelerometer ODR | 8 kHz |
| IMU FIFO watermark | 8 samples |
| Control loop (PID, mixer, motor update) | 1 kHz, one iteration per FIFO watermark interrupt |
| Motor protocol | DShot600 |

The IMU is oversampled relative to the control bandwidth. Every sample passes through the filters at 8 kHz before the stream is decimated to 1 kHz for the PID. This prevents motor vibration above 500 Hz from aliasing into the control band. The IMU's internal anti-alias filter is set to roughly 1–2 kHz. Notch filters are added once logged noise spectra show where they are needed.

Batching costs latency: the oldest sample in a batch is about 1 ms old when used. The loop rate is raised later by lowering the watermark (4 samples at 2 kHz, 2 at 4 kHz).

### 4.2 IMU interrupt

INT1 signals the FIFO watermark. It is configured push-pull, because the board has no pull-up on IMU_INT1. At ODR ≥ 4 kHz the 8 µs interrupt pulse and de-assert-disable setting are required, and `INT_ASYNC_RESET` is cleared, per the ICM-42688-P datasheet. The devicetree `int-gpios` flags must match the polarity the Zephyr driver programs. Samples are timed by the sensor clock, so interrupt latency adds delay but not measurement error. The actual ODR is measured once from FIFO timestamps and used for `dt`.

### 4.3 Calibration

Gyro bias is calibrated at every boot: wait for stillness (low gyro variance, accelerometer magnitude near 1 g), average about one second of samples, subtract before filtering, restart on motion, and block arming until calibration succeeds. Temperature compensation is deferred; bias is logged against IMU temperature, and compensation is added only if the logs show it matters. The IMU temperature sensor is used for this; RP2350 die temperature uses the SoC's own ADC sensor.

## 5. Threads and data flow

| Context | Priority | Work | Rules |
| --- | --- | --- | --- |
| IMU interrupt | ISR | Wake the control thread | Minimal work |
| Control thread | Highest, cooperative | FIFO burst read, filtering, estimator, PID, mixer, DShot write | No zbus, no mutexes, no logging, no flash access |
| Receiver, arming, failsafe | High, preemptible | CRSF parsing, state machine | |
| Housekeeping | Low, preemptible | Battery, LEDs, shell, persistence | zbus allowed |

The control thread publishes its state through a lock-free snapshot (seqlock or double buffer). Slower subsystems communicate over zbus.

## 6. Drivers

| Function | Implementation |
| --- | --- |
| IMU | In-tree ICM-42688 driver, RTIO streaming on the FIFO watermark trigger |
| RGB LED | In-tree WS2812 PIO driver |
| ADC, USB CDC ACM, shell, settings, watchdog, task watchdog | In-tree |
| DShot | Custom PIO driver with its own binding |
| CRSF receiver | Custom driver on the async UART API |
| Aux UART, ESC telemetry | PIO UARTs (hardware UARTs are unavailable on those pins, SNB-HW-001 L-07) |

PIO allocation: PIO0 for DShot, PIO1 for the WS2812, PIO2 for the PIO UARTs.

## 7. Safety

- **Arming** requires: throttle low, RC link up, gyro calibration complete, not in safe mode.
- **Failsafe:** RC link loss for about 200 ms disarms and stops the motors.
- **Motor output:** DShot sends zero throttle continuously while disarmed.
- **Watchdog:** the hardware watchdog resets the board into a state with motors stopped.
- **Flash:** no flash erase or program while armed. Code executes in place from the same QSPI flash, and a sector erase stalls execution for tens to hundreds of milliseconds. The settings layer rejects or defers writes while armed.
- **Crash-loop safe mode:** consecutive fault or watchdog resets are counted in retained RAM. After three in a row, the firmware boots into safe mode: arming refused, USB shell enabled, error log and coredump available for download.

## 8. Flash layout

Flash is the 4 MB W25Q32JVSNIQ. All partitions are aligned to 64 KB.

| Partition | Offset | Size | Content |
| --- | --- | --- | --- |
| `code` | 0x000000 | 1 MB | Application image (future MCUboot slot 0) |
| (reserved) | 0x100000 | 1 MB | Future MCUboot slot 1; unused |
| `storage` | 0x200000 | 64 KB | Settings (NVS): configuration and flight counters |
| `errlog` | 0x210000 | 64 KB | Error records (FCB) |
| `coredump` | 0x220000 | 64 KB | Minimal coredumps (faulting thread only) |
| `blackbox` | 0x230000 | 1856 KB | Flushed blackbox snapshots |

Expected application size is 100–300 KB; `west build -t rom_report` tracks it.

## 9. Persistent data

### 9.1 Configuration and counters

Configuration (PID gains, rates, accelerometer calibration, motor order and direction, channel map) is stored with the settings subsystem on the `storage` partition. Flight counters are kept in RAM while armed and written once per disarm:

| Counter | Type |
| --- | --- |
| Total control-loop cycles | `uint64_t` |
| Cycles in the last armed session | `uint64_t` |
| Missed control-loop deadlines (total and last session) | `uint64_t` |
| Flight count | `uint32_t` |
| Total armed time | `uint64_t` (ms) |

### 9.2 Error log

A custom log backend accepts error-level messages and fatal-error events (Zephyr has no separate critical level). While armed, records go to retained RAM; they are persisted to the `errlog` partition on disarm or at the next boot. Each distinct message is stored once per flight with a count and first/last timestamps, and each record carries the flight number. Dictionary-based logging (`CONFIG_LOG_DICTIONARY_SUPPORT`) keeps records small.

### 9.3 Blackbox

Flight data uses a custom binary format, not the logging subsystem: fixed-size records (about 32 bytes: gyro, setpoints, motor outputs, timestamp), decimated to about 500 Hz. Records go to a RAM ring buffer in the retained region and are flushed to the `blackbox` partition after disarm. The partition is erased in advance while disarmed, so a flush is page programs only.

### 9.4 Retained RAM

A retained SRAM region, declared in the board devicetree and excluded from kernel RAM initialisation, holds the blackbox ring, pending error records and the reset counters, with a magic value and CRC. At boot, valid retained data is flushed to flash before the firmware can arm. The Pico 2 default reserves the first 4 bytes of SRAM for boot-mode retention; the Snowboard region must not overlap it.

| Failure | Outcome |
| --- | --- |
| Firmware fault or watchdog reset | Retained RAM survives; data is flushed at the next boot |
| Crash with impact detection | Impact disarms; the pre-erased flush completes in under about 0.5 s |
| Power loss before flush | In-flight data since the last flush is lost |

Coredumps are written to the `coredump` partition with `CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION` in minimal mode.

## 10. Bootloader

No second-stage bootloader. The RP2350 bootrom's UF2 mode (BOOTSEL) and SWD cover recovery, and the crash-loop safe mode (section 7) prevents a faulty image from flying. The reserved 1 MB keeps MCUboot possible without repartitioning. Adding a bootloader is reconsidered when the board is installed in the frame and updates without BOOTSEL become necessary. At that point, the RP2350 bootrom's native A/B partitions are evaluated alongside MCUboot, and the bootloader's RAM must be placed outside the retained region.

## 11. Bring-up firmware

`apps/bringup` is a separate image so that test-only capabilities never exist in a flight build.

- **Zephyr shells:** sensor, ADC, GPIO, flash, kernel and device shells are enabled first and cover early checks without custom code.
- **`selftest` command:** test IDs mirror SNB-HW-001 section 12 (T-02 to T-09) and print PASS, FAIL or SKIP with measured values. Tests needing a person prompt for confirmation.
- **Order:** clock and crystal lock, flash JEDEC ID, IMU WHO_AM_I, ADC, indicators, UART, then motors.
- **Motor test gating:** requires `--props-off`, caps throttle, stops automatically after a few seconds, and refuses to run if VBAT indicates a battery when only USB power is expected.
- **DShot pattern mode:** continuous fixed frames for checking bit timing on a scope before the ESC is connected.

The same checks later move into on-target ztest suites run by Twister with a hardware map.

## 12. Bring-up sequence

1. Board definition: blink and USB shell.
2. IMU streaming: rates and noise over the shell.
3. CRSF parsing: channels printed.
4. DShot on the bench, props off, from a shell command.
5. Closed loop: rate mode first, angle mode later.

## 13. Open questions

| # | Question |
| --- | --- |
| Q1 | Use of the second Cortex-M33 core: Zephyr SMP support on RP2350, or a dedicated core for the control loop |
| Q2 | NVS or ZMS as the settings backend |
| Q3 | Full-flight blackbox logging: programming flash during flight requires the control path in SRAM |
| Q4 | Size of the retained RAM region |
| Q5 | Zephyr 4.5 migration timing (target qualifier rename) |
| Q6 | Zephyr support for RP2350 MCUboot or bootrom A/B images, when a bootloader is reconsidered |

## 14. Alternatives considered

| Alternative | Decision |
| --- | --- |
| Board and drivers inside the Zephyr tree | Rejected: forks Zephyr and imposes upstream review on fast-changing code |
| Separate hardware and firmware repositories | Rejected for a single board and single developer; revisited if the ESC gets its own firmware |
| Sampling the IMU at the loop rate, or polling it | Rejected: aliasing, and duplicated or skipped samples from clock drift |
| Zephyr logging subsystem for flight data | Rejected: built for events, deferred processing, writes flash on flush |
| Errors stored in the settings partition | Rejected: an error storm could fill or wear the partition holding configuration |
| MCUboot from the start | Deferred: bootrom recovery suffices; adds risk to retained RAM |

## 15. Revision history

| Version | Date | Change |
| --- | --- | --- |
| 1.0 | 2026-10-16 | Initial architecture |
