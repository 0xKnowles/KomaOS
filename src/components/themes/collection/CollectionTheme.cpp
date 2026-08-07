#include "CollectionTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"
#include "util/SeriesTitle.h"
#include "util/XtcProgress.h"

// File scope, not inside the anonymous namespace below: the member functions
// need COLUMNS/ROWS/ROW_HEIGHT too.
using namespace CollectionMetrics;

namespace {

/** Inset of the whole shelf block from the panel edge. */
constexpr int SHELF_SIDE_PADDING = 12;
/** Gutter between the two cards in a row, split evenly. */
constexpr int CARD_GAP = 10;
/** Inset of a cover from its card's left edge. */
constexpr int CARD_PADDING = 5;
/** Gap between a cover and the text beside it. */
constexpr int TEXT_GAP = 8;
/** How far the selection bracket sits outside the cover. */
constexpr int SELECTION_OFFSET = 3;
/** Thickness of the selection bracket. */
constexpr int SELECTION_BORDER = 2;

/** Thinner than a shelf ledge: the header rule is a boundary, not furniture. */
constexpr int HEADER_LEDGE_THICKNESS = 2;

/** Volume badge sits in the cover's bottom-left corner. */
constexpr int BADGE_PADDING = 3;

/** Breathing room above and below the stats box inside its slot. */
constexpr int STATS_BOX_MARGIN = 4;
/** Inset of the stats text from the box outline. */
constexpr int STATS_PADDING = 5;

/** Accent tab marking the selected menu row. */
constexpr int MENU_ACCENT_WIDTH = 4;
/** Inset so the tab floats inside the row's rounded fill instead of fighting its corners. */
constexpr int MENU_ACCENT_INSET = 6;

struct CellGeometry {
  int x;       // left edge of the card
  int y;       // top edge of the card, which is also the cover's top
  int width;   // card width, gutter already removed
  int coverX;  // left edge of the cover
  int textX;   // left edge of the text column beside the cover
  int textW;   // width available to the text column
};

CellGeometry cellFor(const Rect& rect, const int index) {
  const int gridWidth = rect.width - 2 * SHELF_SIDE_PADDING;
  const int cellWidth = gridWidth / COLUMNS;
  const int column = index % COLUMNS;
  const int row = index / COLUMNS;

  CellGeometry cell{};
  cell.x = rect.x + SHELF_SIDE_PADDING + column * cellWidth + CARD_GAP / 2;
  cell.y = rect.y + GRID_TOP_INSET + row * ROW_HEIGHT;
  cell.width = cellWidth - CARD_GAP;
  cell.coverX = cell.x + CARD_PADDING;
  cell.textX = cell.coverX + COVER_WIDTH + TEXT_GAP;
  cell.textW = cell.x + cell.width - CARD_PADDING - cell.textX;
  return cell;
}

/**
 * Draws one cover, or a labelled placeholder when the book has no cover art.
 *
 * Returns nothing: a failed cover load falls through to the placeholder rather
 * than leaving a hole, so the shelf never renders with a gap where a volume is.
 */
void drawCoverArt(const GfxRenderer& renderer, const RecentBook& book, const CellGeometry& cell) {
  bool hasCover = false;

  if (!book.coverBmpPath.empty()) {
    const std::string coverBmpPath = UITheme::getCoverThumbPath(book.coverBmpPath, COVER_HEIGHT);

    HalFile file;
    if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getHeight() > 0) {
        // Crop horizontally to fill the slot rather than letterboxing: manga
        // covers vary in aspect and a ragged shelf edge reads as a bug.
        const float coverRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float slotRatio = static_cast<float>(COVER_WIDTH) / static_cast<float>(COVER_HEIGHT);
        const float cropX = coverRatio > 0.0f ? 1.0f - (slotRatio / coverRatio) : 0.0f;

        renderer.drawBitmap(bitmap, cell.coverX, cell.y, COVER_WIDTH, COVER_HEIGHT, std::max(0.0f, cropX));
        hasCover = true;
      }
      file.close();
    }
  }

  if (!hasCover) {
    // Placeholder: a grey block with the cover glyph, so an un-thumbnailed book
    // still occupies its slot and stays selectable.
    renderer.fillRectDither(cell.coverX, cell.y, COVER_WIDTH, COVER_HEIGHT, Color::LightGray);
    renderer.drawIcon(CoverIcon, cell.coverX + (COVER_WIDTH - 32) / 2, cell.y + (COVER_HEIGHT - 32) / 2, 32);
  }

  // Cover art is a rectangle whatever we do with it, so round it by painting
  // white back over the four corners, then outline the rounded shape. Without
  // the mask the outline's curve would have square bitmap corners poking
  // through it.
  renderer.maskRoundedRectOutsideCorners(cell.coverX, cell.y, COVER_WIDTH, COVER_HEIGHT, CORNER_RADIUS);
  // Outline every cover so a light one does not bleed into the paper.
  renderer.drawRoundedRect(cell.coverX, cell.y, COVER_WIDTH, COVER_HEIGHT, 1, CORNER_RADIUS, true);

  // Volume badge, reversed out of a solid block in the bottom-left corner.
  // Cover art is unpredictable, so plain text over it would be illegible on a
  // dark cover; the block guarantees contrast whatever is underneath.
  const SeriesTitle::Parsed parsed = SeriesTitle::parse(book.title);
  if (parsed.hasVolume()) {
    const std::string label = SeriesTitle::badge(parsed.volume);
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label.c_str(), EpdFontFamily::BOLD);
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int badgeW = textWidth + 2 * BADGE_PADDING;
    const int badgeH = lineHeight + BADGE_PADDING;
    // Inset by the corner radius so the badge sits inside the rounded outline
    // rather than being clipped by the curve.
    const int badgeX = cell.coverX + CORNER_RADIUS;
    const int badgeY = cell.y + COVER_HEIGHT - badgeH - CORNER_RADIUS;

    renderer.fillRect(badgeX, badgeY, badgeW, badgeH, true);
    renderer.drawText(SMALL_FONT_ID, badgeX + BADGE_PADDING, badgeY, label.c_str(), false, EpdFontFamily::BOLD);
  }
}

