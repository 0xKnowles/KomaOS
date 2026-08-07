/**
 * XtcReaderActivity.h
 *
 * XTC ebook reader activity for KomaOS
 * Displays pre-rendered XTC pages on e-ink display
 */

#pragma once

#include <Xtc.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "EndOfBookOptions.h"
#include "activities/Activity.h"

class XtcReaderActivity final : public Activity {
  std::shared_ptr<Xtc> xtc;

  uint32_t currentPage = 0;
  int pagesUntilFullRefresh = 0;
  // Resolved once in onEnter() from the setting and the file header, rather
  // than per input event: neither input can change while the book is open.
  bool readingRightToLeft = false;
  // Held in memory for the whole session: the menu needs to know whether the
  // current page is bookmarked on every open, and re-reading a 258-byte file
  // for that is a pointless SD round trip.
  std::vector<uint32_t> bookmarkedPages;
  // Next-book suggestion menu for the End-of-Book screen
  EndOfBookOptions endOfBookOptions;

  enum class StatusBarOverlayPosition { Bottom, Top };
  struct StatusBarInfo {
    int currentPage;
    int pageCount;
    std::string title;
  };

  void renderPage();
  // Opens the manga menu (Confirm). Replaces the old direct call into chapter
  // selection, which no-opped on any volume without a TOC.
  void openReaderMenu();
  void onReaderMenuConfirm(int action);
  void toggleBookmarkForCurrentPage();
  // Opens chapter selection when the book has chapters (short-press Confirm); no-op otherwise
  void openChapterSelection();
  void renderStatusBarOverlay(StatusBarOverlayPosition position) const;
  // The right-edge column. Its own function rather than a third case in
  // renderStatusBarOverlay: that one delegates to GUI.drawStatusBar, which is
  // horizontal by construction and shared with the EPUB and TXT readers.
  void renderSideStatusBar() const;
  StatusBarInfo getStatusBarInfo() const;
  void saveProgress() const;
  void loadProgress();

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Xtc> xtc,
                             int initialRefreshCountdown)
      : Activity("XtcReader", renderer, mappedInput),
        xtc(std::move(xtc)),
        pagesUntilFullRefresh(initialRefreshCountdown) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool handleForcedRefresh() override {
    {
      RenderLock lock(*this);
      pagesUntilFullRefresh = 1;
    }
    requestUpdate();
    return true;
  }
  ScreenshotInfo getScreenshotInfo() const override;
};
