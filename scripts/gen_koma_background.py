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
    from PIL import Image, ImageChops, ImageDraw, ImageFilter
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
    # Half a border clears the interior on a ruled line, but the wobble below
    # can push a point inward by up to WOBBLE, which would put ink where cover
    # art is drawn. Clearing the wobble as well keeps every interior pristine.
    off = BORDER / 2 + WOBBLE
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


def prepare_flowers(image, threshold, thicken):
    """Make fine line art survive the 1-bit conversion.

    gen_image_header.py cuts at 128, so a grey hairline at 180 does not come out
    faint -- it comes out WHITE, i.e. gone. Anything below `threshold` is forced
    to solid black first, then thickened, because a 1px stroke that survives the
    cut still all but vanishes on a panel with visible pixel structure.
    """
    image = image.point(lambda v: 0 if v < threshold else 255, mode="L")
    for _ in range(thicken):
        # MinFilter grows the dark regions by one pixel per pass.
        image = image.filter(ImageFilter.MinFilter(3))
    return image


def composite_flowers(boxes, flowers_path, fade, threshold, thicken):
    """Lay a flowers-only texture behind the panels.

    The texture must contain no panel borders of its own -- this draws the boxes,
    and anything box-shaped in the source will double up. Ink is attenuated
    inside every panel so decoration never competes with the cover art and text
    drawn over it at runtime; `fade` is how much survives there (0 = none).
    """
    flowers = Image.open(flowers_path).convert("L")
    if flowers.size != (WIDTH, HEIGHT):
        flowers = flowers.resize((WIDTH, HEIGHT), Image.LANCZOS)
    flowers = prepare_flowers(flowers, threshold, thicken)

    px = flowers.load()
    for x, y, w, h in COVERS + [STATS] + MENU:
        for yy in range(max(0, y - BORDER), min(HEIGHT, y + h + BORDER)):
            row = px
            for xx in range(max(0, x - BORDER), min(WIDTH, x + w + BORDER)):
                # Lighten toward white rather than erase, so a motif crossing a
                # panel edge fades out instead of being cut off in a straight line.
                row[xx, yy] = int(255 - (255 - row[xx, yy]) * fade)

    return ImageChops.darker(boxes, flowers)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", help="where to write the PNG (e.g. brand/theme.png)")
    parser.add_argument("--seed", type=int, default=7,
                        help="wobble seed; change for a different hand (default: 7)")
    parser.add_argument("--flowers", metavar="PNG",
                        help="flowers-only texture to lay behind the panels; it must "
                             "contain no boxes of its own")
    parser.add_argument("--flower-fade", type=float, default=0.12, metavar="F",
                        help="fraction of flower ink kept inside a panel, where cover "
                             "art and text are drawn (default: 0.12)")
    parser.add_argument("--flower-threshold", type=int, default=205, metavar="V",
                        help="grey level below which flower ink is forced solid black, "
                             "so hairlines survive the 1-bit cut (default: 205)")
    parser.add_argument("--flower-thicken", type=int, default=1, metavar="N",
                        help="passes of 1px stroke thickening on the flower layer "
                             "(default: 1)")
    args = parser.parse_args()

    image = build(args.seed)
    if args.flowers:
        image = composite_flowers(image, args.flowers, args.flower_fade,
                                  args.flower_threshold, args.flower_thicken)
    image.save(args.output)

    # Report the interiors so a reader can check them against the header.
    print(f"{args.output}: {image.width}x{image.height}, border {BORDER}px")
    print(f"  {len(COVERS)} cover panels, 1 stats panel, {len(MENU)} menu rows")
    if args.flowers:
        print(f"  flowers from {args.flowers}, {args.flower_fade:.0%} kept inside panels")


if __name__ == "__main__":
    main()
