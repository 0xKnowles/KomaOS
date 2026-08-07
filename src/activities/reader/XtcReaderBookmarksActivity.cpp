#include "XtcReaderBookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <string>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

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

  const int count = static_cast<int>(pages.size());

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  auto activate = [this] {
    setResult(PageResult{pages[selectedIndex]});
    finish();
  };

  switch (handleListTouch(selectedIndex, count, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      activate();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  buttonNavigator.onNext([this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
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
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  if (pages.empty()) {
    // Reachable by touch even though the menu hides the entry when empty.
    UITheme::drawCenteredText(renderer, screen, UI_12_FONT_ID, contentTop + contentHeight / 2, tr(STR_NO_BOOKMARKS));
  } else {
    GUI.drawList(
        renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, pages.size(), selectedIndex,
        [this](int index) {
          char label[32];
          snprintf(label, sizeof(label), "%s %lu", I18N.get(StrId::STR_PAGE_LABEL),
                   static_cast<unsigned long>(pages[index] + 1));
          return std::string(label);
        },
        nullptr, nullptr,
        [this](int index) -> std::string {
          // Percentage through the volume, so a list of bare page numbers still
          // conveys roughly where each bookmark sits.
          char pct[8];
          snprintf(pct, sizeof(pct), "%lu%%",
                   pageCount > 0 ? static_cast<unsigned long>((pages[index] + 1) * 100 / pageCount) : 0UL);
          return pct;
        },
        true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
