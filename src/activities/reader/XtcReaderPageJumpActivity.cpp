#include "XtcReaderPageJumpActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void XtcReaderPageJumpActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void XtcReaderPageJumpActivity::onExit() { Activity::onExit(); }

void XtcReaderPageJumpActivity::adjustPage(const int delta) {
  if (pageCount <= 0) {
    return;
  }
  // Clamp rather than wrap: holding the side button to reach the end of a
  // volume should stop there, not roll back to the cover.
  const int next = std::clamp(page + delta, 0, pageCount - 1);
  if (next != page) {
    page = next;
    requestUpdate();
  }
}

void XtcReaderPageJumpActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    setResult(PageResult{static_cast<uint32_t>(page)});
    finish();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Right) {
    adjustPage(LARGE_STEP);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Left) {
    adjustPage(-LARGE_STEP);
    return;
  }

  // Continuous repeat matters here more than anywhere else in the UI: without
  // it, crossing a 600-strip volume is 600 discrete presses.
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustPage(-SMALL_STEP); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustPage(SMALL_STEP); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] { adjustPage(-LARGE_STEP); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] { adjustPage(LARGE_STEP); });
}

void XtcReaderPageJumpActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_GO_TO_PAGE));

  // The page number, large and central: it is the whole point of the screen.
  char pageLine[32];
  snprintf(pageLine, sizeof(pageLine), "%d / %d", page + 1, pageCount);
  UITheme::drawCenteredText(renderer, screen, UI_12_FONT_ID, screen.y + screen.height / 2 - 40, pageLine, true,
                            EpdFontFamily::BOLD);

  // Progress bar, so the position is legible at a glance as well as numerically.
  const int barWidth = screen.width - 2 * metrics.contentSidePadding;
  const int barHeight = 12;
  const int barX = screen.x + metrics.contentSidePadding;
  const int barY = screen.y + screen.height / 2;
  renderer.drawRect(barX, barY, barWidth, barHeight, true);
  if (pageCount > 1) {
    const int filled = (page * (barWidth - 4)) / (pageCount - 1);
    renderer.fillRect(barX + 2, barY + 2, filled, barHeight - 4, true);
  }

  char hint[64];
  snprintf(hint, sizeof(hint), "%s %d    %s %d", tr(STR_STEP_HINT_FRONT), SMALL_STEP, tr(STR_STEP_HINT_SIDE),
           LARGE_STEP);
  UITheme::drawCenteredText(renderer, screen, SMALL_FONT_ID, barY + barHeight + 20, hint);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
