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
constexpr int COVER_HEIGHT = 150;
/** Ledge the covers stand on. */
constexpr int SHELF_THICKNESS = 5;
/** Gap between a shelf ledge and the next row of covers. */
constexpr int ROW_GAP = 10;
/** Strip under the grid holding the selected book's title. */
constexpr int TITLE_STRIP_HEIGHT = 14;

constexpr int ROW_HEIGHT = COVER_HEIGHT + SHELF_THICKNESS + ROW_GAP;

constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeCoverHeight = COVER_HEIGHT;
  v.homeCoverTileHeight = ROWS * ROW_HEIGHT + TITLE_STRIP_HEIGHT;
  v.homeRecentBooksCount = COLUMNS * ROWS;
  return v;
}();

}  // namespace CollectionMetrics

class CollectionTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
};
