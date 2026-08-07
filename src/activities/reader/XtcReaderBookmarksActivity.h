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
 * would be a second SD round trip for data already in hand.
 */
class XtcReaderBookmarksActivity final : public Activity {
 public:
  explicit XtcReaderBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                      std::vector<uint32_t> pages, uint32_t pageCount)
      : Activity("XtcReaderBookmarks", renderer, mappedInput), pages(std::move(pages)), pageCount(pageCount) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<uint32_t> pages;
  uint32_t pageCount = 0;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
};
