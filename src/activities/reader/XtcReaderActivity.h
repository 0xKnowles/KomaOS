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
  // Set when a long-press fired its bound function, so the release that follows
  // the hold does not also open the menu. Same guard the EPUB reader uses.
  bool ignoreNextConfirmRelease = false;
  // True for the duration of a hold-to-peek: Confirm has been held past the
  // threshold with longPressMenuFunction == LP_MENU_VIEW_MODE. render() shows
  // the reassembled full page while this is set and reverts to the strip on
  // release, without moving currentPage either way.
  bool peekingFullPage = false;
  // Next-book suggestion menu for the End-of-Book screen
  EndOfBookOptions endOfBookOptions;

  // Full-view lead-in correction in force for this volume. Remembered per book
  // in progress.bin, because the correction is a property of the file's front
  // matter: a shelf holds volumes needing different values, and one global
  // number means re-entering it on every switch. SETTINGS.mangaSliceOffset is
  // the manual override -- see loadProgress for how the two resolve.
  int sliceOffset = 0;

  enum class StatusBarOverlayPosition { Bottom, Top };
  struct StatusBarInfo {
    int currentPage;
    int pageCount;
    std::string title;
  };

  void renderPage();
  // Full view mode: three strips reassembled into one page. Separate from
  // renderPage because it is a different pipeline -- crop, downscale, re-dither
  // -- not a variation on blitting one strip 1:1.
  bool renderFullPage();
  /** Strips per turn: 3 in Full view, 1 in Split. */
  uint32_t pageStep() const;
  /**
   * Strips before the first grouped page, each of which stands alone.
   *
   * The file's own count corrected by SETTINGS.mangaSliceOffset, for volumes
   * whose front matter is not what the encoder recorded.
   */
  uint32_t leadingStripCount() const;
  /** First strip of the page group `currentPage` falls in. */
  uint32_t pageGroupStart() const;
  bool fullViewActive() const;
  // Whether the current volume's geometry allows reassembling a full page at
  // all, independent of the persistent Full/Split setting. fullViewActive()
  // is this plus the setting check; a hold-to-peek needs the geometry check
  // alone, since it does not touch the setting.
  bool canReassembleFullPage() const;
  // Opens the manga menu (Confirm). Replaces the old direct call into chapter
  // selection, which no-opped on any volume without a TOC.
  void openReaderMenu();
  void onReaderMenuConfirm(int action);
  void toggleBookmarkForCurrentPage();
  // Shared by the manga menu entry and the long-press binding.
  void toggleViewMode();
  // Opens chapter selection when the book has chapters (short-press Confirm); no-op otherwise
  void openChapterSelection();
  void renderStatusBarOverlay(StatusBarOverlayPosition position) const;
  // Split view's bar: the title and a progress hairline against the panel edge,
  // and nothing else. Its own function rather than more flags through
  // GUI.drawStatusBar, which is shared with the EPUB and TXT readers and has no
  // reason to grow a manga-only layout.
  void renderSplitStatusBar(StatusBarOverlayPosition position) const;
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
