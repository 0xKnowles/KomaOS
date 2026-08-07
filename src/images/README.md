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

## Bringing a full-size logo down to 120x120

Artwork drawn at print size needs to survive a ~16x downscale to one bit. The
generator handles the reduction so the header and the preview always agree:

```bash
# See how the artwork reduces before committing to a threshold.
python3 scripts/gen_image_header.py brand/komaos-mark.png \
    --trim --size 120x120 --contact-sheet /tmp/sheet.png

# Emit the header at the chosen threshold, with a preview of both polarities.
python3 scripts/gen_image_header.py brand/komaos-mark.png src/images/Logo120.h Logo120 \
    --trim --size 120x120 --threshold 150 --preview /tmp/preview.png
```

| Flag | Effect |
|---|---|
| `--trim` | crops the uniform white border before resizing, so the mark fills the slot |
| `--size WxH` | LANCZOS resize, aspect preserved, padded with white |
| `--threshold N` | luminance cutoff (default 128) |
| `--dither` | Floyd–Steinberg instead of a hard threshold |
| `--invert` | swap ink and paper |
| `--preview` | upscaled PNG of the packed result, normal **and** inverted |
| `--contact-sheet` | grid of threshold candidates; exits without writing a header unless an output path is also given |

**Check both polarities.** The boot screen draws the mark as ink on white, but
the sleep screen calls `invertScreen()` unless the user picked the light sleep
theme (`SleepActivity.cpp`), so the same 1800 bytes are also displayed white on
black. `--preview` renders both.

**Halftone and screentone do not survive this reduction.** At 120x120 a
screentone fill averages out to one flat tone, so a panel either lands entirely
above the threshold (solid white) or entirely below it (solid black), and two
panels of similar density merge into one shape. Gutters thinner than about 16
source pixels vanish for the same reason. If the full-size mark relies on
texture to separate its parts, author a **simplified 120x120 variant** — flat
fills, gutters wide enough to survive — and keep the detailed artwork for the
README and the web UI, where there are real pixels to spend.

`--dither` preserves average tone but replaces flat areas with speckle, which
partial-refresh e-ink ghosts badly at this size. Prefer a threshold for logos.

## Where the logo appears

| Slot | Size | Source |
|---|---|---|
| Boot screen | 120x120, 1-bit | `Logo120.h` (`BootActivity.cpp`) |
| Sleep screen (default mode) | 120x120, 1-bit, **inverted** | `Logo120.h` (`SleepActivity.cpp`) |
| README / docs | full colour | `docs/images/` |
| Web UI pages | full colour | none yet — `src/network/html/*.html` have no logo or favicon |

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
