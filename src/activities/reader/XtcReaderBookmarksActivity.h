#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * List of bookmarked pages in the open volume, returning the chosen page.
 *
 * Takes the already-loaded page list rather than a path: the reader keeps it in
 * memory to render the menu's bookmarked/not state, so re-reading the file here
 * would be a second SD round trip for data already in hand. cachePath is kept
 * separately to load each row's thumbnail on demand (util/XtcBookmarkThumbnail.h)
 * -- a 300-byte SD read per visible row, not the whole list at once.
 *
 * Rows are custom-drawn rather than going through the shared list widget
 * (components/UITheme.h GUI.drawList): that widget has no bitmap slot, and a
 * thumbnail is the entire point of this screen (see EndOfBookOptions and
 * XtcReaderChapterSelectionActivity for the two existing styles this could
 * have followed -- this one needs the latter's custom row layout).
 */
class XtcReaderBookmarksActivity final : public Activity {
 public:
  explicit XtcReaderBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                      std::vector<uint32_t> pages, uint32_t pageCount, std::string cachePath)
      : Activity("XtcReaderBookmarks", renderer, mappedInput),
        pages(std::move(pages)),
        pageCount(pageCount),
        cachePath(std::move(cachePath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<uint32_t> pages;
  uint32_t pageCount = 0;
  std::string cachePath;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  int getPageItems() const;
};
