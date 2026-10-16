#!/usr/bin/env python3
"""Generate docs/pinmap.md from the schematic netlist.

Usage (from the KiCad project directory):
    python3 tools/gen_pinmap.py

Exports the netlist with kicad-cli, maps every RP2350A (U1) pin to its net,
and traces each GPIO through series parts to its far end (connector, pad or
IC). The peripheral column is the firmware's intended pin function, taken
from PERIPHERAL below. The script fails if a GPIO's net no longer matches
EXPECTED_NET, so a schematic change cannot silently desync the firmware DTS.
"""
import re, subprocess, sys, tempfile, pathlib

SCH = "snowboard_fc.kicad_sch"
OUT = pathlib.Path("docs/pinmap.md")
MCU = "U1"

# GPIO -> (expected net, intended peripheral, direction). Keep in sync with
# SNB-HW-001 section 7 and the board DTS.
EXPECTED = {
    0:  ("/RX_TX",      "UART0 TX",        "out"),
    1:  ("/RX_RX",      "UART0 RX",        "in"),
    2:  ("/IMU_SCK",    "SPI0 SCK",        "out"),
    3:  ("/IMU_MOSI",   "SPI0 TX",         "out"),
    4:  ("/IMU_MISO",   "SPI0 RX",         "in"),
    5:  ("/IMU_CS",     "GPIO (SPI0 CS)",  "out"),
    6:  ("/IMU_INT1",   "GPIO interrupt",  "in"),
    8:  ("/M1",         "PIO0 (DShot)",    "out"),
    9:  ("/M2",         "PIO0 (DShot)",    "out"),
    10: ("/M3",         "PIO0 (DShot)",    "out"),
    11: ("/M4",         "PIO0 (DShot)",    "out"),
    12: ("/AUX_TX",     "PIO UART TX",     "out"),
    13: ("/AUX_RX",     "PIO UART RX",     "in"),
    14: ("/I2C_SDA",    "I2C1 SDA",        "bidir"),
    15: ("/I2C_SCL",    "I2C1 SCL",        "out"),
    16: ("/WS_DATA",    "PIO1 (WS2812)",   "out"),
    17: ("/STATUS_LED", "GPIO",            "out"),
    18: ("/BUZZ",       "GPIO",            "out"),
    22: ("/ESC_TEL",    "PIO UART RX",     "in"),
    26: ("/VBAT_SENSE", "ADC0",            "in"),
    27: ("/CUR_SENSE",  "ADC1",            "in"),
    28: ("/RSSI",       "ADC2",            "in"),
}
SERIES = ("R", "L", "FB")      # parts traced through to the far end
RAILS = {"GND", "+3V3", "+5V"}


def export_netlist():
    tmp = pathlib.Path(tempfile.mkdtemp()) / "pinmap.net"
    subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format",
                    "kicadsexpr", "-o", str(tmp), SCH],
                   check=True, capture_output=True)
    return tmp.read_text()


def parse(t):
    values = dict(re.findall(r'\(comp \(ref "([^"]+)"\)\s*\(value "([^"]*)"\)', t))
    nets, net_of, func_of = {}, {}, {}
    for m in re.finditer(r'\(net \(code "?\d+"?\) \(name "([^"]*)"\)(.*?)(?=\(net \(code|\Z)', t, re.S):
        nodes = re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)(?: \(pinfunction "([^"]*)"\))?', m.group(2))
        nets[m.group(1)] = nodes
        for r, p, f in nodes:
            net_of[(r, p)] = m.group(1)
            func_of[(r, p)] = f
    return values, nets, net_of, func_of


def pin_order(label):
    m = re.search(r"\((\d+)\)$", label)
    return int(m.group(1)) if m else 999


def fmt(ref, pin, func, values):
    label = f"{ref}.{pin}" + (f" ({func})" if func else "")
    return f"{label} {values.get(ref, '')}".strip()


