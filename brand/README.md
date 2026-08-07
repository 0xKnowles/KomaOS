# Brand source artwork

Master logo files live here at full resolution. Everything the firmware and the
docs use is derived from them, so edits start here.

| File | Use |
|---|---|
| `komaos-lockup.png` | 2048x2048 stacked lockup — mark, wordmark, progress rule, tagline. Source for the README image and anything web-facing. |
| `komaos-mark.png` | 731x884 K mark alone, cropped from the lockup. Source for the 120x120 boot and sleep slot. |

Derived files, and how to rebuild them:

```bash
# Firmware boot/sleep mark (see src/images/README.md for why threshold 144)
python3 scripts/gen_image_header.py brand/komaos-mark.png --size 120x120 --threshold 144 \
    --preview /tmp/preview.png
# ... then save the previewed bilevel as src/images/Logo120.png and emit the header:
python3 scripts/gen_image_header.py src/images/Logo120.png src/images/Logo120.h Logo120

# README image
python3 -c "from PIL import Image; im=Image.open('brand/komaos-lockup.png').convert('RGB'); \
    im.resize((640,640), Image.LANCZOS).save('docs/images/komaos-lockup.png', optimize=True)"
```

The lockup's wordmark is **not** baked into the boot screen: `BootActivity.cpp`
draws "KomaOS" as live text under the mark via `tr(STR_KOMAOS)`, so the 120x120
image is the mark only. The tagline and progress rule are lockup-only — at 120px
they would be a few pixels tall.
