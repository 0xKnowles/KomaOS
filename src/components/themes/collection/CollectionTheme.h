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

// A koma dashboard, not a grid: one hero cover, two smaller panels beside it,
// and a stats strip closing the block. Panel edges are slanted a few pixels so
// the gutters read as inked panel borders rather than a table.
//
// The slant is in the BORDERS only. drawBitmap cannot rotate (GfxRenderer.cpp's
// drawImage still carries the "rotate bits" TODO), so cover art stays upright
// and the panel is masked back to its quad around it -- which is what sells the
// effect anyway, since real koma hold upright art inside tilted frames.

/** Inset of the whole block from the panel edge. */
constexpr int MARGIN = 10;
/** White gutter between komas. */
constexpr int GUTTER = 8;
/** Ink weight of a panel border. */
constexpr int BORDER = 2;
/** Largest per-corner displacement. Beyond ~6 the masking eats visible art. */
constexpr int SLANT = 5;

constexpr int HERO_WIDTH = 190;
constexpr int HERO_HEIGHT = 260;
/** Two stacked panels fill the height beside the hero. */
constexpr int SIDE_HEIGHT = (HERO_HEIGHT - GUTTER) / 2;
// 54, not 58: at 58 the worst-case menu's last row ends exactly on the button
// hints. The static_assert below would still pass, but flush is not clearance.
constexpr int STATS_HEIGHT = 54;

constexpr int COVER_HEIGHT = HERO_HEIGHT;
/** Hero, plus the two volumes beside it. */
constexpr int PANEL_COUNT = 3;

constexpr int TILE_HEIGHT = HERO_HEIGHT + GUTTER + STATS_HEIGHT + SLANT * 2;

// The home screen draws the button menu directly under this block, and the rect
// it is given has a fixed height that does NOT subtract the cover tile -- so
// nothing at runtime stops a tall block from pushing the last menu row into the
// button hints. Check it here instead.
//
// Worst case is five rows: Browse / Recent / OPDS / Transfer / Settings.
namespace layout_check {
constexpr int MENU_ROWS_WORST_CASE = 5;
constexpr int PORTRAIT_PANEL_HEIGHT = 800;
constexpr int MENU_TOP = LyraMetrics::values.homeTopPadding + TILE_HEIGHT + LyraMetrics::values.homeMenuTopOffset;
constexpr int MENU_HEIGHT = MENU_ROWS_WORST_CASE * LyraMetrics::values.menuRowHeight +
                            (MENU_ROWS_WORST_CASE - 1) * LyraMetrics::values.menuSpacing;
static_assert(MENU_TOP + MENU_HEIGHT <= PORTRAIT_PANEL_HEIGHT - LyraMetrics::values.buttonHintsHeight,
              "Collection block is too tall: the home menu would overrun the button hints. "
              "Reduce HERO_HEIGHT or STATS_HEIGHT.");
}  // namespace layout_check

constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeCoverHeight = COVER_HEIGHT;
  v.homeCoverTileHeight = TILE_HEIGHT;
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
