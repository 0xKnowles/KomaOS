#!/usr/bin/env python3
"""Convert a PNG into the packed 1-bit C header the renderer draws.

    python3 scripts/gen_image_header.py src/images/Logo120.png src/images/Logo120.h Logo120

Output format (what GfxRenderer::drawImage expects):
  row-major, 8 pixels per byte, MSB = leftmost pixel, 1 = white, 0 = black ink.
Rows are byte-aligned, so a width that is not a multiple of 8 is padded with
white on the right.

Requires Pillow (`pip install pillow`); it is a host-side tool only and is not
part of the firmware build.
"""

import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

# Pixels at or above this luminance become white (bit 1); below become ink.
THRESHOLD = 128


def pack(image):
    width, height = image.size
    row_bytes = (width + 7) // 8
    # Pad bits default to 1 (white) so a non-multiple-of-8 width does not
    # draw a black stripe down the right edge.
    data = bytearray(b"\xff" * (row_bytes * height))
    pixels = image.load()

    for y in range(height):
        for x in range(width):
            if pixels[x, y] < THRESHOLD:
                data[y * row_bytes + (x >> 3)] &= ~(1 << (7 - (x & 7))) & 0xFF

    return data, row_bytes


def emit(name, width, height, data, per_line=19):
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "",
        f"// Image dimensions: {width}x{height}",
        f"static const uint8_t {name}[] = {{",
    ]
    for start in range(0, len(data), per_line):
        chunk = data[start:start + per_line]
        lines.append("    " + " ".join(f"0x{b:02x}," for b in chunk))
    # Strip the trailing comma on the final value.
    lines[-1] = lines[-1].rstrip(",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", help="input PNG")
    parser.add_argument("output", help="output .h path")
    parser.add_argument("name", nargs="?", help="C array name (default: output basename)")
    args = parser.parse_args()

    name = args.name or os.path.splitext(os.path.basename(args.output))[0]

    image = Image.open(args.source)
    # Flatten alpha onto white first: an RGBA logo converted straight to "L"
    # gives transparent pixels a value of 0 and the whole background turns black.
    if image.mode in ("RGBA", "LA", "PA"):
        flattened = Image.new("RGBA", image.size, (255, 255, 255, 255))
        flattened.paste(image, mask=image.convert("RGBA").split()[-1])
        image = flattened
    image = image.convert("L")

    data, row_bytes = pack(image)
    with open(args.output, "w", encoding="utf-8") as fh:
        fh.write(emit(name, image.width, image.height, data))

    print(f"{args.output}: {name}[{len(data)}] ({image.width}x{image.height}, {row_bytes} bytes/row)")


if __name__ == "__main__":
    main()
