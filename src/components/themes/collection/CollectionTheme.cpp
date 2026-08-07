#include "CollectionTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

// File scope, not inside the anonymous namespace below: drawRecentBookCover
// itself needs COLUMNS/ROWS/ROW_HEIGHT too.
using namespace CollectionMetrics;

namespace {

/** Breathing room either side of a cover inside its grid cell. */
constexpr int TILE_H_PADDING = 8;
/** Thickness of the selection bracket drawn around the chosen cover. */
constexpr int SELECTION_BORDER = 3;

struct CellGeometry {
  int x;       // left edge of the cell
  int y;       // top edge of the cell (cover top)
  int width;   // cell width
  int coverX;  // left edge of the cover itself
  int coverW;  // cover width
};

CellGeometry cellFor(const Rect& rect, const int index) {
  const int cellWidth = rect.width / COLUMNS;
  const int column = index % COLUMNS;
  const int row = index / COLUMNS;

  CellGeometry cell{};
  cell.x = rect.x + column * cellWidth;
  cell.y = rect.y + row * ROW_HEIGHT;
  cell.width = cellWidth;
  cell.coverX = cell.x + TILE_H_PADDING;
  cell.coverW = cellWidth - 2 * TILE_H_PADDING;
  return cell;
}

/**
 * Draws one cover, or a labelled placeholder when the book has no cover art.
 *
 * Returns nothing: a failed cover load falls through to the placeholder rather
 * than leaving a hole, so the shelf never renders with a gap where a volume is.
 */
void drawCoverArt(GfxRenderer& renderer, const RecentBook& book, const CellGeometry& cell) {
  bool hasCover = false;

  if (!book.coverBmpPath.empty()) {
    const std::string coverBmpPath = UITheme::getCoverThumbPath(book.coverBmpPath, COVER_HEIGHT);

    HalFile file;
    if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getHeight() > 0) {
        // Crop horizontally to fill the cell rather than letterboxing: manga
        // covers vary in aspect and a ragged shelf edge reads as a bug.
        const float coverRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float cellRatio = static_cast<float>(cell.coverW) / static_cast<float>(COVER_HEIGHT);
        const float cropX = coverRatio > 0.0f ? 1.0f - (cellRatio / coverRatio) : 0.0f;

        renderer.drawBitmap(bitmap, cell.coverX, cell.y, cell.coverW, COVER_HEIGHT, std::max(0.0f, cropX));
        hasCover = true;
      }
      file.close();
    }
  }

  if (!hasCover) {
    // Placeholder: a grey block with the cover glyph, so an un-thumbnailed book
    // still occupies its slot and stays selectable.
    renderer.fillRectDither(cell.coverX, cell.y, cell.coverW, COVER_HEIGHT, Color::LightGray);
    renderer.drawIcon(CoverIcon, cell.coverX + (cell.coverW - 32) / 2, cell.y + (COVER_HEIGHT - 32) / 2, 32);
  }

  // Outline every cover so a light cover does not bleed into the paper.
  renderer.drawRect(cell.coverX, cell.y, cell.coverW, COVER_HEIGHT, true);
}

/** The ledge a row of covers stands on, drawn full width like a real shelf. */
void drawShelf(const GfxRenderer& renderer, const Rect& rect, const int row) {
  const int shelfY = rect.y + row * ROW_HEIGHT + COVER_HEIGHT;
  renderer.fillRect(rect.x, shelfY, rect.width, SHELF_THICKNESS, true);
  // A dithered lip under the solid ledge reads as depth on 1-bit e-ink, where a
  // second solid line would just look like a thicker shelf.
  renderer.fillRectDither(rect.x, shelfY + SHELF_THICKNESS, rect.width, 2, Color::LightGray);
}

/** Bracket around the selected cover: drawn outside it so no art is hidden. */
void drawSelection(const GfxRenderer& renderer, const CellGeometry& cell) {
  for (int i = 1; i <= SELECTION_BORDER; i++) {
    renderer.drawRect(cell.coverX - i, cell.y - i, cell.coverW + 2 * i, COVER_HEIGHT + 2 * i, true);
  }
}

}  // namespace

void CollectionTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                          const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                          bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int bookCount = std::min(static_cast<int>(recentBooks.size()), COLUMNS * ROWS);

  // Covers come off the SD card once, then the composed shelf is cached in the
  // stored buffer; only the selection bracket and title are redrawn per frame.
  // Re-reading eight BMPs on every selector move would make the home screen
  // unusable.
  if (!coverRendered) {
    for (int i = 0; i < bookCount; i++) {
      drawCoverArt(renderer, recentBooks[i], cellFor(rect, i));
    }
    for (int row = 0; row < ROWS; row++) {
      // Draw a shelf under any row that has at least one book on it.
      if (row * COLUMNS < bookCount) {
        drawShelf(renderer, rect, row);
      }
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;  // Only "rendered" if the buffer actually stored
  }

  if (selectorIndex >= 0 && selectorIndex < bookCount) {
    drawSelection(renderer, cellFor(rect, selectorIndex));

    // The shelf shows art, not filenames, so the selected volume names itself
    // in the strip below the grid.
    const int titleY = rect.y + ROWS * ROW_HEIGHT;
    renderer.fillRect(rect.x, titleY, rect.width, TITLE_STRIP_HEIGHT, false);
    UITheme::drawCenteredText(renderer, rect, SMALL_FONT_ID, titleY, recentBooks[selectorIndex].title.c_str(), true,
                              EpdFontFamily::BOLD);
  }
}
