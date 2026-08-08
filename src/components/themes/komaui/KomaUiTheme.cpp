#include "KomaUiTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
 * Where a cover actually lands inside a panel.
 *
 * drawBitmap only ever scales *down* and always fits (it takes the smaller of
 * the width and height scales), so a portrait cover in a landscape panel is
 * limited by the panel's height and cannot be made to fill the width -- cropping
 * does not help, because filling would need an upscale drawBitmap will not do.
 * The slack therefore has to be laid out rather than wished away: this records
 * the drawn box so the caller can put the leftover width to work instead of
 * leaving it as a white margin down one side.
 */
struct CoverFit {
  int x, y, w, h;
  /** Panel width left over beside the art, before padding. */
  int slack;
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
  return CoverFit{panel.x, panel.y + (panel.h - drawnH) / 2, drawnW, drawnH, panel.w - drawnW};
}

/**
 * Series name, volume and progress in the width left beside a cover.
 *
 * Only drawn when the slack is wide enough to be worth reading; below that the
 * cover is centred instead, which turns a lopsided margin into a symmetric one.
 */
void drawPanelInfo(const GfxRenderer& renderer, const RecentBook& book, const XtcProgress::Snapshot& progress,
                   const int x, const int y, const int w, const int h) {
  const SeriesTitle::Parsed parsed = SeriesTitle::parse(book.title);
  const std::string& name = parsed.series.empty() ? book.title : parsed.series;
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);

  const std::vector<std::string> nameLines = renderer.wrappedText(SMALL_FONT_ID, name.c_str(), w, 2);

  // Volume and progress share one line; both are short and a stacked column of
  // three near-empty lines reads as padding rather than information.
  char detail[24] = "";
  if (parsed.hasVolume()) {
    snprintf(detail, sizeof(detail), "%s", SeriesTitle::badge(parsed.volume).c_str());
  }
  if (progress.hasPageCount()) {
    const size_t used = strlen(detail);
    snprintf(detail + used, sizeof(detail) - used, "%s%d%%", used > 0 ? "  " : "", progress.percent());
  }

  const int lines = static_cast<int>(nameLines.size()) + (detail[0] ? 1 : 0);
  int textY = y + (h - lines * lineHeight) / 2;
  for (const std::string& line : nameLines) {
    renderer.drawText(SMALL_FONT_ID, x, textY, line.c_str(), true, EpdFontFamily::BOLD);
    textY += lineHeight;
  }
  if (detail[0]) {
    renderer.drawText(SMALL_FONT_ID, x, textY, detail, true);
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

/**
 * Draws one shelf panel: art, then either an info column or a centred cover.
 *
 * The background's panels are wider than a manga cover is, so every slot has
 * horizontal slack. Wide slots (the two beside the hero) spend it on metadata;
 * narrow ones split it evenly and caption the art instead.
 */
void drawPanel(const GfxRenderer& renderer, const RecentBook& book, const XtcProgress::Snapshot& progress,
               const Panel& panel) {
  /** Below this the leftover width cannot hold a readable word, so it is split. */
  constexpr int MIN_INFO_WIDTH = 96;

  HalFile file;
  if (!book.coverBmpPath.empty() &&
      Storage.openFileForRead("HOME", UITheme::getCoverThumbPath(book.coverBmpPath, COVER_HEIGHT), file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
      CoverFit fit = fitCover(panel, bitmap.getWidth(), bitmap.getHeight());
      const bool withInfo = fit.slack - 2 * PAD >= MIN_INFO_WIDTH;
      if (!withInfo) {
        fit.x += fit.slack / 2;
      }
      // maxWidth/maxHeight stay the full panel so drawBitmap derives the same
      // scale fitCover did; only the origin moves.
      renderer.drawBitmap(bitmap, fit.x, fit.y, panel.w, panel.h);
      if (withInfo) {
        drawPanelInfo(renderer, book, progress, fit.x + fit.w + PAD, panel.y, fit.slack - 2 * PAD, panel.h);
      } else {
        drawCoverLabel(renderer, book, fit);
      }
      return;
    }
  }

  // No thumbnail yet: the panel keeps its inked border from the background, so
  // a glyph plus the metadata is enough to say the slot is filled.
  constexpr int GLYPH = 32;
  const CoverFit fit = fitCover(panel, panel.h * 7 / 10, panel.h);
  renderer.drawIcon(CoverIcon, fit.x + (fit.w - GLYPH) / 2, panel.y + (panel.h - GLYPH) / 2, GLYPH);
  if (fit.slack - 2 * PAD >= MIN_INFO_WIDTH) {
    drawPanelInfo(renderer, book, progress, fit.x + fit.w + PAD, panel.y, fit.slack - 2 * PAD, panel.h);
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

  // The header text is painted over by the blit, so the banner panel gets it
  // back. This is also why the theme owns the title rather than drawHeader:
  // that runs on every screen, not just home.
  UITheme::drawCenteredText(renderer, Rect{BANNER.x, BANNER.y, BANNER.w, BANNER.h}, UI_12_FONT_ID,
                            BANNER.y + (BANNER.h - renderer.getLineHeight(UI_12_FONT_ID)) / 2, tr(STR_KOMAOS), true,
                            EpdFontFamily::BOLD);

  if (recentBooks.empty()) {
    UITheme::drawCenteredText(renderer, Rect{HERO.x, HERO.y, SIDE_TOP.right() - HERO.x, HERO.h}, SMALL_FONT_ID,
                              HERO.y + HERO.h / 2, tr(STR_NO_RECENT_BOOKS), true);
    return;
  }

  const Panel slots[PANEL_COUNT] = {HERO, SIDE_TOP, SIDE_BOTTOM};
  const int bookCount = std::min(static_cast<int>(recentBooks.size()), PANEL_COUNT);

  if (!shelfProgressLoaded) {
    for (int i = 0; i < static_cast<int>(shelfProgress.size()); i++) {
      shelfProgress[i] =
          i < bookCount ? XtcProgress::read(XtcProgress::cachePathFor(recentBooks[i].path)) : XtcProgress::Snapshot{};
    }
    shelfProgressLoaded = true;
  }

  for (int i = 0; i < bookCount; i++) {
    drawPanel(renderer, recentBooks[i], shelfProgress[i], slots[i]);
    if (i == selectorIndex) {
      drawPanelSelection(renderer, slots[i]);
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
