#pragma once
#include <functional>
#include <vector>

#include "./FileBrowserActivity.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  // Home can be entered while Back is still held (e.g. leaving Settings with
  // Back): ignore that stale release until a fresh press is seen here.
  bool backPressSeen = false;
  uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
  size_t coverBufferSize = 0;      // Bytes allocated to coverBuffer
  // Logical rect last passed to drawRecentBookCover. The cover snapshot only
  // needs to cover this region, not the entire framebuffer, so we cache the
  // tile instead of all 48 KB. Set in render() before the call.
  int coverRectX = 0;
  int coverRectY = 0;
  int coverRectW = 0;
  int coverRectH = 0;
  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;

  // The menu is a fixed list. OPDS used to be inserted only when a server was
  // configured, which made the row order depend on runtime state; it now always
  // sits last and opens the server list, where an empty store offers "add
  // server" rather than a dead end. KomaUI's background inks exactly MENU_COUNT
  // rows, so a list that changes length leaves an empty box on the page.
  static constexpr int MENU_COUNT = 5;
  static constexpr HomeMenuItem MENU_ORDER[MENU_COUNT] = {
      HomeMenuItem::BOOKS,         HomeMenuItem::MANGA,        HomeMenuItem::FILE_TRANSFER,
      HomeMenuItem::SETTINGS_MENU, HomeMenuItem::OPDS_BROWSER,
  };

  // Convert HomeMenuItem to menu index (used in onEnter)
  static int menuItemToIndex(HomeMenuItem item) {
    for (int i = 0; i < MENU_COUNT; i++) {
      if (MENU_ORDER[i] == item) return i;
    }
    return 0;
  }

  // Convert menu index to HomeMenuItem (used in loop)
  static HomeMenuItem indexToMenuItem(int idx) {
    if (idx < 0 || idx >= MENU_COUNT) return HomeMenuItem::NONE;
    return MENU_ORDER[idx];
  }
  void onSelectBook(const std::string& path);
  void onBooksOpen();
  void onMangaOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();

  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void loadRecentBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE)
      : Activity("Home", renderer, mappedInput), initialMenuItem(initialMenuItemValue) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
