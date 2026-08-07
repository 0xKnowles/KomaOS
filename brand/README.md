# Brand source artwork

Master logo files live here at full resolution. Everything the firmware or the
docs use is derived from them, so edits start here.

| File | Use |
|---|---|
| `komaos-lockup.png` | full stacked lockup — mark, wordmark, tagline. README and web UI. |
| `komaos-mark.png` | the K mark alone, square, no wordmark. Source for the 120x120 boot/sleep slot. |
| `komaos-mark-120.png` | *(optional)* hand-simplified variant for the 120x120 slot, if the detailed mark does not reduce cleanly. |

Regenerate the firmware header with `scripts/gen_image_header.py` — see
[src/images/README.md](../src/images/README.md) for the flags and for why
screentone fills need a simplified variant at 120x120.

The lockup's wordmark is **not** baked into the boot screen: `BootActivity.cpp`
draws "KomaOS" as live text under the mark via `tr(STR_KOMAOS)`, so the 120x120
image should be the mark only.
