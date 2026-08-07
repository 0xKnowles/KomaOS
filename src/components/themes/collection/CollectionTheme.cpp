#include "CollectionTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"
#include "util/SeriesTitle.h"
#include "util/XtcProgress.h"

// File scope, not inside the anonymous namespace below: the member functions
// need the panel metrics too.
using namespace CollectionMetrics;

namespace {

/** Thinner than a panel border: the header rule is a boundary, not furniture. */
constexpr int HEADER_LEDGE_THICKNESS = 2;
/** Volume badge inset from a cover's corner. */
constexpr int BADGE_PADDING = 3;
/** Accent tab marking the selected menu row. */
constexpr int MENU_ACCENT_WIDTH = 4;
constexpr int MENU_ACCENT_INSET = 6;

/**
 * A manga panel: four corners, in clockwise order from the top left.
 *
 * Stored as corners rather than a Rect because the whole point is that opposite
 * edges are not parallel. Every corner carries a few pixels of displacement, so
 * no two panels share an angle and the block stops reading as a table.
 */
struct Koma {
  int x[4];
  int y[4];

  int minX() const { return std::min(std::min(x[0], x[1]), std::min(x[2], x[3])); }
  int maxX() const { return std::max(std::max(x[0], x[1]), std::max(x[2], x[3])); }
  int minY() const { return std::min(std::min(y[0], y[1]), std::min(y[2], y[3])); }
  int maxY() const { return std::max(std::max(y[0], y[1]), std::max(y[2], y[3])); }
};

/**
 * Builds a panel from an upright rect plus a per-corner slant pattern.
 *
 * `pattern` picks one of a fixed set of displacements. Fixed, not random: the
 * home screen redraws from a cached buffer and a random tilt would jitter every
 * time the selector moved.
 */
Koma komaFrom(const int left, const int top, const int width, const int height, const int pattern) {
  // dx/dy per corner, in units of SLANT. Each row leans differently so adjacent
  // panels never line up along a shared edge.
  static constexpr int8_t SLANTS[4][8] = {
      {0, 1, 1, 0, 0, -1, -1, 0},
      {1, 0, 0, 1, -1, 0, 0, -1},
      {0, -1, 1, 1, 0, 1, -1, -1},
      {-1, 1, 0, -1, 1, -1, 0, 1},
  };
  const int8_t* d = SLANTS[pattern & 3];

  Koma k{};
  const int cx[4] = {left, left + width, left + width, left};
  const int cy[4] = {top, top, top + height, top + height};
  for (int i = 0; i < 4; i++) {
    k.x[i] = cx[i] + d[i * 2] * SLANT;
    k.y[i] = cy[i] + d[i * 2 + 1] * SLANT;
  }
  return k;
}

/** Left and right edge of the panel on a given row, or false when outside it. */
bool komaSpanAt(const Koma& koma, const int y, int& outLeft, int& outRight) {
  int left = INT32_MAX;
  int right = INT32_MIN;
  for (int i = 0; i < 4; i++) {
    const int j = (i + 1) & 3;
    const int y0 = koma.y[i];
    const int y1 = koma.y[j];
    if (y0 == y1) continue;
    if (y < std::min(y0, y1) || y >= std::max(y0, y1)) continue;
    // Edge crossing this row, by similar triangles.
    const int cross = koma.x[i] + (koma.x[j] - koma.x[i]) * (y - y0) / (y1 - y0);
    left = std::min(left, cross);
    right = std::max(right, cross);
  }
  if (left > right) return false;
  outLeft = left;
  outRight = right;
  return true;
}

/**
 * Paints white everywhere inside the panel's bounding box but outside the panel.
 *
 * This is what lets upright cover art sit in a tilted frame: draw the bitmap
 * across the whole box, then cut it back to the quad. There is no polygon fill
 * in the renderer and no way to rotate a bitmap, so masking is the only route.
 */
void maskOutsideKoma(const GfxRenderer& renderer, const Koma& koma) {
  const int top = koma.minY();
  const int bottom = koma.maxY();
  const int left = koma.minX();
  const int right = koma.maxX();

  for (int y = top; y < bottom; y++) {
    int spanLeft, spanRight;
    if (!komaSpanAt(koma, y, spanLeft, spanRight)) {
      renderer.fillRect(left, y, right - left, 1, false);
      continue;
    }
    if (spanLeft > left) renderer.fillRect(left, y, spanLeft - left, 1, false);
    if (spanRight < right) renderer.fillRect(spanRight, y, right - spanRight, 1, false);
  }
}

/** Inks the panel's four edges. */
void drawKomaBorder(const GfxRenderer& renderer, const Koma& koma, const int weight) {
  for (int i = 0; i < 4; i++) {
    const int j = (i + 1) & 3;
    renderer.drawLine(koma.x[i], koma.y[i], koma.x[j], koma.y[j], weight, true);
  }
}

/** Dithered fill inside the panel, for a placeholder or an emphasis panel. */
void fillKoma(const GfxRenderer& renderer, const Koma& koma, const Color color) {
  for (int y = koma.minY(); y < koma.maxY(); y++) {
    int spanLeft, spanRight;
    if (komaSpanAt(koma, y, spanLeft, spanRight) && spanRight > spanLeft) {
      renderer.fillRectDither(spanLeft, y, spanRight - spanLeft, 1, color);
    }
  }
}

/** Cover art filling the panel, cut back to its quad. */
void drawKomaCover(const GfxRenderer& renderer, const RecentBook& book, const Koma& koma) {
  const int left = koma.minX();
  const int top = koma.minY();
  const int width = koma.maxX() - left;
  const int height = koma.maxY() - top;
  bool hasCover = false;

  if (!book.coverBmpPath.empty()) {
    HalFile file;
    if (Storage.openFileForRead("HOME", UITheme::getCoverThumbPath(book.coverBmpPath, COVER_HEIGHT), file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getHeight() > 0) {
        // Crop to fill rather than letterbox: a panel with paper showing down
        // one side reads as a mistake, not a margin.
        const float coverRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float slotRatio = static_cast<float>(width) / static_cast<float>(height);
        const float cropX = coverRatio > 0.0f ? 1.0f - (slotRatio / coverRatio) : 0.0f;
        renderer.drawBitmap(bitmap, left, top, width, height, std::max(0.0f, cropX));
        hasCover = true;
      }
      file.close();
    }
  }

  if (!hasCover) {
    fillKoma(renderer, koma, Color::LightGray);
    renderer.drawIcon(CoverIcon, left + (width - 32) / 2, top + (height - 32) / 2, 32);
  }

  maskOutsideKoma(renderer, koma);
  drawKomaBorder(renderer, koma, BORDER);
}

/** Volume badge, reversed out of solid ink so it reads over any artwork. */
void drawVolumeBadge(const GfxRenderer& renderer, const RecentBook& book, const Koma& koma) {
  const SeriesTitle::Parsed parsed = SeriesTitle::parse(book.title);
  if (!parsed.hasVolume()) return;

  const std::string label = SeriesTitle::badge(parsed.volume);
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label.c_str(), EpdFontFamily::BOLD);
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int badgeW = textWidth + 2 * BADGE_PADDING;
  const int badgeH = lineHeight + BADGE_PADDING;
  // Anchored to the bottom-left corner's own position, so the badge leans with
  // the panel instead of floating off a tilted edge.
  const int badgeX = koma.x[3] + SLANT + BADGE_PADDING;
  const int badgeY = koma.y[3] - badgeH - BADGE_PADDING;

