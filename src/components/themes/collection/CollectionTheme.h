#pragma once

#include <array>

#include "components/themes/lyra/LyraTheme.h"
#include "util/XtcProgress.h"

class GfxRenderer;

/**
 * Collection: the home screen's recent books as a bookshelf.
 *
 * Four volumes in a 2x2 grid, one per series, each as a card: cover on the
 * left, series name beside it, volume badged on the cover. A stats box closes
 * the block, reporting the selected volume's number, page position and
 * percentage read.
 *
 * The card layout is forced by arithmetic, not taste. A 2x2 grid on a 480px
 * panel gives ~228px-wide cells, but the vertical budget (see layout_check
 * below) caps a cover at 130px tall, which is only ~91px wide at a typical
 * manga aspect. Centring a 91px cover in a 228px cell leaves it swimming; using
 * the spare width for the series name is what makes the bigger grid worth
 * having.
 *
 * The stats box reads the selected volume rather than totalling the shelf: the
 * recent list this theme is handed is already capped at four and collapsed to
 * one entry per series, so a "library total" drawn from it would really be a
 * count of the shelf. It also cannot report anything time-based -- the X4 has
 * no RTC and nothing on the SD card is timestamped, so streaks and pages-per-day
 * are not computable, however much they would suit the genre.
 *
 * Everything else (lists, keyboard, popups) is inherited from Lyra; this
 * overrides the home cover block, the menu styling and the header rule.
 */

namespace CollectionMetrics {

constexpr int COLUMNS = 2;
constexpr int ROWS = 2;
constexpr int COVER_HEIGHT = 130;
/** Cover width, at roughly the aspect a manga cover is drawn to. */
constexpr int COVER_WIDTH = 91;
/** Ledge the covers stand on. */
constexpr int SHELF_THICKNESS = 5;
/** Gap between a shelf ledge and the next row of covers. */
constexpr int ROW_GAP = 8;
/** Stats box closing the block, below the last shelf. */
constexpr int STATS_BOX_HEIGHT = 44;
/** Corner radius for covers, cards and the stats box. */
constexpr int CORNER_RADIUS = 4;
/**
 * Blank strip above the first row of covers.
 *
 * The selection bracket is drawn outside the cover it marks, so without this
 * the top row's bracket would land above the tile rect -- which is also the
 * region the home screen snapshots and repaints from. Anything drawn outside it
 * is never erased, so a bracket there would smear across the header as the
 * selector moved.
 */
constexpr int GRID_TOP_INSET = 4;

constexpr int ROW_HEIGHT = COVER_HEIGHT + SHELF_THICKNESS + ROW_GAP;
constexpr int TILE_HEIGHT = GRID_TOP_INSET + ROWS * ROW_HEIGHT + STATS_BOX_HEIGHT;

// The home screen draws the button menu directly under this block, and the rect
// it is given has a fixed height that does NOT subtract the cover tile -- so
// nothing at runtime stops a tall shelf from pushing the last menu row into the
// button hints. Check it here instead.
//
// Worst case is five rows: Browse / Recent / OPDS / Transfer / Settings.
// "Continue Reading" is not among them because Lyra sets
// homeContinueReadingInMenu = false, which this theme inherits.
namespace layout_check {
constexpr int MENU_ROWS_WORST_CASE = 5;
constexpr int PORTRAIT_PANEL_HEIGHT = 800;
constexpr int MENU_TOP = LyraMetrics::values.homeTopPadding + TILE_HEIGHT + LyraMetrics::values.homeMenuTopOffset;
constexpr int MENU_HEIGHT = MENU_ROWS_WORST_CASE * LyraMetrics::values.menuRowHeight +
                            (MENU_ROWS_WORST_CASE - 1) * LyraMetrics::values.menuSpacing;
static_assert(MENU_TOP + MENU_HEIGHT <= PORTRAIT_PANEL_HEIGHT - LyraMetrics::values.buttonHintsHeight,
              "Collection shelf is too tall: the home menu would overrun the button hints. "
              "Reduce COVER_HEIGHT, ROW_GAP or STATS_BOX_HEIGHT.");
}  // namespace layout_check

constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeCoverHeight = COVER_HEIGHT;
  v.homeCoverTileHeight = TILE_HEIGHT;
  v.homeRecentBooksCount = COLUMNS * ROWS;
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
  mutable std::array<XtcProgress::Snapshot, CollectionMetrics::COLUMNS * CollectionMetrics::ROWS> shelfProgress{};
  mutable bool shelfProgressLoaded = false;

  void drawStatsBox(const GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
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
