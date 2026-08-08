#pragma once

/**
 * Where text sits on the koma splash (src/images/BootSplash.h).
 *
 * The two panels the artwork leaves empty, measured off the converted image's
 * pixels rather than the design that produced it -- the generator crops and
 * thresholds, so the drawing's intent and the bytes on the device are not
 * guaranteed to agree. Re-measure if the splash is ever regenerated.
 */
namespace SplashLayout {

/**
 * Blit geometry for the splash, which is NOT in logical coordinates.
 *
 * GfxRenderer::drawImage rotates the origin but hands the bitmap through
 * untouched -- it still carries a "TODO: Rotate bits". So the image is stored
 * pre-rotated in panel space (800x480, via gen_image_header.py --rotate 90) and
 * the anchor has to be worked back through the Portrait transform:
 *
 *   phyX = y                        -> y = 0 puts it at the left edge
 *   phyY = (panelHeight - 1 - x) - h -> x = -1, h = 480 puts it at the top
 *
 * Hence the -1: x = 0 would land the image one row above the panel. Passing
 * y = 0 with the logical height of 800 -- the obvious call, and the one that
 * shipped first -- computes phyY = -321, which HalDisplay::drawImage takes as
 * uint16_t and turns into 65215. Nothing clips, nothing errors, and the splash
 * simply never appears.
 *
 * Everything else here is in logical coordinates: drawCenteredText and drawIcon
 * go through drawPixel, which does apply the orientation transform. Only the
 * raw blit is in panel space.
 */
constexpr int BLIT_X = -1;
constexpr int BLIT_Y = 0;
constexpr int BLIT_W = 800;
constexpr int BLIT_H = 480;

constexpr int WIDTH = 480;
constexpr int HEIGHT = 800;

/** Empty panel holding the mark, the wordmark and the status line. */
constexpr int MAIN_X = 23;
constexpr int MAIN_Y = 231;
constexpr int MAIN_W = 433;
constexpr int MAIN_H = 227;

/** Empty strip along the bottom, for the tagline. */
constexpr int STRIP_Y = 658;
constexpr int STRIP_H = 117;

constexpr int MARK_SIZE = 120;
/** Mark sits high in the panel so two text lines clear its bottom edge. */
constexpr int MARK_Y = MAIN_Y + 16;
constexpr int WORDMARK_Y = MARK_Y + MARK_SIZE + 12;
constexpr int STATUS_Y = WORDMARK_Y + 26;
/** Tagline centred in the bottom strip, less the font's own ascent. */
constexpr int TAGLINE_Y = STRIP_Y + STRIP_H / 2 - 8;

static_assert(STATUS_Y + 20 <= MAIN_Y + MAIN_H, "Status line falls outside the splash's empty panel");
static_assert(TAGLINE_Y >= STRIP_Y, "Tagline falls above the splash's bottom strip");

}  // namespace SplashLayout