  renderer.fillRect(badgeX, badgeY, badgeW, badgeH, true);
  renderer.drawText(SMALL_FONT_ID, badgeX + BADGE_PADDING, badgeY, label.c_str(), false, EpdFontFamily::BOLD);
}

/** Selection bracket: a second border outside the panel, following its slant. */
void drawKomaSelection(const GfxRenderer& renderer, const Koma& koma) {
  const int cx = (koma.minX() + koma.maxX()) / 2;
  const int cy = (koma.minY() + koma.maxY()) / 2;
  Koma outer{};
  for (int i = 0; i < 4; i++) {
    // Pushed outward from the centre, so the bracket stays parallel to the edge
    // it marks rather than closing in at the corners.
    outer.x[i] = koma.x[i] + (koma.x[i] > cx ? 3 : -3);
    outer.y[i] = koma.y[i] + (koma.y[i] > cy ? 3 : -3);
  }
  drawKomaBorder(renderer, outer, BORDER);
}

/** The header's edge treatment, carried over from the panel borders. */
void drawLedge(const GfxRenderer& renderer, const int x, const int y, const int width, const int thickness) {
  renderer.fillRect(x, y, width, thickness, true);
  renderer.fillRectDither(x, y + thickness, width, 2, Color::LightGray);
}

/** Panel geometry for slot `index`: 0 is the hero, 1 and 2 the stack beside it. */
Koma panelFor(const Rect& rect, const int index) {
  const int left = rect.x + MARGIN;
  const int top = rect.y + SLANT;
  const int sideLeft = left + HERO_WIDTH + GUTTER;
  const int sideWidth = rect.width - MARGIN - sideLeft + rect.x;

  if (index == 0) return komaFrom(left, top, HERO_WIDTH, HERO_HEIGHT, 0);
  if (index == 1) return komaFrom(sideLeft, top, sideWidth, SIDE_HEIGHT, 1);
  return komaFrom(sideLeft, top + SIDE_HEIGHT + GUTTER, sideWidth, SIDE_HEIGHT, 2);
}

}  // namespace

void CollectionTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                          const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                          bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int bookCount = std::min(static_cast<int>(recentBooks.size()), PANEL_COUNT);
  const bool composing = !coverRendered;

  // Covers come off the SD card once and the composed block is cached; only the
  // selection bracket and the stats panel are redrawn per frame. The masking
  // here is per-row work, so recomposing on every selector move would be far
  // more expensive than the old grid was.
  if (!coverRendered) {
    for (int i = 0; i < bookCount; i++) {
      const Koma koma = panelFor(rect, i);
      drawKomaCover(renderer, recentBooks[i], koma);
      drawVolumeBadge(renderer, recentBooks[i], koma);
    }
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  if (composing || !shelfProgressLoaded) {
    for (int i = 0; i < static_cast<int>(shelfProgress.size()); i++) {
      shelfProgress[i] =
          i < bookCount ? XtcProgress::read(XtcProgress::cachePathFor(recentBooks[i].path)) : XtcProgress::Snapshot{};
    }
    shelfProgressLoaded = true;
  }

  if (selectorIndex >= 0 && selectorIndex < bookCount) {
    drawKomaSelection(renderer, panelFor(rect, selectorIndex));
  }

  drawStatsKoma(renderer, rect, recentBooks, selectorIndex);
}

void CollectionTheme::drawStatsKoma(const GfxRenderer& renderer, const Rect rect,
                                    const std::vector<RecentBook>& recentBooks, const int selectorIndex) const {
  const int slotTop = rect.y + SLANT + HERO_HEIGHT + GUTTER;

  // Cleared and redrawn every frame: it reports the selected volume, so unlike
  // the panels above it the copy in the stored buffer is stale the moment the
  // selector moves. Cleared past the slant so a leaning border leaves no trail.
  renderer.fillRect(rect.x, slotTop - SLANT, rect.width, STATS_HEIGHT + 2 * SLANT, false);

  const Koma koma = komaFrom(rect.x + MARGIN, slotTop, rect.width - 2 * MARGIN, STATS_HEIGHT, 3);
  drawKomaBorder(renderer, koma, BORDER);

  const int bookCount = std::min(static_cast<int>(recentBooks.size()), PANEL_COUNT);
  const bool hasSelection = selectorIndex >= 0 && selectorIndex < bookCount;

  // The selected volume, not a library total: the recent list handed to this
  // theme is already capped and collapsed to one entry per series, so any
  // "total" drawn from it would be a count of this block dressed up as one.
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
      snprintf(pageText, sizeof(pageText), "%lu / %lu", static_cast<unsigned long>(progress.page + 1),
               static_cast<unsigned long>(progress.pageCount));
      snprintf(percentText, sizeof(percentText), "%d%%", progress.percent());
    } else if (progress.valid) {
      snprintf(pageText, sizeof(pageText), "%lu", static_cast<unsigned long>(progress.page + 1));
    }
  }

  // Title across the top of the panel, stats in a row beneath it.
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int innerLeft = koma.minX() + BORDER + 6;
  const int innerWidth = koma.maxX() - innerLeft - BORDER - 6;
  const int innerTop = koma.minY() + SLANT + 4;

  if (hasSelection && innerWidth > 0) {
    const SeriesTitle::Parsed parsed = SeriesTitle::parse(recentBooks[selectorIndex].title);
    const std::string& name = parsed.series.empty() ? recentBooks[selectorIndex].title : parsed.series;
    renderer.drawText(SMALL_FONT_ID, innerLeft, innerTop,
                      renderer.truncatedText(SMALL_FONT_ID, name.c_str(), innerWidth).c_str(), true,
                      EpdFontFamily::BOLD);
  }

  // Three labelled figures. Short labels: they share the panel's width and clip
  // rather than wrap.
  const char* labels[3] = {tr(STR_STAT_VOLUME), tr(STR_STAT_PAGE), tr(STR_STAT_DONE)};
  const char* values[3] = {volumeText, pageText, percentText};
  const int cellWidth = innerWidth / 3;
  const int statsY = innerTop + lineHeight + 2;

  for (int i = 0; i < 3; i++) {
    const int cellX = innerLeft + i * cellWidth;
    char line[40];
    snprintf(line, sizeof(line), "%s %s", labels[i], values[i]);
    renderer.drawText(SMALL_FONT_ID, cellX, statsY, renderer.truncatedText(SMALL_FONT_ID, line, cellWidth - 4).c_str());
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
