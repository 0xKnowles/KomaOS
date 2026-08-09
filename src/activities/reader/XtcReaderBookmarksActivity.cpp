#include "XtcReaderBookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/XtcBookmarkThumbnail.h"

namespace {

// Breathing room above and below the thumbnail within a row.
constexpr int ROW_PADDING = 6;
constexpr int ROW_HEIGHT = XtcBookmarkThumbnail::HEIGHT + 2 * ROW_PADDING;
constexpr int THUMB_MARGIN_X = 16;
constexpr int TEXT_MARGIN_X = 12;

// Unpacks a WIDTHxHEIGHT 1bpp thumbnail (bit 0 = ink, matching XTG's own
// polarity) at (x, y). `black` mirrors drawText's own convention: false draws
// the ink pixels as white, for a row whose background is already filled black.
void drawThumbnail(const GfxRenderer& renderer, const int x, const int y, const uint8_t* buffer, const bool black) {
  for (int row = 0; row < XtcBookmarkThumbnail::HEIGHT; row++) {
    for (int col = 0; col < XtcBookmarkThumbnail::WIDTH; col++) {
      const size_t byteIdx = static_cast<size_t>(row) * XtcBookmarkThumbnail::ROW_BYTES + col / 8;
      const bool ink = ((buffer[byteIdx] >> (7 - (col % 8))) & 1) == 0;
      if (ink) {
        renderer.drawPixel(x + col, y + row, black);
      }
    }
  }
}

}  // namespace

int XtcReaderBookmarksActivity::getPageItems() const {
  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int availableHeight = screen.y + screen.height - contentTop - metrics.verticalSpacing;
  return std::max(1, availableHeight / ROW_HEIGHT);
}

void XtcReaderBookmarksActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void XtcReaderBookmarksActivity::onExit() { Activity::onExit(); }

void XtcReaderBookmarksActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (pages.empty()) {
    return;  // Nothing to navigate; Back is the only way out.
  }

  const int totalItems = static_cast<int>(pages.size());
  const int pageItems = getPageItems();

  auto activate = [this] {
    setResult(PageResult{pages[selectedIndex]});
    finish();
  };

  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  // Rows are taller than the shared list widget's, so touch hit-testing goes
  // through the lower-level rowTouch() with our own row height rather than
  // handleListTouch() (which assumes the widget's fixed row step). Same
  // approach as XtcReaderChapterSelectionActivity.
  int row = -1;
  const auto touch = mappedInput.rowTouch(row, contentTop, ROW_HEIGHT, pageItems, screen.x, screen.x + screen.width);
  if (touch != MappedInputManager::RowTouch::None) {
    const int touched = selectedIndex / pageItems * pageItems + row;
    if (touched >= 0 && touched < totalItems) {
      if (touch == MappedInputManager::RowTouch::Down) {
        if (selectedIndex != touched) {
          selectedIndex = touched;
          requestUpdate();
        }
      } else {
        selectedIndex = touched;
        activate();
      }
      return;
    }
  }

  buttonNavigator.onNext([this, totalItems] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, totalItems] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate();
  }
}

void XtcReaderBookmarksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_BOOKMARKS));

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.y + screen.height - contentTop - metrics.verticalSpacing;

  if (pages.empty()) {
    // Reachable by touch even though the menu hides the entry when empty.
    UITheme::drawCenteredText(renderer, screen, UI_12_FONT_ID, contentTop + contentHeight / 2, tr(STR_NO_BOOKMARKS));
  } else {
    const int pageItems = getPageItems();
    const int totalItems = static_cast<int>(pages.size());
    const int pageStart = selectedIndex / pageItems * pageItems;

    // Reused per row rather than held across the activity's lifetime: it is
    // only 300 bytes and only touched while a result is being drawn, the same
    // reasoning XtcReaderActivity::renderPage applies to its (much larger)
    // page buffer.
    auto thumbBuffer = makeUniqueNoThrow<uint8_t[]>(XtcBookmarkThumbnail::SIZE);

    for (int i = pageStart; i < totalItems && i < pageStart + pageItems; i++) {
      const int rowY = contentTop + (i - pageStart) * ROW_HEIGHT;
      const bool selected = i == selectedIndex;
      if (selected) {
        renderer.fillRect(screen.x, rowY, screen.width - 1, ROW_HEIGHT - 2);
      }

      const int thumbX = screen.x + THUMB_MARGIN_X;
      const int thumbY = rowY + ROW_PADDING;
      const bool hasThumb =
          thumbBuffer && XtcBookmarkThumbnail::load(cachePath, pages[i], thumbBuffer.get(), XtcBookmarkThumbnail::SIZE);
      if (hasThumb) {
        drawThumbnail(renderer, thumbX, thumbY, thumbBuffer.get(), !selected);
      } else {
        // No cached thumbnail (bookmark predates this feature, or generation
        // failed) -- an outline placeholder beats a hole in the row.
        renderer.drawRect(thumbX, thumbY, XtcBookmarkThumbnail::WIDTH, XtcBookmarkThumbnail::HEIGHT, !selected);
      }

      const int textX = thumbX + XtcBookmarkThumbnail::WIDTH + TEXT_MARGIN_X;
      const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

      char label[32];
      snprintf(label, sizeof(label), "%s %lu", I18N.get(StrId::STR_PAGE_LABEL),
               static_cast<unsigned long>(pages[i] + 1));
      renderer.drawText(UI_10_FONT_ID, textX, thumbY + 2, label, !selected, EpdFontFamily::BOLD);

      // Percentage through the volume, so a list of bare page numbers still
      // conveys roughly where each bookmark sits.
      char pct[16];
      snprintf(pct, sizeof(pct), "%lu%%",
               pageCount > 0 ? static_cast<unsigned long>((pages[i] + 1) * 100 / pageCount) : 0UL);
      renderer.drawText(UI_10_FONT_ID, textX, thumbY + 2 + lineHeight + 4, pct, !selected);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
