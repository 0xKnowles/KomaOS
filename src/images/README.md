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

**Whether screentone survives depends on the dot pitch, so check, don't assume.**
A fill whose dots are large relative to the mark reduces into real visible
texture; a fine fill averages into one flat tone, and then a panel lands wholly
above the threshold (solid white) or wholly below it (solid black), with
similar-density panels merging into one shape. The KomaOS mark is the first
case — its screentones came through at 120x120 with no simplification needed.
Run `--contact-sheet` on new artwork before assuming either outcome.

Gutters and outlines are the harder constraint: anything thinner than about 1/16
of the mark's width disappears at this size regardless of threshold.

`--dither` preserves average tone but replaces flat areas with speckle, which
partial-refresh e-ink ghosts badly at this size. Prefer a threshold for logos.

## Where the logo appears

| Slot | Size | Source |
|---|---|---|
| Boot screen | 120x120, 1-bit | `Logo120.h` (`BootActivity.cpp`) |
| Sleep screen (default mode) | 120x120, 1-bit, **inverted** | `Logo120.h` (`SleepActivity.cpp`) |
| README / docs | full colour | `docs/images/` |
| Web UI pages | full colour | none yet — `src/network/html/*.html` have no logo or favicon |

## How Logo120 was produced

`Logo120.h` is the KomaOS panel-K mark. It came from the master lockup in
[`brand/`](../../brand/README.md):

```bash
# 1. Crop the mark out of the lockup (the wordmark is drawn as live text, not baked in).
# 2. Reduce and threshold:
python3 scripts/gen_image_header.py brand/komaos-mark.png --size 120x120 --threshold 144 \
    --preview /tmp/preview.png
```

Threshold 144 was chosen off a `--contact-sheet` sweep: it keeps the most tonal
separation between panels while the white gutters and outlines still survive,
and it stays legible when the sleep screen inverts it. Lower (128) flattens the
panels toward white; higher (176+) merges the upper panels into one black mass
and loses the K.

The mark is 731x884, so at 120x120 it is fitted to 99x120 and padded with white
— the array stays 120x120, which is what both call sites pass to `drawImage`.

**`Logo120.png` is the thresholded 1-bit image itself**, not the full-colour
master. That makes the header reproducible with no flags:

```bash
python3 scripts/gen_image_header.py src/images/Logo120.png src/images/Logo120.h Logo120
```

is a no-op that re-emits byte-identical output. Keep it that way — upstream's
copies of these two files had silently drifted into different logo revisions,
and this invariant is what stops that recurring. Edit `brand/` and re-derive,
never hand-edit either file here.

Keep it to 120x120: both call sites centre on that size, and the header costs
1800 bytes of flash at that resolution.
