#pragma once
#include <I18n.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

/**
 * The manga reader's menu, opened with Confirm.
 *
 * Before this existed, Confirm called straight into chapter selection, which
 * returns immediately when a volume has no TOC -- so on a converted CBZ without
 * chapters the button did nothing at all.
 *
 * Deliberately shorter than the EPUB menu: no font, dictionary, footnote or
 * text-settings entries, because none of them mean anything for a pre-rendered
 * page. What is left is what you actually reach for mid-volume.
 */
class XtcReaderMenuActivity final : public Activity {
 public:
  enum class MenuAction {
    QUICK_JUMP,
    BOOKMARKS,
    TOGGLE_BOOKMARK,
    SELECT_CHAPTER,
    MANGA_SETTINGS,
    TOGGLE_VIEW_MODE,
    ROTATE_SCREEN,
    SCREENSHOT,
    GO_HOME,
  };

  explicit XtcReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                 uint32_t currentPage, uint32_t totalPages, uint8_t currentOrientation,
                                 bool hasChapters, bool hasBookmarks, bool currentPageBookmarked);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static std::vector<MenuItem> buildMenuItems(bool hasChapters, bool hasBookmarks);
  void closeCancelled();

  const std::vector<MenuItem> menuItems;

  int selectedIndex = 0;

  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  // True while the button press that closed the popup is still held; its release
  // must not fall through to the menu's own Back/Confirm handlers.
  bool popupClosing = false;

  std::string title;
  uint8_t pendingOrientation = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  uint32_t currentPage = 0;
  uint32_t totalPages = 0;
  bool currentPageBookmarked = false;
};
