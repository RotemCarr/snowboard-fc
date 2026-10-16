# Snowboard Rev 3: RP2350A flight controller

KiCad 7 project (`snowboard_fc`) for Snowboard, a 40 × 40 mm RP2350A flight controller that replaces the flight controller in a SpeedyBee F405 V4 stack. Open `snowboard_fc.kicad_pro` in KiCad 7 or newer.

The hardware specification is **SNB-HW-001**. Where it and the schematic disagree, the schematic governs.

## Status

| Item | State |
| --- | --- |
| Revision | Rev 3, ordered from JLCPCB (fabrication and full assembly) |
| Schematic | Hierarchical, 7 sheets: top level, Power, MCU, IMU, USB, IO, LEDs |
| PCB | Fully routed. DRC (`fab/rev3/drc_report.txt`, 2026-10-13): 0 violations, 0 unconnected, 0 footprint errors |
| Schematic ↔ PCB | Every pad carries the net the schematic gives it; values and footprints match |
| Pin map | `docs/pinmap.md`, generated from the netlist and checked against SNB-HW-001 section 7 |

## Contents

```
hardware/
├── snowboard_fc.kicad_pro          # open this
├── snowboard_fc.kicad_sch          # root sheet (block diagram)
├── snowboard_fc.kicad_pcb
├── sheets/                         # power, mcu, imu, usb, io, leds
├── sym-lib-table, fp-lib-table     # project libraries via ${KIPRJMOD}/lib
├── lib/
│   ├── snowboard_fc.kicad_sym
│   └── snowboard_fc.pretty/
├── fab/
│   └── rev3/                       # exactly what was sent to JLCPCB, plus its DRC report
├── docs/
│   ├── schematic-rev3.pdf          # all 7 sheets
│   ├── pinmap.md                   # generated, see below
│   └── snowboard_fc_pcb_layers.png
├── tools/
│   ├── gen_pinmap.py               # maintained
│   └── build/                      # archival scripts used to build Rev 3 (hardcoded paths, not runnable as-is)
└── LICENSE-RaspberryPi-RP2350-Minimal.txt
```

Power symbols (`power:GND`, `power:+3V3`, `power:+5V`, `power:PWR_FLAG`) come from KiCad's standard library and are cached in the schematic. 3D models reference KiCad's standard 3D library through `${KISYS3DMOD}`; if the 3D viewer shows parts missing in KiCad 8 or newer, define `KISYS3DMOD` under Preferences → Configure Paths, pointing at your KiCad 3D model directory. Neither affects the fab outputs.

## Board

| Item | Value |
| --- | --- |
| Size | 40 × 40 mm, 3 mm corner radius |
| Mounting | 4 × Ø4.0 mm NPTH on 30.5 × 30.5 mm (M3 grommets); copper keep-out Ø6.8 mm around each, all layers |
| Layers | 4: F.Cu signal + GND pour · In1 solid GND plane · In2 signal + +3V3 pour · B.Cu signal + GND pour |
| Thickness | 1.0 mm |
| Signal rules | 0.127 mm trace / 0.127 mm clearance, vias 0.45 / 0.25 mm |
| Power rules | 0.5 mm traces (BAT, BAT_F, +5V, +5V_BUCK, VBUS, BUCK_SW); vias 0.8 / 0.4 mm |
| Hole-to-copper | 0.2 mm; copper-to-edge 0.3 mm |
| Via-in-pad | None, except the RP2350 exposed-pad vias from Raspberry Pi's footprint |
| Parts | 98; ICs and connectors on top, most 0402 passives on the bottom |

Connectors: J1 ESC 8-pin JST-SH on the rear edge (SpeedyBee F405 V4 order), J2 USB-C on the front edge (silkscreen "FWD"), J3 receiver 4-pin JST-SH on the left edge, J4 SWD 3-pin JST-SH on the right edge, JP1–JP5 1.27 mm solder pads.

## RP2350 core

The RP2350 core block is copied from Raspberry Pi's **RP2350A Minimal design, PCB R4 / schematic S1**, under the MIT licence (`LICENSE-RaspberryPi-RP2350-Minimal.txt`, Copyright (c) 2026 Raspberry Pi Ltd).

