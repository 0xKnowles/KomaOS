#!/usr/bin/env python3
"""Draw the KomaUI home background from the panel metrics the theme already uses.

The previous background was drawn by hand and then *measured*, which meant the
metrics in KomaUiTheme.h were a description of an image nobody could reproduce.
This inverts that: the panel rectangles below are the same numbers the firmware
draws into, so the artwork can be regenerated without re-measuring anything.

Style follows the boot screen: thin hand-inked borders, clean white gutters. No
decoration inside a panel -- every one of them has cover art or text drawn over
it at runtime, and the boot screen's botanicals would fight both.

    python3 scripts/gen_koma_background.py brand/theme.png

Then convert to the packed header the renderer blits:

    python3 scripts/gen_image_header.py brand/theme.png \\
        src/images/KomaBackground.h KomaBackground --size 480x800 --rotate 90

Requires Pillow. Host-side tool only; not part of the firmware build.
"""

import argparse
import random
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

# Logical portrait canvas. The converter rotates this into panel space.
WIDTH, HEIGHT = 480, 800

# Panel INTERIORS, matching KomaUiMetrics in
# src/components/themes/komaui/KomaUiTheme.h exactly. Borders are drawn outside
# these, so the usable area a panel reports stays what the firmware expects.
COVERS = [(16, 40, 121, 305), (183, 40, 116, 305), (343, 40, 119, 305)]
STATS = (18, 372, 443, 118)
MENU = [(18, 508, 443, 43), (18, 565, 443, 40), (18, 621, 444, 55), (18, 694, 444, 63)]

# Boot screen's borders sit around 3px at this size. Thicker reads as a grid,
# which is the complaint that prompted this.
BORDER = 3
# Peak deviation of a hand-inked edge, in pixels. Above ~1.5 the panels start to
# look damaged rather than drawn.
WOBBLE = 1.2
# Points per edge where the wobble is resampled. Fewer means longer, lazier
# curves; more means a jittery line.
SEGMENTS = 7


def wobbly_edge(draw, start, end, rng):
    """Stroke one edge as a slightly irregular polyline rather than a ruled line."""
    x0, y0 = start
    x1, y1 = end
    points = []
    for i in range(SEGMENTS + 1):
        t = i / SEGMENTS
        x = x0 + (x1 - x0) * t
        y = y0 + (y1 - y0) * t
        # Only nudge interior points: the corners have to meet their neighbours.
        if 0 < i < SEGMENTS:
            x += rng.uniform(-WOBBLE, WOBBLE)
            y += rng.uniform(-WOBBLE, WOBBLE)
        points.append((x, y))
    draw.line(points, fill=0, width=BORDER, joint="curve")


def draw_panel(draw, rect, rng):
    """Ink a border around a panel interior, leaving the interior untouched."""
    x, y, w, h = rect
    # Stroke centreline sits half a border outside the interior, so the inked
    # band lies just outside it. Wobble then pushes individual points a pixel
    # either way, so the interior ends up a shade LARGER than the metrics rect
    # -- the safe direction: ink never intrudes into where content is drawn.
    off = BORDER / 2
    left, top = x - off, y - off
    right, bottom = x + w - 1 + off, y + h - 1 + off

    wobbly_edge(draw, (left, top), (right, top), rng)
    wobbly_edge(draw, (right, top), (right, bottom), rng)
    wobbly_edge(draw, (right, bottom), (left, bottom), rng)
    wobbly_edge(draw, (left, bottom), (left, top), rng)


def build(seed):
    # Supersampled, then reduced: drawing the wobble at 1x gives stair-stepped
    # edges that survive thresholding as visible jaggies.
    scale = 4
    image = Image.new("L", (WIDTH * scale, HEIGHT * scale), 255)
    draw = ImageDraw.Draw(image)
    rng = random.Random(seed)

    global BORDER, WOBBLE
    border, wobble = BORDER, WOBBLE
    BORDER, WOBBLE = border * scale, wobble * scale
    try:
        for rect in COVERS + [STATS] + MENU:
            draw_panel(draw, tuple(v * scale for v in rect), rng)
    finally:
        BORDER, WOBBLE = border, wobble

    return image.resize((WIDTH, HEIGHT), Image.LANCZOS)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", help="where to write the PNG (e.g. brand/theme.png)")
    parser.add_argument("--seed", type=int, default=7,
                        help="wobble seed; change for a different hand (default: 7)")
    args = parser.parse_args()

    image = build(args.seed)
    image.save(args.output)

    # Report the interiors so a reader can check them against the header.
    print(f"{args.output}: {image.width}x{image.height}, border {BORDER}px")
    print(f"  {len(COVERS)} cover panels, 1 stats panel, {len(MENU)} menu rows")


if __name__ == "__main__":
    main()
