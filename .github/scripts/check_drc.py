#!/usr/bin/env python3
"""Fail CI on a KiCad DRC text report, except for known-benign parity items.

Usage: check_drc.py reports/drc.rpt

Rules:
  * Any DRC violation or unconnected item fails the check.
  * Schematic-parity items fail, except:
      - "Pad missing net given by schematic (unconnected-(...))": the pad is a
        no-connect in the schematic, so a net-less pad on the PCB is correct.
        A pad that should be wired to a real net names that net instead and
        still fails.
      - footprint_symbol_mismatch about the BOM attribute: affects the BOM
        only, never copper or the fab files.
"""
import re
import sys

ALLOWED = [
    ("net_conflict", re.compile(r"Pad missing net given by schematic \(unconnected-\(")),
    ("footprint_symbol_mismatch", re.compile(r"Exclude from bill of materials")),
]

ENTRY = re.compile(r"^\[(?P<type>[a-z_]+)\]: (?P<msg>.*)$")
COUNT = re.compile(r"^\*\* Found (?P<n>\d+) (?P<what>DRC violations|unconnected pads)")


def main(path: str) -> int:
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()

    failures, ignored = [], []
    for i, line in enumerate(lines):
        m = COUNT.match(line)
        if m and int(m["n"]) > 0:
            failures.append(f"{m['n']} {m['what']}")
        m = ENTRY.match(line)
        if not m:
            continue
        where = lines[i + 2].strip() if i + 2 < len(lines) else ""
        item = f"[{m['type']}] {m['msg']} {where}"
        if any(t == m["type"] and rx.search(m["msg"]) for t, rx in ALLOWED):
            ignored.append(item)
        else:
            failures.append(item)

    for item in ignored:
        print(f"::notice::ignored (benign): {item}")
    for item in failures:
        print(f"::error::{item}")
    print(f"{len(failures)} failing, {len(ignored)} ignored")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
    