/**
 * Series name and volume in the space beside the cover.
 *
 * The series name rather than the raw title: "Berserk v03" already carries its
 * volume on the badge, so repeating it here would waste two of the three lines
 * this column has.
 */
void drawCoverLabel(const GfxRenderer& renderer, const RecentBook& book, const CellGeometry& cell) {
  if (cell.textW <= 0) {
    return;
  }

  const SeriesTitle::Parsed parsed = SeriesTitle::parse(book.title);
  const std::string& name = parsed.series.empty() ? book.title : parsed.series;

  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const auto lines = renderer.wrappedText(SMALL_FONT_ID, name.c_str(), cell.textW, 3, EpdFontFamily::BOLD);

  // Author only if the wrapped name left room for it, so a long series name is
  // never traded against a line of text nobody is looking for.
  const bool showAuthor = !book.author.empty() && lines.size() <= 2;
  const int blockHeight = static_cast<int>(lines.size()) * lineHeight + (showAuthor ? lineHeight * 3 / 2 : 0);

  int y = cell.y + (COVER_HEIGHT - blockHeight) / 2;
  for (const auto& line : lines) {
    renderer.drawText(SMALL_FONT_ID, cell.textX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += lineHeight;
  }
  if (showAuthor) {
    y += lineHeight / 2;
    renderer.drawText(SMALL_FONT_ID, cell.textX, y,
                      renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), cell.textW).c_str(), true);
  }
}

/**
 * The theme's signature rule: a solid bar with a dithered lip under it.
 *
 * Used for the shelf ledges and again under the header, so every screen carries
 * the same edge treatment rather than the motif living only on the home screen.
 * The dithered lip reads as depth on 1-bit e-ink, where a second solid line
 * would just look like a thicker bar.
 */
void drawLedge(const GfxRenderer& renderer, const int x, const int y, const int width, const int thickness) {
  renderer.fillRect(x, y, width, thickness, true);
  renderer.fillRectDither(x, y + thickness, width, 2, Color::LightGray);
}

/** The ledge a row of cards stands on, drawn full width like a real shelf. */
void drawShelf(const GfxRenderer& renderer, const Rect& rect, const int row) {
  drawLedge(renderer, rect.x + SHELF_SIDE_PADDING, rect.y + GRID_TOP_INSET + row * ROW_HEIGHT + COVER_HEIGHT,
            rect.width - 2 * SHELF_SIDE_PADDING, SHELF_THICKNESS);
}

/**
 * Bracket around the selected card.
 *
 * Drawn outside the cover so no art is hidden, and around the whole card so the
 * series name beside it is visibly part of the same selection. It stops at the
 * shelf line: a bracket that crossed the ledge would read as a box floating in
 * front of the shelf rather than a volume standing on it.
 */
