#include "KomaUiTheme.h"

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
#include "images/KomaBackground.h"
#include "util/SeriesTitle.h"
#include "util/XtcProgress.h"

// File scope, not inside the anonymous namespace below: the member functions
// need the panel metrics too.
using namespace KomaUiMetrics;

namespace {

/** The background is stored pre-rotated in panel space; see SplashLayout.h for
 *  why the anchor is not (0,0). Same transform, same reasoning. */
constexpr int BLIT_X = -1;
constexpr int BLIT_Y = 0;
constexpr int BLIT_W = 800;
constexpr int BLIT_H = 480;

/**
 * Where a cover actually lands inside a slot.
 *
 * drawBitmap only ever scales *down* and always fits (it takes the smaller of
 * the width and height scales), so a cover can never be made to fill a slot of a
 * different aspect -- cropping does not help, because filling would need an
 * upscale drawBitmap will not do. The slack has to be laid out rather than
 * wished away, so this records the drawn box for the caller to place text
 * against instead of leaving a white margin down one side.
 */
struct CoverFit {
  int x, y, w, h;
};

/** Reproduces drawBitmap's fit arithmetic so the drawn box is known up front. */
CoverFit fitCover(const Panel& panel, const int coverW, const int coverH) {
  float scale = 1.0f;
  if (coverW > 0 && coverH > 0) {
    scale = std::min(static_cast<float>(panel.w) / static_cast<float>(coverW),
                     static_cast<float>(panel.h) / static_cast<float>(coverH));
    scale = std::min(scale, 1.0f);
  }
  const int drawnW = std::max(1, static_cast<int>(static_cast<float>(coverW) * scale));
  const int drawnH = std::max(1, static_cast<int>(static_cast<float>(coverH) * scale));
  return CoverFit{panel.x, panel.y + (panel.h - drawnH) / 2, drawnW, drawnH};
}

/**
 * Series name and volume in the space left over beside or under a cover.
 *
 * Page and percentage are deliberately absent: the stats koma already reports
 * them for the selected volume, and repeating them on every slot turns three
 * covers into three progress readouts.
 */
void drawPanelInfo(const GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int w,
                   const int h) {
  const SeriesTitle::Parsed parsed = SeriesTitle::parse(book.title);
  const std::string& name = parsed.series.empty() ? book.title : parsed.series;
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);

  const int maxNameLines = std::max(1, std::min(2, h / lineHeight - (parsed.hasVolume() ? 1 : 0)));
  const std::vector<std::string> nameLines = renderer.wrappedText(SMALL_FONT_ID, name.c_str(), w, maxNameLines);

  const int lines = static_cast<int>(nameLines.size()) + (parsed.hasVolume() ? 1 : 0);
  int textY = y + (h - lines * lineHeight) / 2;
  for (const std::string& line : nameLines) {
    renderer.drawText(SMALL_FONT_ID, x, textY, line.c_str(), true, EpdFontFamily::BOLD);
    textY += lineHeight;
  }
  if (parsed.hasVolume()) {
    renderer.drawText(SMALL_FONT_ID, x, textY, SeriesTitle::badge(parsed.volume).c_str(), true);
  }
}

/** Series name reversed out of a bar across the bottom of the art itself. */
void drawCoverLabel(const GfxRenderer& renderer, const RecentBook& book, const CoverFit& fit) {
  const SeriesTitle::Parsed parsed = SeriesTitle::parse(book.title);
  const std::string& name = parsed.series.empty() ? book.title : parsed.series;
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int barH = lineHeight + 4;
  const int barY = fit.y + fit.h - barH;

  // Reversed out of solid ink: cover art is unpredictable, and plain text over
  // it is illegible on a dark cover. The bar spans the art, not the panel --
  // running it into the margin is what made the slack look like a mistake.
  renderer.fillRect(fit.x, barY, fit.w, barH, true);
  const std::string shown = renderer.truncatedText(SMALL_FONT_ID, name.c_str(), fit.w - 2 * PAD);
  renderer.drawText(SMALL_FONT_ID, fit.x + PAD, barY + 2, shown.c_str(), false, EpdFontFamily::BOLD);
}

/** Below these the leftover space cannot hold readable text and is split instead. */
constexpr int MIN_INFO_WIDTH = 96;
constexpr int MIN_INFO_HEIGHT = 46;

enum class Caption { Under, Beside, OverArt };