- **Copied exactly:** the RP2350A footprint with its 9 exposed-pad vias; the core regulator (AOTA-B201610S3R3-101-T inductor L2 with its polarity dot on +1V1, 4.7 µF on VREG_VIN, the 1.1 V output and VREG_AVDD, 33 Ω AVDD resistor); all 100 nF decoupling including one per DVDD pin; the 27 Ω USB resistors; the crystal group (ABM8-272-T3, 15 pF, 1 kΩ). 114 of 114 pads, 89 of 89 tracks, 22 of 22 vias and 8 of 8 zones match the reference.
- **Fan-out:** every RP2350 pin in use escapes along Raspberry Pi's own traces to the edge of the core block. No foreign copper sits inside the regulator, crystal or under-chip zones.
- **Deliberate differences:** this board is 4-layer 1.0 mm with a solid GND plane on In1 (the reference is 2-layer 1.6 mm), and the core's GND vias land on that plane. The flash sits left of the core because the reference flash position is taken by J2.

## Ordering

Files in `fab/rev3/`: `snowboard_fc_gerbers_jlc.zip`, `snowboard_fc_JLC_BOM_v7.csv`, `snowboard_fc_JLC_CPL_v5.csv`.

- JLC settings: 4 layers, 1.0 mm, JLC04161H-7628 stackup, min track/space 0.127 mm, min hole 0.25 mm, ENIG.
- J2 (GCT USB4085-GF-A) and SW1 (C&K PTS810SJM250SMTRLFS) come through JLC Global Sourcing. L2 and Y1 may need pre-order.
- In the placement preview, check: the RP2350 pin-1 dot; L2's dot toward +1V1; D4's pin-1 mark; J1, J3 and J4 openings facing the board edge; J2 seated over its holes.

## Notes for firmware

- **Pin map:** use `docs/pinmap.md` for the board DTS and pinctrl.
- **Aux UART and ESC telemetry use PIO UARTs.** GPIO12/13 offer only UART0, which the receiver uses on GPIO0/1, and GPIO22 offers only UART1 TX.
- **D4 (XL-1615RGBC-WS2812B):** single-wire GRB at 800 kbit/s, reset low time at least 300 µs. Pinout 1 GND, 2 DIN, 3 VDD, 4 DOUT. It replaced the discontinued WS2812B-2020 in the same position.
- **IMU INT1** has no external pull-up; configure it push-pull.
- **Crystal:** the circuit is tuned for IOVDD = 3.3 V.

## Regenerating the docs

From `hardware/`:

```sh
kicad-cli sch export pdf -o docs/schematic-rev3.pdf snowboard_fc.kicad_sch
python3 tools/gen_pinmap.py
```

`gen_pinmap.py` exports the netlist itself and exits with an error if a GPIO's net no longer matches its `EXPECTED` table. When a pin moves, update the schematic, `EXPECTED`, the board DTS and SNB-HW-001 section 7 together.

## Revision history

| Revision | Summary |
| --- | --- |
| v0.3 | First fully routed board |
| v0.3.1 | Removed three vias-in-pad; moved C13, C27, C28 |
| Rev 3 | RP2350 core rebuilt as an exact copy of Raspberry Pi's Minimal R4 (regulator, crystal, decoupling, USB termination); ABM8-272-T3 crystal; AOTA inductor with polarity fixed; W25Q32JVSNIQ flash; XL-1615RGBC-WS2812B; full JLC assembly including J2; schematic title blocks set to Rev 3; project restructured into `sheets/`, `lib/`, `fab/rev3/` (connectivity and Gerbers unchanged) |

## Next revision — planned changes

Findings from checking v0.3.1 against the design intent (measured on the routed board, not assumed), ordered by how much they matter. Items 1–10 were written against v0.3.1 and are carried over unchanged; re-check each against the Rev 3 layout before acting on it, since the Rev 3 core rebuild moved the RP2350 decoupling, regulator and USB termination. Item 11 was found on Rev 3.

