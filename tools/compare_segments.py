#!/usr/bin/env -S uv run --script
# /// script
# dependencies = ["numpy"]
# ///
import argparse
import re
import sys
from collections import OrderedDict

import numpy as np


def main():
    parser = argparse.ArgumentParser(description="Compare sweeps between '#segment <name>' markers in a link log")
    parser.add_argument("log")
    parser.add_argument("--watch", type=float, nargs="*", default=[], help="frequencies in MHz to report")
    args = parser.parse_args()

    segments = OrderedDict()
    current = None
    geometry = None
    for line in open(args.log, encoding="utf-8", errors="replace"):
        if line.startswith("#segment "):
            current = line.split(maxsplit=1)[1].strip()
            if current != "end":
                segments.setdefault(current, [])
            continue
        if current in (None, "end") or not line.startswith("@S "):
            continue
        fields = dict(re.findall(r"(\w+)=(\S+)", line))
        geometry = (int(fields["start"]), int(fields["step"]), int(fields["bins"]))
        segments[current].append([np.nan if v == "x" else int(v) / 2 for v in fields["data"].split(",")])

    if not geometry:
        print("no sweeps inside segments")
        return 1
    start, step, bins = geometry
    frequencies = (start + step * np.arange(bins)) / 1e6
    watch = [int(round((mhz * 1e6 - start) / step)) for mhz in args.watch]

    means = {}
    for name, rows in segments.items():
        if not rows:
            print(f"{name}: no sweeps")
            continue
        data = np.array(rows)
        mean = np.nanmean(data, axis=0)
        means[name] = mean
        floor = np.nanpercentile(mean, 20)
        spurs = np.flatnonzero(mean > floor + 8)
        print(f"{name}: {len(rows)} sweeps, floor {floor:.1f} dBm, bins >8 dB above floor: "
              + ", ".join(f"{frequencies[i]:.3f}({mean[i]-floor:+.0f})" for i in spurs))
        if watch:
            print("    watched: " + ", ".join(f"{frequencies[i]:.3f}={mean[i]:.1f}" for i in watch))

    names = list(means)
    for i in range(1, len(names)):
        difference = means[names[i]] - means[names[0]]
        worst = np.argsort(np.abs(difference))[::-1][:6]
        print(f"{names[i]} minus {names[0]}: mean {np.nanmean(difference):+.2f} dB; largest changes "
              + ", ".join(f"{frequencies[j]:.3f}:{difference[j]:+.1f}" for j in sorted(worst)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
