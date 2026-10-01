#!/usr/bin/env -S uv run --script
# /// script
# dependencies = ["pillow"]
# ///
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def heat(level):
    stops = [(0, 0, 0), (20, 10, 90), (0, 110, 200), (0, 200, 160), (240, 230, 40), (255, 90, 20)]
    level = max(0.0, min(1.0, level))
    position = level * (len(stops) - 1)
    index = min(int(position), len(stops) - 2)
    fraction = position - index
    return tuple(round(a + (b - a) * fraction) for a, b in zip(stops[index], stops[index + 1]))


def signal(x, row):
    peaks = [(0.28, 0.55), (0.62, 0.9), (0.8, 0.35)]
    value = 0.12
    for center, height in peaks:
        if row % 3 == 0 or center == 0.62:
            value += height * math.exp(-((x - center) ** 2) / 0.002)
    return value


def render(size):
    scale = 8
    big = size * scale
    image = Image.new("RGB", (big, big), (8, 12, 20))
    draw = ImageDraw.Draw(image)
    split = int(big * 0.45)
    rows = 12
    row_height = (big - split) / rows
    for row in range(rows):
        for column in range(big):
            color = heat(signal(column / big, row))
            y0 = split + row * row_height
            draw.line([(column, y0), (column, y0 + row_height)], fill=color)
    points = [(column, split - 4 * scale - signal(column / big, 0) * (split - 8 * scale)) for column in range(0, big, scale)]
    draw.line(points, fill=(255, 224, 102), width=max(scale, big // 24))
    return image.resize((size, size), Image.LANCZOS)


output = Path(sys.argv[1])
output.mkdir(parents=True, exist_ok=True)
for size in (16, 32, 64):
    render(size).save(output / f"icon{size}.png")