void drawSelection(const GfxRenderer& renderer, const CellGeometry& cell) {
  for (int i = 0; i < SELECTION_BORDER; i++) {
    const int inset = SELECTION_OFFSET - i;
    renderer.drawRoundedRect(cell.coverX - inset, cell.y - inset, COVER_WIDTH + 2 * inset, COVER_HEIGHT + 2 * inset, 1,
                             CORNER_RADIUS + inset, true);
  }
}

/** One label-over-value column of the stats box. */
void drawStatCell(const GfxRenderer& renderer, const Rect& cellRect, const char* label, const char* value) {
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int top = cellRect.y + (cellRect.height - 2 * lineHeight) / 2;
  UITheme::drawCenteredText(renderer, cellRect, SMALL_FONT_ID, top, label, true);
  UITheme::drawCenteredText(renderer, cellRect, SMALL_FONT_ID, top + lineHeight, value, true, EpdFontFamily::BOLD);
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
  // Captured before the compose below flips coverRendered: a recompose is
  // exactly when the recent list can have changed under the progress cache.
  const bool composing = !coverRendered;

  // Covers come off the SD card once, then the composed shelf is cached in the
  // stored buffer; only the selection bracket and the stats box are redrawn per
  // frame. Re-reading four BMPs on every selector move would make the home
  // screen unusable.
  if (!coverRendered) {
    for (int i = 0; i < bookCount; i++) {
      const CellGeometry cell = cellFor(rect, i);
      drawCoverArt(renderer, recentBooks[i], cell);
      drawCoverLabel(renderer, recentBooks[i], cell);
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

  // Progress is read on the same pass as the covers, and kept until the shelf
  // is next recomposed -- which is exactly when the recent list can have
  // changed underneath it.
  if (composing || !shelfProgressLoaded) {
    for (int i = 0; i < static_cast<int>(shelfProgress.size()); i++) {
      shelfProgress[i] =
          i < bookCount ? XtcProgress::read(XtcProgress::cachePathFor(recentBooks[i].path)) : XtcProgress::Snapshot{};
    }
    shelfProgressLoaded = true;
  }

  if (selectorIndex >= 0 && selectorIndex < bookCount) {
    drawSelection(renderer, cellFor(rect, selectorIndex));
  }

  drawStatsBox(renderer, rect, recentBooks, selectorIndex);
}

void CollectionTheme::drawStatsBox(const GfxRenderer& renderer, const Rect rect,
                                   const std::vector<RecentBook>& recentBooks, const int selectorIndex) const {
  const int slotY = rect.y + GRID_TOP_INSET + ROWS * ROW_HEIGHT;

  // Cleared and redrawn every frame: it reports the selected volume, so unlike
  // the shelf above it, the copy held in the stored cover buffer is stale the
  // moment the selector moves.
  renderer.fillRect(rect.x, slotY, rect.width, STATS_BOX_HEIGHT, false);

  const int boxX = rect.x + SHELF_SIDE_PADDING;
  const int boxY = slotY + STATS_BOX_MARGIN;
  const int boxW = rect.width - 2 * SHELF_SIDE_PADDING;
  const int boxH = STATS_BOX_HEIGHT - 2 * STATS_BOX_MARGIN;

  renderer.drawRoundedRect(boxX, boxY, boxW, boxH, 1, CORNER_RADIUS, true);

  const int bookCount = std::min(static_cast<int>(recentBooks.size()), COLUMNS * ROWS);
  const bool hasSelection = selectorIndex >= 0 && selectorIndex < bookCount;

  // Volume, page position and percentage of whichever card is selected. Not
  // library-wide totals: the recent list handed to this theme is already capped
  // at four and collapsed to one entry per series, so any "total" drawn from it
  // would be a count of the shelf, dressed up as a count of the library.
  // Default to a dash: a cell whose value cannot be computed says so, rather
  // than showing a plausible-looking zero.
  char volumeText[8] = "--";
  char pageText[24] = "--";
  char percentText[8] = "--";

  if (hasSelection) {
    const SeriesTitle::Parsed parsed = SeriesTitle::parse(recentBooks[selectorIndex].title);
    if (parsed.hasVolume()) {
      snprintf(volumeText, sizeof(volumeText), "%s", SeriesTitle::badge(parsed.volume).c_str());
    }

    const XtcProgress::Snapshot& progress = shelfProgress[selectorIndex];
    if (progress.hasPageCount()) {
      // Page position as well as the percentage: "88 / 210" is what you act on
      // when hunting a scene, and the percentage is what you glance at.
      snprintf(pageText, sizeof(pageText), "%lu / %lu", static_cast<unsigned long>(progress.page + 1),
               static_cast<unsigned long>(progress.pageCount));
      snprintf(percentText, sizeof(percentText), "%d%%", progress.percent());
    } else if (progress.valid) {
      // Progress written by older firmware, or an EPUB on the shelf: the page
      // is known but nothing it could be a fraction of is.
      snprintf(pageText, sizeof(pageText), "%lu", static_cast<unsigned long>(progress.page + 1));
    }
  }

  const int cellWidth = (boxW - 2 * STATS_PADDING) / 3;
  const int cellsX = boxX + STATS_PADDING;
  // Three labels share the box's width, so each has roughly 148px. Anything
  // longer is clipped rather than wrapped -- keep translations of these short.
  const char* labels[3] = {tr(STR_STAT_VOLUME), tr(STR_STAT_PAGE), tr(STR_STAT_DONE)};
  const char* values[3] = {volumeText, pageText, percentText};

  for (int i = 0; i < 3; i++) {
    drawStatCell(renderer, Rect{cellsX + i * cellWidth, boxY, cellWidth, boxH}, labels[i], values[i]);

    // Dotted dividers between the cells. DarkGray, not LightGray: LightGray
    // inks only x%2==0 && y%2==0, so a one-pixel column of it is every fourth
    // pixel and effectively invisible.
    if (i > 0) {
      renderer.fillRectDither(cellsX + i * cellWidth, boxY + STATS_PADDING, 1, boxH - 2 * STATS_PADDING,
                              Color::DarkGray);
    }
  }
}

void CollectionTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                     const std::function<std::string(int index)>& buttonLabel,
                                     const std::function<UIIcon(int index)>& rowIcon) const {
  // Icons and labels first: iconForName lives in LyraTheme.cpp's anonymous
  // namespace, so delegating is the only way to reuse the lookup rather than
  // duplicating the whole icon table here.
  LyraTheme::drawButtonMenu(renderer, rect, buttonCount, selectedIndex, buttonLabel, rowIcon);

  const int pad = LyraMetrics::values.contentSidePadding;
  const int rowHeight = LyraMetrics::values.menuRowHeight;
  const int step = rowHeight + LyraMetrics::values.menuSpacing;
  const int left = rect.x + pad;
  const int right = rect.x + rect.width - pad;

  for (int i = 0; i < buttonCount; i++) {
    const int rowY = rect.y + i * step;

    if (i == selectedIndex) {
      // A solid tab down the left edge. Reads at a glance against the light
      // fill Lyra already drew, and unlike inverting the row it does not need
      // white text or a white icon -- drawIcon only ever draws ink.
      renderer.fillRect(left + 2, rowY + MENU_ACCENT_INSET, MENU_ACCENT_WIDTH, rowHeight - 2 * MENU_ACCENT_INSET, true);

      // Chevron on the right, so the selected row reads as "this one opens".
      const int chevronX = right - 20;
      const int chevronY = rowY + rowHeight / 2;
      renderer.drawLine(chevronX, chevronY - 7, chevronX + 7, chevronY, 2, true);
      renderer.drawLine(chevronX + 7, chevronY, chevronX, chevronY + 7, 2, true);
    } else if (i + 1 < buttonCount && i + 1 != selectedIndex) {
      // Hairline between two unselected rows so the block reads as a list
      // rather than floating text. Skipped next to the selected row, where the
      // fill already provides the separation.
      const int separatorY = rowY + rowHeight + LyraMetrics::values.menuSpacing / 2;
      renderer.fillRectDither(left + MENU_ACCENT_INSET, separatorY, right - left - 2 * MENU_ACCENT_INSET, 1,
                              Color::DarkGray);
    }
  }
}

void CollectionTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                                 const char* subtitle) const {
  LyraTheme::drawHeader(renderer, rect, title, subtitle);
  // Same edge treatment as a shelf ledge, so file browser, settings and reader
  // screens all read as part of the same theme rather than plain Lyra with a
  // different home screen.
  drawLedge(renderer, rect.x, rect.y + rect.height - HEADER_LEDGE_THICKNESS, rect.width, HEADER_LEDGE_THICKNESS);
}
