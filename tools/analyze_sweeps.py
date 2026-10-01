#!/usr/bin/env -S uv run --script
# /// script
# dependencies = ["numpy"]
# ///
import argparse
import re
import sys
from collections import defaultdict

import numpy as np

METHODS = {0: "retune", 1: "retune+RX", 2: "standby+retune+RX"}


def parse(path):
    sweeps = []
    for line in open(path, encoding="utf-8", errors="replace"):
        if not line.startswith("@S "):
            continue
        fields = dict(re.findall(r"(\w+)=(\S+)", line))
        values = [np.nan if v == "x" else int(v) / 2 for v in fields["data"].split(",")]
        sweeps.append(
            {
                "n": int(fields["n"]),
                "start": int(fields["start"]),
                "step": int(fields["step"]),
                "bins": int(fields["bins"]),
                "method": int(fields["method"]),
                "samples": int(fields["samples"]),
                "ms": int(fields["ms"]),
                "rpc_errors": int(fields["rpc_errors"]),
                "device_errors": fields["device_errors"],
                "dbm": np.array(values),
            }
        )
    return sweeps


def jump_events(rows, threshold_db):
    reference = np.nanmedian(np.vstack([row["dbm"] for row in rows]), axis=0)
    events = []
    for row in rows:
        excess = row["dbm"] - reference
        raised = excess > threshold_db
        if raised.mean() > 0.2:
            indices = np.flatnonzero(raised)
            events.append((row["n"], raised.mean(), indices[0], indices[-1], np.nanmedian(excess[raised])))
    return reference, events


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("--jump-db", type=float, default=10.0)
    args = parser.parse_args()

    sweeps = parse(args.log)
    if not sweeps:
        print("no sweeps")
        return 1
    groups = defaultdict(list)
    for sweep in sweeps:
        groups[(sweep["start"], sweep["step"], sweep["bins"], sweep["method"], sweep["samples"])].append(sweep)

    references = {}
    for key, rows in groups.items():
        start, step, bins, method, samples = key
        ms = np.array([row["ms"] for row in rows])
        reference, events = jump_events(rows, args.jump_db)
        references[key] = reference
        floor = np.nanpercentile(reference, 20)
        print(
            f"{METHODS.get(method, method):18s} samples={samples} start={start/1e6:.3f} step={step/1e3:.1f}k bins={bins} "
            f"sweeps={len(rows)} ms/sweep={ms.mean():.0f}±{ms.std():.0f} ms/step={ms.mean()/bins:.2f} "
            f"floor={floor:.1f} dBm rpc_errors={rows[-1]['rpc_errors']} device_errors={rows[-1]['device_errors']}"
        )
        for n, fraction, first, last, excess in events:
            print(
                f"    jump: sweep {n}: {fraction*100:.0f}% of bins raised by {excess:.1f} dB, "
                f"{(start + first*step)/1e6:.3f}-{(start + last*step)/1e6:.3f} MHz"
            )
        peaks = np.argsort(reference)[::-1][:8]
        print("    strongest bins: " + ", ".join(f"{(start + p*step)/1e6:.3f}:{reference[p]:.1f}" for p in sorted(peaks)))

    keys = list(references)
    for i in range(len(keys)):
        for j in range(i + 1, len(keys)):
            a, b = keys[i], keys[j]
            if a[:3] != b[:3]:
                continue
            difference = references[a] - references[b]
            correlation = np.corrcoef(np.nan_to_num(references[a]), np.nan_to_num(references[b]))[0, 1]
            print(
                f"{METHODS.get(a[3])}/{a[4]} vs {METHODS.get(b[3])}/{b[4]}: mean diff {np.nanmean(difference):+.2f} dB, "
                f"max |diff| {np.nanmax(np.abs(difference)):.1f} dB, correlation {correlation:.3f}"
            )
    return 0


if __name__ == "__main__":
    sys.exit(main())
