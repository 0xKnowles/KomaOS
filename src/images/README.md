# Built-in images

Each `*.h` here is a packed 1-bit bitmap compiled into flash and drawn by
`GfxRenderer::drawImage(array, x, y, width, height)`.

**Format:** row-major, 8 pixels per byte, MSB = leftmost pixel, `1` = white,
`0` = black ink. Rows are byte-aligned; a width that is not a multiple of 8 is
padded with white on the right.

## Regenerating a header from a PNG

```bash
pip install pillow
python3 scripts/gen_image_header.py src/images/Logo120.png src/images/Logo120.h Logo120
```

Pixels are thresholded at luminance 128. A transparent PNG is flattened onto
white first, so an alpha-only glyph (black RGB + alpha mask, which is how
`Logo120.png` is authored) comes out as ink on white rather than a solid black
square.

The tool is host-side only — it is not wired into the PlatformIO build, so the
generated header must be committed alongside the source PNG.

## KomaOS branding status

`Logo120.h` is still the inherited CrossPoint mark and is what the boot screen
currently draws (`src/activities/boot_sleep/BootActivity.cpp`). Note that
upstream's `Logo120.png` and `Logo120.h` had already drifted apart — they are
two different logo revisions — so regenerating the header from the PNG will
change the boot image even before a new logo lands.

To ship the KomaOS logo:

1. Replace `Logo120.png` (120x120) and `logo.svg` with the KomaOS mark.
2. Run the command above.
3. Drop the `TODO(branding)` comment in `BootActivity.cpp`.

Keep it to 120x120: the boot screen centres on that size, and the header costs
1800 bytes of flash at that resolution.