| # | Change | Why | How |
|---|---|---|---|
| 1 | **Move L1 (buck inductor) to sit directly on U3's SW pin; keep the switch node on F.Cu only.** | The switch node (`BUCK_SW`) is the noisiest net on the board. Today it is 13.5 mm long, drops to 0.35–0.45 mm, and changes layers through two 0.25 mm vias. Works at the real load (< 0.5 A) but radiates more than it should and is not sized for the TPS54202's 2 A rating. | Place L1 with its pin 1 pad ≤ 1.5 mm from U3 pin 2; route SW as one 0.6–0.8 mm F.Cu trace, no vias; put the 22 µF output caps and the BOOT cap in the same corner so the switching loop is small. Re-run the autorouter afterwards. |
| 2 | **Move U2 (QSPI flash) against the RP2350.** | Flash is 18 mm from the MCU; QSPI traces are 14–24 mm with 1–2 vias each. Fine at 66–100 MHz, marginal if QSPI is pushed to 133 MHz. | Place U2 within 5 mm of RP2350 pins 55–60 (top-left of the QFN) and route QSPI on F.Cu with no vias, matched within 1 mm. The USB-C sits in that space today, so either shift J2 left along the front edge or switch to a mid-mount SMD USB-C (smaller footprint). |
| 3 | **Pull the four distant RP2350 decoupling caps in.** | C2, C3, C6 and C13 are 2.8–4.8 mm from the nearest supply pin; the other seven are ≤ 1.7 mm. 100 nF decoupling is only effective within ~2 mm of the pin. | After #1 and #2 free up bottom-side space under/around the QFN, hand-place one 100 nF directly under each IOVDD pin (via-in-pad into the pin's own via is fine on the bottom side) before autorouting. |
| 4 | **Route USB D+/D− as a differential pair.** | Currently two single-ended traces, D+ 25.0 mm, D− 33.5 mm (8.5 mm skew), 4 vias, no impedance control. Acceptable for the RP2350's full-speed USB; not good practice. | Hand-route D+/D− side by side on F.Cu from U1 pins 51/52 through the 27 Ω resistors to J2, with the ESD array (D5) on the same layer, length-matched within 1 mm and no vias. Lock before autorouting. |
| 5 | **Thermal reliefs on the outer GND/+3V3 pours (if hand-soldering).** | SMD pads connect solid to the pours in v0.3.1 (chosen to clear DRC "starved thermal" errors). Solid connections make 0402 ground pads heat-sink the iron. Irrelevant for hot-plate reflow. | Set the F.Cu/B.Cu pours to thermal relief for SMD pads (gap 0.25 mm, spoke 0.3 mm), keep In1 solid, re-run DRC and fix any pad that cannot get two spokes. Can be done on v0.3.1 without re-routing. |
| 6 | **Move the IMU to the board centre and add a ground ring under it.** | U5 is 5 mm off-centre (fine for the gyro, slightly worse for the accelerometer under rotation). The SPI bus is 6–12 mm with 2–3 vias per line and no guard; fine at 24 MHz but could be tidier. | Place U5 at (0, 0) first, hand-route SCK/MOSI/MISO/CS on F.Cu over the solid In1 ground, keep other signals out of a ~3 mm ring around it, and keep the ferrite + caps on the VDD side. |
| 7 | **Replace J2 (THT USB4085) with a mid-mount or SMD USB-C.** | The through-hole receptacle blocks both sides of 9 × 9 mm and pushes the flash away (#2). | Mid-mount SMD USB-C 16-pin (e.g. GCT USB4110 or similar), slot in the board edge, footprint from the vendor drawing. |
| 8 | **Keep-out ring silkscreen and a mask-defined annulus at the grommet holes.** | Hole keep-outs are copper-only today; the silkscreen ring is informational. | Add a 7 mm solder-mask ring around each hole so the grommet flange never sits on exposed copper. |
| 9 | **Verify the ICM-42688-P land pattern against DS-000347 §10.2.** | The footprint is the generic LGA-14 2.5 × 3.0 / 0.5 mm pattern (correct pad order per the datasheet pinout figure), not the vendor-drawn one. | Compare pad size/pitch with the datasheet drawing before fab; adjust if different. |
| 10 | **Power-via sizing on BAT_F / +5V.** | A few power nets change layers through single 0.25 mm vias; adequate below ~0.8 A, not for the full 2 A buck rating. | Use 0.8/0.4 mm vias (already defined in the Power netclass) or 2–3 vias in parallel wherever a power net changes layer. |
| 11 | **Move the aux UART pads (JP1) from GPIO12/13 to GPIO20/21, and ESC telemetry to a UART RX pin.** | GPIO12/13 offer only UART0, which the receiver already uses on GPIO0/1, and GPIO22 offers only UART1 TX. Rev 3 works around this with PIO UARTs in firmware (SNB-HW-001 L-07). JP1's value still reads "UART1 pads". | Route JP1 TX/RX to GPIO20/21 (UART1 TX/RX, both unused). Move ESC_TEL to GPIO23 (UART1 RX, alternate function) only if the aux UART goes elsewhere; otherwise keep TEL on a PIO UART. Update `EXPECTED` in `tools/gen_pinmap.py` and the board DTS in the same commit. |

Items 5 and 10 can be applied to v0.3.1 without re-placing anything. Items 1–4, 6 and 7 change placement and need a fresh autoroute, which the `tools/` scripts do end to end.