/**
 * Anchors the art inside its slot and says where the caption goes.
 *
 * A slot is never the cover's aspect, so there is always slack in one direction.
 * Which one depends on the background: the current portrait slots are narrower
 * than a cover, so the slack is vertical and the caption sits under the art; the
 * previous landscape slots left it beside the art instead. Both cases are worth
 * keeping -- the alternative is a layout that has to be rewritten every time the
 * artwork is regenerated.
 */
Caption place(const Panel& panel, CoverFit& fit) {
  const int slackX = panel.w - fit.w;
  const int slackY = panel.h - fit.h;

  if (slackY - 2 * PAD >= MIN_INFO_HEIGHT && slackY >= slackX) {
    // Art to the top of the slot, caption in the band under it.
    fit.x = panel.x + slackX / 2;
    fit.y = panel.y;
    return Caption::Under;
  }
  if (slackX - 2 * PAD >= MIN_INFO_WIDTH) {
    return Caption::Beside;
  }
  // Nothing useful fits: centre the art so the margin is symmetric rather than
  // pooled on one side, and caption over the art itself.
  fit.x = panel.x + slackX / 2;
  return Caption::OverArt;
}

/** Draws the caption for a placed cover, wherever place() decided it goes. */
void drawCaption(const GfxRenderer& renderer, const RecentBook& book, const Panel& panel, const CoverFit& fit,
                 const Caption where) {
  switch (where) {
    case Caption::Under:
      drawPanelInfo(renderer, book, panel.x + PAD, fit.y + fit.h + PAD, panel.w - 2 * PAD,
                    panel.bottom() - (fit.y + fit.h) - 2 * PAD);
      break;
    case Caption::Beside:
      drawPanelInfo(renderer, book, fit.x + fit.w + PAD, panel.y, panel.w - fit.w - 2 * PAD, panel.h);
      break;
    case Caption::OverArt:
      drawCoverLabel(renderer, book, fit);
      break;
  }
}

/** Draws one shelf slot: art, then its caption wherever the slack allows. */
void drawPanel(const GfxRenderer& renderer, const RecentBook& book, const Panel& panel) {
  HalFile file;
  if (!book.coverBmpPath.empty() &&
      Storage.openFileForRead("HOME", UITheme::getCoverThumbPath(book.coverBmpPath, COVER_HEIGHT), file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
      CoverFit fit = fitCover(panel, bitmap.getWidth(), bitmap.getHeight());
      const Caption where = place(panel, fit);
      // maxWidth/maxHeight stay the full panel so drawBitmap derives the same
      // scale fitCover did; only the origin moves.
      renderer.drawBitmap(bitmap, fit.x, fit.y, panel.w, panel.h);
      drawCaption(renderer, book, panel, fit, where);
      return;
    }
  }

  // No thumbnail yet: the slot keeps its inked border from the background, so a
  // glyph plus the caption is enough to say it is filled. The placeholder box is
  // a nominal cover aspect, so an empty slot lays out like a filled one.
  constexpr int GLYPH = 32;
  CoverFit fit = fitCover(panel, panel.h * 7 / 10, panel.h);
  const Caption where = place(panel, fit);
  renderer.drawIcon(CoverIcon, fit.x + (fit.w - GLYPH) / 2, fit.y + (fit.h - GLYPH) / 2, GLYPH);
  // No art to reverse a bar out of, so the over-art caption is simply skipped.
  if (where != Caption::OverArt) {
    drawCaption(renderer, book, panel, fit, where);
  }
}

/** Marks the selected panel by inking a bar down its left edge. */
void drawPanelSelection(const GfxRenderer& renderer, const Panel& panel) {
  renderer.fillRect(panel.x, panel.y, 6, panel.h, true);
}

}  // namespace

void KomaUiTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                      const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                      bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  // Deliberately not using the cover cache. It snapshots one sub-rect, but this
  // theme paints the whole page including the area under the menu, so a partial
  // restore would leave the background cleared below the tile. Redrawing costs
  // one blit plus three thumbnails against an e-ink refresh that already takes
  // a second or more.
  coverRendered = true;
  coverBufferStored = false;
  bufferRestored = false;
  (void)storeCoverBuffer;
  (void)rect;

  renderer.drawImage(KomaBackground, BLIT_X, BLIT_Y, BLIT_W, BLIT_H);

  // No title on this page: the background has no banner box, and the artwork is
  // the identity. The header rule below still carries the motif onto every other
  // screen.
  if (recentBooks.empty()) {
    UITheme::drawCenteredText(renderer,
                              Rect{COVER[0].x, COVER[0].y, COVER[PANEL_COUNT - 1].right() - COVER[0].x, COVER[0].h},
                              SMALL_FONT_ID, COVER[0].y + COVER[0].h / 2, tr(STR_NO_RECENT_BOOKS), true);
    return;
  }

  const int bookCount = std::min(static_cast<int>(recentBooks.size()), PANEL_COUNT);

  if (!shelfProgressLoaded) {
    for (int i = 0; i < static_cast<int>(shelfProgress.size()); i++) {
      shelfProgress[i] =
          i < bookCount ? XtcProgress::read(XtcProgress::cachePathFor(recentBooks[i].path)) : XtcProgress::Snapshot{};
    }
    shelfProgressLoaded = true;
  }

  for (int i = 0; i < bookCount; i++) {
    drawPanel(renderer, recentBooks[i], COVER[i]);
    if (i == selectorIndex) {
      drawPanelSelection(renderer, COVER[i]);
    }
  }

  drawStatsKoma(renderer, rect, recentBooks, selectorIndex);
}

void KomaUiTheme::drawStatsKoma(const GfxRenderer& renderer, const Rect, const std::vector<RecentBook>& recentBooks,
                                const int selectorIndex) const {
  const int bookCount = std::min(static_cast<int>(recentBooks.size()), PANEL_COUNT);
  const bool hasSelection = selectorIndex >= 0 && selectorIndex < bookCount;

  // The selected volume, not a library total: the recent list handed to this
  // theme is already capped and collapsed to one entry per series, so a "total"
  // drawn from it would be a count of this page wearing a library's name.
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

  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int cellW = (STATS.w - 2 * PAD) / 3;
  const int labelY = STATS.y + (STATS.h - 2 * lineHeight) / 2;
  const char* labels[3] = {tr(STR_STAT_VOLUME), tr(STR_STAT_PAGE), tr(STR_STAT_DONE)};
  const char* vals[3] = {volumeText, pageText, percentText};

  for (int i = 0; i < 3; i++) {
    const Rect cell{STATS.x + PAD + i * cellW, STATS.y, cellW, STATS.h};
    UITheme::drawCenteredText(renderer, cell, SMALL_FONT_ID, labelY, labels[i], true);
    UITheme::drawCenteredText(renderer, cell, SMALL_FONT_ID, labelY + lineHeight, vals[i], true, EpdFontFamily::BOLD);
  }
}

void KomaUiTheme::drawButtonMenu(GfxRenderer& renderer, Rect, int buttonCount, int selectedIndex,
                                 const std::function<std::string(int index)>& buttonLabel,
                                 const std::function<UIIcon(int index)>& rowIcon) const {
  // Labels only, placed in the rows the background already inks. Not delegated
  // to Lyra: its rows draw their own fills and icons, which is exactly the
  // furniture this theme exists to get rid of.
  (void)rowIcon;

  const int rows = std::min(buttonCount, MENU_ROWS);
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);

  for (int i = 0; i < rows; i++) {
    const Panel& row = MENU[i];
    const bool selected = i == selectedIndex;
    // Selection inverts the whole row. On a page of inked panels a light fill
    // barely registers, and the panel border already supplies the outline.
    if (selected) {
      renderer.fillRect(row.x, row.y, row.w, row.h, true);
    }
    const std::string label = renderer.truncatedText(UI_12_FONT_ID, buttonLabel(i).c_str(), row.w - 2 * PAD);
    renderer.drawText(UI_12_FONT_ID, row.x + PAD * 2, row.y + (row.h - lineHeight) / 2, label.c_str(), !selected,
                      EpdFontFamily::BOLD);
  }
}

void KomaUiTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  LyraTheme::drawHeader(renderer, rect, title, subtitle);
  // Same edge treatment as a shelf ledge, so file browser, settings and reader
  // screens all read as part of the same theme rather than plain Lyra with a
  // different home screen.
  // Carries the page's inked-border look onto the screens that do not get the
  // koma background -- file browser, settings, the readers. A solid rule with a
  // dithered lip under it, which reads as depth on 1-bit where a second solid
  // line would just look thicker.
  constexpr int LEDGE = 2;
  const int ledgeY = rect.y + rect.height - LEDGE;
  renderer.fillRect(rect.x, ledgeY, rect.width, LEDGE, true);
  renderer.fillRectDither(rect.x, ledgeY + LEDGE, rect.width, 2, Color::LightGray);
}