def trace(net, frm, values, nets, net_of):
    """Describe where a net goes, following 2-pin series parts once."""
    out = []
    for r, p, f in nets[net]:
        if (r, p) == frm:
            continue
        if r.startswith(SERIES):
            other = "2" if p == "1" else "1"
            far = net_of.get((r, other))
            part = f"{r} {values.get(r, '')}"
            ends = [(a, b, c) for a, b, c in nets[far] if (a, b) != (r, other)]
            if far in RAILS or len(ends) > 4:
                out.append(f"{part} to `{far.lstrip('/')}`")
            else:
                out.append(f"{part} → " + ", ".join(fmt(a, b, c, values) for a, b, c in ends))
        elif r[0] == "C":
            other = "2" if p == "1" else "1"
            out.append(f"{r} {values.get(r, '')} to `{net_of.get((r, other)).lstrip('/')}`")
        elif r == MCU:
            continue
        else:
            out.append(fmt(r, p, f, values))
    return "; ".join(out)


def main():
    values, nets, net_of, func_of = parse(export_netlist())
    pins = {p: (n, func_of[(r, p)]) for (r, p), n in net_of.items() if r == MCU}
    gpio_pin = {}
    for p, (n, f) in pins.items():
        m = re.match(r"GPIO(\d+)", f or "")
        if m:
            gpio_pin[int(m.group(1))] = p

    errors, rows = [], []
    for g in range(30):
        p = gpio_pin[g]
        net, _ = pins[p]
        if net.startswith("unconnected"):
            if g in EXPECTED:
                errors.append(f"GPIO{g}: expected {EXPECTED[g][0]}, now unconnected")
            rows.append(f"| {g} | {p} | — | not connected | — | — |")
            continue
        exp = EXPECTED.get(g)
        if not exp or exp[0] != net:
            errors.append(f"GPIO{g}: netlist says {net}, EXPECTED says {exp[0] if exp else 'nothing'}")
            continue
        rows.append(f"| {g} | {p} | `{net.lstrip('/')}` | {exp[1]} | {exp[2]} | "
                    f"{trace(net, (MCU, p), values, nets, net_of)} |")
    if errors:
        sys.exit("pin map out of date:\n  " + "\n  ".join(errors))

    special = []
    for p in sorted(pins, key=lambda x: (not x.isdigit(), int(x) if x.isdigit() else 0)):
        n, f = pins[p]
        if f and not f.startswith("GPIO") and n not in RAILS and n not in ("GND", "/MCU/DVDD"):
            special.append(f"| {p} | {f.replace('~{CS}', 'CS')} | `{n.lstrip('/')}` | "
                           f"{trace(n, (MCU, p), values, nets, net_of)} |")
    supplies = {}
    for p, (n, f) in pins.items():
        if n in RAILS or n == "/MCU/DVDD":
            supplies.setdefault(n.lstrip("/"), []).append(f"{f} ({p})")

    md = [
        "# Snowboard Rev 3 pin map",
        "",
        "Generated by `tools/gen_pinmap.py` from `snowboard_fc.kicad_sch`. Do not edit by hand:",
        "change the schematic (and `EXPECTED` in the script if a pin moves), then regenerate.",
        "Authoritative for the board DTS and pinctrl. Spec: SNB-HW-001 section 7.",
        "",
        "## GPIO",
        "",
        "| GPIO | Pin | Net | Peripheral | Dir | Connects to |",
        "| --- | --- | --- | --- | --- | --- |",
        *rows,
        "",
        "Notes:",
        "",
        "- GPIO12/13 offer only UART0 (already used by the receiver on GPIO0/1) and GPIO22 offers only",
        "  UART1 TX, so the aux UART and ESC telemetry use PIO UARTs (SNB-HW-001 L-07).",
        "- ADC inputs are limited to GPIO26–29 on the RP2350A.",
        "- RP2350 erratum E9: internal pull-downs are not used on any input.",
        "",
        "## Dedicated pins",
        "",
        "| Pin | Function | Net | Connects to |",
        "| --- | --- | --- | --- |",
        *special,
        "",
        "## Supply pins",
        "",
        "| Net | Pins |",
        "| --- | --- |",
        *[f"| `{n}` | {', '.join(sorted(v, key=pin_order))} |" for n, v in sorted(supplies.items())],
        "",
    ]
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text("\n".join(md))
    print(f"wrote {OUT}: {len(rows)} GPIO rows, {len(special)} dedicated pins")


if __name__ == "__main__":
    main()
