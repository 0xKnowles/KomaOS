#include "XtcReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <string>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

XtcReaderMenuActivity::XtcReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                             const uint32_t currentPage, const uint32_t totalPages,
                                             const uint8_t currentOrientation, const bool hasChapters,
                                             const bool hasBookmarks, const bool currentPageBookmarked)
    : Activity("XtcReaderMenu", renderer, mappedInput),
      menuItems(buildMenuItems(hasChapters, hasBookmarks)),
      title(std::move(title)),
      pendingOrientation(currentOrientation),
      currentPage(currentPage),
      totalPages(totalPages),
      currentPageBookmarked(currentPageBookmarked) {}

std::vector<XtcReaderMenuActivity::MenuItem> XtcReaderMenuActivity::buildMenuItems(const bool hasChapters,
                                                                                   const bool hasBookmarks) {
  std::vector<MenuItem> items;
  items.reserve(8);
  // Quick jump first: in a 600-strip volume it is the entry reached for most.
  items.push_back({MenuAction::QUICK_JUMP, StrId::STR_GO_TO_PAGE});
  if (hasBookmarks) {
    items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  items.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
  // Only when the volume carries a TOC. Offering an entry that opens an empty
  // list is what made the old Confirm binding feel broken.
  if (hasChapters) {
    items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  }
  items.push_back({MenuAction::MANGA_SETTINGS, StrId::STR_CAT_MANGA});
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  return items;
}

void XtcReaderMenuActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void XtcReaderMenuActivity::onExit() { Activity::onExit(); }

void XtcReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  // Orientation still rides back on a cancel: it is applied live from the
  // popup, so discarding it here would silently revert what the user just saw.
  result.data = MenuResult{-1, pendingOrientation, 0};
  setResult(std::move(result));
  finish();
}

bool XtcReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void XtcReaderMenuActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    // The popup acts on button press; if that input closed it, the trailing
    // release must be swallowed below (Back would close the menu, Confirm
    // would re-activate the selected item).
    popupClosing = !optionPopup.isActive();
    return;
  }
  if (popupClosing) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      return;  // closing press still held
    }
    popupClosing = false;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      return;  // swallow the release that closed the popup
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return;
  }

  auto activateSelected = [this] {
    const auto selectedAction = menuItems[selectedIndex].action;
    if (selectedAction == MenuAction::ROTATE_SCREEN) {
      optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                       pendingOrientation, [this](int idx) {
                         pendingOrientation = static_cast<uint8_t>(idx);
                         requestUpdate();
                       });
      requestUpdate();
      return;
    }

    setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, 0});
    finish();
  };

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentTop =
      screen.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
  switch (handleListTouch(selectedIndex, static_cast<int>(menuItems.size()), contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      activateSelected();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }
}

void XtcReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str());

  // Page position rather than a percentage: "page 88 of 210" is what you act on
  // when hunting a scene; 42% is not.
  char progressLine[48];
  snprintf(progressLine, sizeof(progressLine), "%lu / %lu", static_cast<unsigned long>(currentPage + 1),
           static_cast<unsigned long>(totalPages));
  GUI.drawSubHeader(
      renderer,
      Rect{screen.x, screen.y + metrics.topPadding + metrics.headerHeight, screen.width, metrics.tabBarHeight},
      progressLine);

  const int contentTop =
      screen.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, menuItems.size(), selectedIndex,
      [this](int index) { return I18N.get(menuItems[index].labelId); }, nullptr, nullptr,
      [this](int index) -> std::string {
        const auto value = menuItems[index].action;
        if (value == MenuAction::ROTATE_SCREEN) {
          return I18N.get(orientationLabels[pendingOrientation]);
        }
        if (value == MenuAction::TOGGLE_BOOKMARK) {
          // Shows what the page IS, not what the action will do -- the label
          // already says "toggle", and a state that flips under you is worse.
          return I18N.get(currentPageBookmarked ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
        }
        return "";
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
