#pragma once

#include <array>

#include "components/themes/lyra/LyraTheme.h"
#include "util/XtcProgress.h"

class GfxRenderer;

/**
 * Collection: the home screen as a page of manga panels.
 *
 * A hero cover with two smaller panels stacked beside it and a stats koma
 * closing the block. Panel edges are slanted a few pixels and no two lean the
 * same way, so the block reads as inked panels rather than a table -- which is
 * what the earlier grid versions of this theme kept looking like.
 *
 * The slant lives in the borders. drawBitmap cannot rotate, so cover art is
 * drawn upright across each panel's bounding box and then masked back to its
 * quad; real koma hold upright art inside tilted frames anyway. The masking is
 * per-row, which is why the composed block is cached and only the selection
 * bracket and stats panel are redrawn as the selector moves.
 *
 * The stats koma reports the selected volume, not a library total: the recent
 * list this theme is handed is already capped and collapsed to one entry per
 * series, so a "total" drawn from it would be a count of this block wearing a
 * library's name. Nothing time-based is possible either -- the X4 has no RTC
 * and nothing on the card is timestamped, so streaks and pages-per-day are not
 * computable, however well they would suit the genre.
 *
 * Everything else (lists, keyboard, popups) is inherited from Lyra; this
 * overrides the home block, the menu styling and the header rule.
 */

namespace CollectionMetrics {

/**
 * Panel interiors of src/images/KomaBackground.h, in logical coordinates.
 *
 * Measured off the converted image's pixels, not the artwork that produced it:
 * the generator crops, thresholds and rotates, so the drawing's intent and the
 * bytes on the device are not guaranteed to agree. Re-measure if the background
 * is regenerated -- the static_asserts below catch a panel that has moved off
 * the screen, but not one that has shifted by twenty pixels.
 */
struct Panel {
  int x, y, w, h;
  // constexpr, not just const: the static_asserts below and the metrics lambda
  // both call these in constant expressions, and a member function is not
  // implicitly constexpr.
  constexpr int right() const { return x + w; }
  constexpr int bottom() const { return y + h; }
};

constexpr Panel BANNER{30, 42, 420, 55};
constexpr Panel HERO{27, 105, 219, 229};
constexpr Panel SIDE_TOP{246, 95, 205, 112};
constexpr Panel SIDE_BOTTOM{252, 216, 201, 111};
constexpr Panel STATS{30, 338, 422, 74};

constexpr int MENU_ROWS = 5;
constexpr Panel MENU[MENU_ROWS] = {
    {28, 413, 420, 58}, {28, 472, 424, 66}, {29, 539, 423, 58}, {28, 598, 424, 58}, {28, 658, 424, 59},
};

/** Hero plus the two panels beside it. */
constexpr int PANEL_COUNT = 3;
/** Thumbnails are generated at the hero's height, the largest slot. */
constexpr int COVER_HEIGHT = HERO.h;
/** Inset of text from a panel's inked border. */
constexpr int PAD = 8;

constexpr int PORTRAIT_PANEL_HEIGHT = 800;
static_assert(MENU[MENU_ROWS - 1].bottom() <= PORTRAIT_PANEL_HEIGHT - LyraMetrics::values.buttonHintsHeight,
              "The background's last menu row overlaps the button hints.");
static_assert(STATS.bottom() <= MENU[0].y, "The stats panel overlaps the first menu row.");
static_assert(HERO.right() <= SIDE_TOP.x, "The hero panel overlaps the side column.");

constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeCoverHeight = COVER_HEIGHT;
  // The theme paints the whole page, so it takes the whole page as its rect
  // and HomeActivity is left with nothing to position.
  v.homeTopPadding = 0;
  v.homeCoverTileHeight = MENU[0].y;
  v.homeMenuTopOffset = 0;
  // Kept in step with the drawn rows so selection and touch land where the ink
  // is. The rows are hand-drawn and not evenly pitched; drawButtonMenu places
  // labels at their measured positions rather than deriving them from these.
  v.menuRowHeight = MENU[0].h;
  v.menuSpacing = MENU[1].y - MENU[0].bottom();
  v.homeRecentBooksCount = PANEL_COUNT;
  v.homeGroupRecentsBySeries = true;
  return v;
}();

}  // namespace CollectionMetrics

class CollectionTheme : public LyraTheme {
  // Progress for the four volumes on the shelf, read once when the shelf is
  // composed rather than on every selector move -- the stats box reads whichever
  // volume is selected, and four SD opens per frame to redraw one 44px box is
  // not a trade worth making. Mutable because the draw methods are const, the
  // same reason LyraTheme::coverWidth is.
  mutable std::array<XtcProgress::Snapshot, CollectionMetrics::PANEL_COUNT> shelfProgress{};
  mutable bool shelfProgressLoaded = false;

  void drawStatsKoma(const GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                     int selectorIndex) const;

 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  // Lyra's menu is rows of icon-and-label with a light fill on the selected one
  // and nothing else, which reads as very flat under a shelf of cover art. This
  // delegates to Lyra for the icons and labels -- the icon lookup is file-local
  // to LyraTheme.cpp -- then overlays the styling.
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  // Carries the shelf's ledge motif onto every other screen.
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
};
