#!/usr/bin/env -S uv run --script
# /// script
# dependencies = ["numpy", "pillow"]
# ///
import argparse
import base64
import re
import sys

import numpy as np
from PIL import Image


def last_frame(log_path):
    frame = None
    current = None
    for line in open(log_path, encoding="utf-8", errors="replace"):
        if line.startswith("@F begin"):
            current = {"header": dict(re.findall(r"(\w+)=(\S+)", line)), "chunks": []}
        elif line.startswith("@F end") and current is not None:
            current["expected"] = int(dict(re.findall(r"(\w+)=(\S+)", line))["bytes"])
            frame = current
            current = None
        elif line.startswith("@F ") and current is not None:
            current["chunks"].append(line[3:].strip())
    return frame


def main():
    parser = argparse.ArgumentParser(description="Decode the last kikite framebuffer dump in a link log into a PNG")
    parser.add_argument("log")
    parser.add_argument("output")
    args = parser.parse_args()

    frame = last_frame(args.log)
    if frame is None:
        print("no complete screenshot in log")
        return 1
    encoded = b"".join(base64.b64decode(chunk) for chunk in frame["chunks"])
    if len(encoded) != frame["expected"]:
        print(f"length mismatch: got {len(encoded)} bytes, expected {frame['expected']}")
        return 1

    width = int(frame["header"]["width"])
    height = int(frame["header"]["height"])
    runs = np.frombuffer(encoded, dtype=np.uint8).reshape(-1, 4)
    pixels = np.repeat(runs[:, 1:4], runs[:, 0].astype(np.int64), axis=0)
    if len(pixels) != width * height:
        print(f"pixel count mismatch: {len(pixels)} != {width * height}")
        return 1
    raw = pixels.reshape(height, width, 3)[:, :, ::-1]
    upright = raw[:, ::-1].transpose(1, 0, 2)
    Image.fromarray(np.ascontiguousarray(upright)).save(args.output)
    print(f"saved {args.output} ({upright.shape[1]}x{upright.shape[0]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
