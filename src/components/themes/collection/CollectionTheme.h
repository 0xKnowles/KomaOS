#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

/**
 * Collection: the home screen's recent books as a bookshelf.
 *
 * Where the other themes surface one or three recent titles as cards, this one
 * lays the last eight out as cover art standing on two shelves. For manga that
 * is the more useful view -- volumes are recognised by their spine art long
 * before their filename, and a series read in order fills a shelf.
 *
 * Everything else (lists, header, keyboard, popups) is inherited from Lyra
 * unchanged; only drawRecentBookCover is overridden.
 */

namespace CollectionMetrics {

// 4 columns x 2 rows. Ten is all RecentBooksStore keeps (MAX_RECENT_BOOKS), so
// eight fills the shelf without the grid ever looking half-empty for a reader
// with a modest history.
constexpr int COLUMNS = 4;
constexpr int ROWS = 2;
constexpr int COVER_HEIGHT = 138;
/** Ledge the covers stand on. */
constexpr int SHELF_THICKNESS = 5;
/** Gap between a shelf ledge and the next row of covers. */
constexpr int ROW_GAP = 8;
/** Strip under the grid holding the selected book's title. */
constexpr int TITLE_STRIP_HEIGHT = 14;

constexpr int ROW_HEIGHT = COVER_HEIGHT + SHELF_THICKNESS + ROW_GAP;
constexpr int TILE_HEIGHT = ROWS * ROW_HEIGHT + TITLE_STRIP_HEIGHT;

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
              "Reduce COVER_HEIGHT or ROW_GAP.");
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
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  // Lyra's menu is five rows of icon-and-label with a light fill on the
  // selected one and nothing else, which reads as very flat under a shelf of
  // cover art. This delegates to Lyra for the icons and labels -- the icon
  // lookup is file-local to LyraTheme.cpp -- then overlays the styling.
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  // Carries the shelf's ledge motif onto every other screen.
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
};
