/**
 * XtcReaderActivity.cpp
 *
 * XTC ebook reader activity implementation
 * Displays pre-rendered XTC pages on e-ink display
 */

#include "XtcReaderActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc/ReadingDirection.h>
#include <Xtc/XthPixels.h>

#include <algorithm>
#include <utility>

#include "KomaSettings.h"
#include "KomaState.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "XtcReaderBookmarksActivity.h"
#include "XtcReaderChapterSelectionActivity.h"
#include "XtcReaderMenuActivity.h"
#include "XtcReaderPageJumpActivity.h"
#include "activities/settings/LibraryHealthCheckActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/FullPageLayout.h"
#include "util/ScreenshotUtil.h"
#include "util/XtcBookmarkThumbnail.h"
#include "util/XtcBookmarks.h"
#include "util/XtcProgress.h"

namespace {

// Geometry of the right-edge status column (XTC_STATUS_BAR_RIGHT).
/** Gap between the column's rule and its contents, on both sides. */
constexpr int SIDE_BAR_PADDING = 5;
/** Vertical gap between stacked elements in the column. */
constexpr int SIDE_BAR_GAP = 4;
/** Narrow enough that a two-digit count still gets a readable column. */
constexpr int SIDE_BAR_MIN_WIDTH = 26;
/** Ceiling, so a pathological page count cannot eat the artwork. */
constexpr int SIDE_BAR_MAX_WIDTH = 52;
/** Thickness of the vertical progress bar. */
constexpr int SIDE_BAR_BAR_WIDTH = 7;
/** Square marking a bookmarked page. */
constexpr int SIDE_BAR_BOOKMARK_SIZE = 7;
/** Thickness of the Split-view progress hairline, flush to the panel edge. */
constexpr int SPLIT_BAR_HEIGHT = 3;
/** Gap between that hairline and the title. */
constexpr int SPLIT_BAR_GAP = 3;
/** Side inset for the Split-view title, so it never touches the panel edge. */
constexpr int SPLIT_TEXT_MARGIN = 8;

// Ink accumulated since the last full (scrubbing) refresh, in bits, that forces
// an early scrub on the turn after it is crossed -- see render(). Set to three
// full 480x800 black pages' worth of bits, on the reasoning that a near-solid
// page ghosts far worse than the sparse pages the page-count cadence is tuned
// around, so three of them should not go unscrubbed.
//
// UNVERIFIED: this number has not been tuned on hardware, and the right value
// is a property of the panel, not of the arithmetic. Watch the panel for
// residual ghosting (threshold too high) or for scrubbing on nearly every turn
// (too low) and adjust.
constexpr uint32_t INK_REFRESH_THRESHOLD = 3u * 480u * 800u;

// Ink (black) pixel count currently in the framebuffer. drawPixel CLEARS a bit
// for ink and SETS it for white (GfxRenderer.cpp:543-547), so this popcounts the
// complement rather than the buffer itself. One pass over 48,000 bytes with a
// hardware popcount per byte, once per page turn -- against a turn that already
// costs a full-page decode and a 1-2s panel refresh.
uint32_t countInkPixels(const GfxRenderer& renderer) {
  const uint8_t* buffer = renderer.getFrameBuffer();
  const size_t size = renderer.getBufferSize();
  if (!buffer) {
    return 0;
  }
  uint32_t ink = 0;
  for (size_t i = 0; i < size; i++) {
    ink += static_cast<uint32_t>(__builtin_popcount(static_cast<uint8_t>(~buffer[i])));
  }
  return ink;
}

}  // namespace

void XtcReaderActivity::onEnter() {
  Activity::onEnter();

  if (!xtc) {
    return;
  }

  xtc->setupCacheDir();

  readingRightToLeft = xtc::isRightToLeft(SETTINGS.mangaReadingDirection, xtc->getReadDirection());
  LOG_DBG("XTR", "Reading direction: %s (setting=%u, header=%u)", readingRightToLeft ? "RTL" : "LTR",
          SETTINGS.mangaReadingDirection, xtc->getReadDirection());

  // Load saved progress
  loadProgress();
  XtcBookmarks::load(xtc->getCachePath(), bookmarkedPages);

  // Save current XTC as last opened book and add to recent books
  APP_STATE.openEpubPath = xtc->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(xtc->getPath(), xtc->getTitle(), xtc->getAuthor(), xtc->getThumbBmpPath());

  // Trigger first update
  requestUpdate();
}

void XtcReaderActivity::onExit() {
  Activity::onExit();

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  xtc.reset();
}

void XtcReaderActivity::openChapterSelection() {
  if (xtc && xtc->hasChapters() && !xtc->getChapters().empty()) {
    startActivityForResult(std::make_unique<XtcReaderChapterSelectionActivity>(renderer, mappedInput, xtc, currentPage),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               currentPage = std::get<PageResult>(result.data).page;
                             }
                           });
  }
}

void XtcReaderActivity::openReaderMenu() {
  if (!xtc) {
    return;
  }
  startActivityForResult(std::make_unique<XtcReaderMenuActivity>(
                             renderer, mappedInput, xtc->getTitle(), currentPage, xtc->getPageCount(),
                             SETTINGS.orientation, xtc->hasChapters() && !xtc->getChapters().empty(),
                             !bookmarkedPages.empty(), XtcBookmarks::contains(bookmarkedPages, currentPage)),
                         [this](const ActivityResult& result) {
                           const auto& menu = std::get<MenuResult>(result.data);
                           // Orientation is applied even on cancel: the popup changes it live, so
                           // discarding it here would revert what the user just saw happen.
                           if (menu.orientation != SETTINGS.orientation) {
                             SETTINGS.orientation = menu.orientation;
                             SETTINGS.saveToFile();
                             // No reflow to redo, unlike the EPUB reader: an XTC page is a fixed
                             // image, so applying the transform and re-rendering is the whole job.
                             ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
                           }
                           if (!result.isCancelled) {
                             onReaderMenuConfirm(menu.action);
                           }
                           requestUpdate();
                         });
}

void XtcReaderActivity::toggleViewMode() {
  SETTINGS.mangaViewMode = SETTINGS.mangaViewMode == KomaSettings::MANGA_VIEW_MODE::MANGA_VIEW_FULL
                               ? KomaSettings::MANGA_VIEW_MODE::MANGA_VIEW_SPLIT
                               : KomaSettings::MANGA_VIEW_MODE::MANGA_VIEW_FULL;
  SETTINGS.saveToFile();
  // Snap to the group's first strip. Switching to Full from the middle of a
  // page would otherwise reassemble starting at whichever strip was on screen,
  // splicing in the next page.
  currentPage = pageGroupStart();
}

void XtcReaderActivity::toggleBookmarkForCurrentPage() {
  // Sampled before the toggle because toggle()'s own false return is ambiguous:
  // it means both "removed" and "refused, list full". Comparing before against
  // after resolves it, and says whether a thumbnail needs generating or clearing.
  const bool wasBookmarked = XtcBookmarks::contains(bookmarkedPages, currentPage);
  const bool nowBookmarked = XtcBookmarks::toggle(bookmarkedPages, currentPage);
  if (!XtcBookmarks::save(xtc->getCachePath(), bookmarkedPages)) {
    LOG_ERR("XTR", "Failed to save bookmarks for page %lu", currentPage);
  }
  if (nowBookmarked && !wasBookmarked) {
    // Best-effort: the bookmarks list falls back to a placeholder when the
    // thumbnail is missing, so a failure here is not worth undoing the bookmark.
    XtcBookmarkThumbnail::generate(xtc->getCachePath(), currentPage, *xtc);
  } else if (wasBookmarked && !nowBookmarked) {
    XtcBookmarkThumbnail::remove(xtc->getCachePath(), currentPage);
  }
}

void XtcReaderActivity::onReaderMenuConfirm(const int action) {
  switch (static_cast<XtcReaderMenuActivity::MenuAction>(action)) {
    case XtcReaderMenuActivity::MenuAction::QUICK_JUMP:
      startActivityForResult(
          std::make_unique<XtcReaderPageJumpActivity>(renderer, mappedInput, currentPage, xtc->getPageCount()),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              currentPage = std::get<PageResult>(result.data).page;
            }
            requestUpdate();
          });
      break;

    case XtcReaderMenuActivity::MenuAction::BOOKMARKS:
      startActivityForResult(std::make_unique<XtcReaderBookmarksActivity>(renderer, mappedInput, bookmarkedPages,
                                                                          xtc->getPageCount(), xtc->getCachePath()),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) {
                                 currentPage = std::get<PageResult>(result.data).page;
                               }
                               requestUpdate();
                             });
      break;

    case XtcReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK:
      toggleBookmarkForCurrentPage();
      break;

    case XtcReaderMenuActivity::MenuAction::SELECT_CHAPTER:
      openChapterSelection();
      break;

    case XtcReaderMenuActivity::MenuAction::MANGA_SETTINGS:
      // Opens the settings screen on the Manga tab, so the options that affect
      // what is on screen are one step away rather than five.
      startActivityForResult(std::make_unique<SettingsActivity>(renderer, mappedInput, SettingsActivity::MANGA_TAB),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;

    case XtcReaderMenuActivity::MenuAction::TOGGLE_VIEW_MODE:
      toggleViewMode();
      break;

    case XtcReaderMenuActivity::MenuAction::SCREENSHOT:
      ScreenshotUtil::takeScreenshot(renderer);
      break;

    case XtcReaderMenuActivity::MenuAction::LIBRARY_HEALTH_CHECK:
      startActivityForResult(std::make_unique<LibraryHealthCheckActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;

    case XtcReaderMenuActivity::MenuAction::GO_HOME:
      onGoHome();
      break;

    case XtcReaderMenuActivity::MenuAction::ROTATE_SCREEN:
      // Applied by the menu handler above, before this switch runs.
      break;
  }
}

void XtcReaderActivity::loop() {
  if (!xtc) {
    return;
  }

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);

  const bool atEndOfBook = currentPage >= xtc->getPageCount();

  // While the end screen suggestion menu is showing it owns Confirm/Back/navigation
  // input. Anything it doesn't handle (e.g. long-press Back to the file browser) falls
  // through to the regular handlers below; page turns are absorbed by the end-of-book
  // block.
  if (atEndOfBook && endOfBookOptions.menuActive()) {
    std::string openPath;
    switch (endOfBookOptions.handleMenuInput(mappedInput, &openPath)) {
      case EndOfBookOptions::Action::OpenBook:
        activityManager.goToReader(openPath);
        return;
      case EndOfBookOptions::Action::GoHome:
        onGoHome();
        return;
      case EndOfBookOptions::Action::LastPage:
        currentPage = xtc->getPageCount() > 0 ? xtc->getPageCount() - 1 : 0;
        requestUpdate();
        return;
      case EndOfBookOptions::Action::Redraw:
        requestUpdate();
        return;
      case EndOfBookOptions::Action::None:
        break;
    }
  }

  // Open the manga menu. Previously this called openChapterSelection() directly,
  // which returns immediately when a volume has no TOC -- so on a converted CBZ
  // without chapters, Confirm did nothing at all.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || ReaderUtils::isTouchMenuGesture(mappedInput)) {
    // A long-press that already fired its bound function suppresses this
    // release, so the hold does not also open the menu.
    if (ignoreNextConfirmRelease) {
      ignoreNextConfirmRelease = false;
      // Ends a hold-to-peek: back to the strip currentPage was already on.
      // currentPage itself was never touched, so this is a pure re-render.
      if (peekingFullPage) {
        peekingFullPage = false;
        requestUpdate();
      }
      return;
    }
    openReaderMenu();
    return;
  }

  // Long-press Confirm runs the user-selected function. Only VIEW_MODE applies
  // here -- bookmarking already has its own menu entry, and KOReader sync and
  // the dictionary are text-reader features with nothing to act on in a paged
  // image. Anything else falls through and Confirm behaves as before.
  //
  // This is a peek, not a toggle: holding past the threshold shows the
  // reassembled full page; releasing (above) reverts to the strip. currentPage
  // is never written by either edge, so there is nothing to restore.
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      SETTINGS.longPressMenuFunction == KomaSettings::LP_MENU_VIEW_MODE &&
      mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS && !ignoreNextConfirmRelease) {
    peekingFullPage = true;
    // Latched before the release arrives, so the hold cannot also open the menu
    // and cannot re-fire while the button stays down.
    ignoreNextConfirmRelease = true;
    requestUpdate();
    return;
  }

  // No null check on xtc: loop() returns at the top when it is null, and the
  // early return added for the menu made that provable to cppcheck.
  if (ReaderUtils::handleBackNavigation(mappedInput, activityManager, xtc->getPath().c_str(),
                                        {this, [](void* ctx) { static_cast<XtcReaderActivity*>(ctx)->onGoHome(); }})) {
    return;
  }

  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  // Manga is read towards the spine, so "forward" is the control on the left.
  // Swapping here rather than at each use keeps the swap in one place and means
  // everything downstream -- skip-ahead, the end-of-book screen, progress --
  // can keep reading "next" as "further into the book". Covers buttons, tilt
  // and touch zones alike, since all three have already been folded in above.
  if (readingRightToLeft) {
    std::swap(prevTriggered, nextTriggered);
  }

  // At end of the book with no suggestion menu, forward button goes home and back
  // button returns to last page
  if (currentPage >= xtc->getPageCount()) {
    if (endOfBookOptions.menuActive()) {
      // Selection movement was handled above; absorb leftover page-turn triggers so
      // e.g. "previous" at the top of the list doesn't jump back into the book
      return;
    }
    if (nextTriggered) {
      onGoHome();
    } else {
      currentPage = xtc->getPageCount() - 1;
      requestUpdate();
    }
    return;
  }

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool skipPages =
      !fromTilt && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP && heldMs > ReaderUtils::SKIP_HOLD_MS;
  // A turn moves whole pages, which is three strips in Full view. A long-press
  // skip is already expressed in strips, so it is left alone -- rounding it to
  // the group would change what the setting means.
  const int skipAmount = skipPages ? SETTINGS.getMangaSkipPages() : static_cast<int>(pageStep());

  if (prevTriggered) {
    // Step from the group's first strip, not from wherever inside it the
    // reader happens to be, so a turn back in Full view lands on a page
    // boundary rather than drifting off one.
    const uint32_t from = fullViewActive() ? pageGroupStart() : currentPage;
    currentPage = from >= static_cast<uint32_t>(skipAmount) ? from - skipAmount : 0;
    requestUpdate();
  } else if (nextTriggered) {
    currentPage = (fullViewActive() ? pageGroupStart() : currentPage) + skipAmount;
    if (currentPage >= xtc->getPageCount()) {
      currentPage = xtc->getPageCount();  // Allow showing "End of book"
    }
    requestUpdate();
  }
}

void XtcReaderActivity::render(RenderLock&&) {
  if (!xtc) {
    return;
  }

  // Bounds check
  if (currentPage >= xtc->getPageCount()) {
    // Show end of book screen. Sole load site: runs on the render task (serialized by
    // RenderLock); the main task only reads the suggestions once the flag is published.
    endOfBookOptions.loadOnce(xtc->getPath());
    renderer.clearScreen();
    endOfBookOptions.render(renderer, mappedInput);
    renderer.displayBuffer();
    return;
  }

  // Captured before dispatch: renderPage()'s 2-bit branch and
  // ReaderUtils::displayWithRefreshCycle both scrub exactly when
  // pagesUntilFullRefresh is at or below this, and then reset it -- so reading it
  // here predicts whether the render below scrubs, without duplicating the rule.
  const bool scrubbingThisTurn = pagesUntilFullRefresh <= 1;

  // Full view falls back rather than failing the turn: a layout or allocation
  // that did not work out should still leave the reader on a readable strip.
  // A peek in progress asks for the same reassembly as the persistent Full
  // setting, without requiring the setting itself -- canReassembleFullPage()
  // is the geometry half of fullViewActive() with the setting check dropped.
  const bool showFullPage = peekingFullPage ? canReassembleFullPage() : fullViewActive();
  if (!showFullPage || !renderFullPage()) {
    renderPage();
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, false, SETTINGS.getMangaRefreshFrequency());
  }

  if (SETTINGS.mangaInkAwareRefresh) {
    if (scrubbingThisTurn) {
      // A scrub just ran -- from the page-count cadence, or from an ink crossing
      // latched on an earlier turn. Ghosting is cleared, so the total restarts.
      inkSinceLastFullRefresh = 0;
    } else {
      inkSinceLastFullRefresh += countInkPixels(renderer);
      if (inkSinceLastFullRefresh >= INK_REFRESH_THRESHOLD) {
        // This page has already gone to the panel in the ordinary (non-scrub)
        // mode above, so the earliest a scrub can act on the crossing is the
        // next turn, which re-reads pagesUntilFullRefresh at the top of render().
        pagesUntilFullRefresh = 1;
        inkSinceLastFullRefresh = 0;
      }
    }
  }

  saveProgress();
}

XtcReaderActivity::StatusBarInfo XtcReaderActivity::getStatusBarInfo() const {
  const auto sb = SETTINGS.statusBarSpec();
  const int bookPageCount = static_cast<int>(xtc->getPageCount());
  const int bookPage = static_cast<int>(currentPage) + 1;
  std::string title = sb.titleMode == KomaSettings::STATUS_BAR_TITLE::BOOK_TITLE ? xtc->getTitle() : "";

  // Reading direction is otherwise invisible: turning RTL on gives no feedback
  // beyond the controls behaving differently, which is indistinguishable from
  // having pressed the wrong button. Prefixing the title is enough to confirm
  // it took, and costs no status-bar layout change.
  const char* directionMark = readingRightToLeft ? "\xE2\x86\x90 " : "";

  if (!xtc->hasChapters()) {
    return StatusBarInfo{bookPage, bookPageCount, directionMark + title};
  }

  const auto& chapters = xtc->getChapters();
  const auto chapterIt = std::find_if(chapters.begin(), chapters.end(), [this](const xtc::ChapterInfo& chapter) {
    return currentPage >= chapter.startPage && currentPage <= chapter.endPage;
  });

  if (chapterIt == chapters.end() || chapterIt->endPage < chapterIt->startPage) {
    return StatusBarInfo{bookPage, bookPageCount, directionMark + title};
  }

  if (sb.titleMode == KomaSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = chapterIt->name.empty() ? tr(STR_UNNAMED) : chapterIt->name;
  }

  return StatusBarInfo{static_cast<int>(currentPage - chapterIt->startPage) + 1,
                       static_cast<int>(chapterIt->endPage - chapterIt->startPage) + 1, directionMark + title};
}

void XtcReaderActivity::renderStatusBarOverlay(const StatusBarOverlayPosition position) const {
  const auto sb = SETTINGS.statusBarSpec();
  const bool drawBottom = sb.xtcMode == KomaSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_BOTTOM &&
                          position == StatusBarOverlayPosition::Bottom;
  const bool drawTop =
      sb.xtcMode == KomaSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP && position == StatusBarOverlayPosition::Top;
  if (!drawBottom && !drawTop) {
    return;
  }

  // Split view gets a deliberately minimal bar: the strip is the page, drawn
  // edge to edge, and counts, battery and percentages all eat into it for
  // information the reader can get from the menu. Title plus a progress hairline
  // is what is worth the height. Full view keeps the full bar -- a reassembled
  // page is already letterboxed, so the room is there.
  if (!fullViewActive()) {
    renderSplitStatusBar(position);
    return;
  }

  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight <= 0) {
    return;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);

  int clearY;
  int paddingBottom = 0;
  if (position == StatusBarOverlayPosition::Bottom) {
    clearY = renderer.getScreenHeight() - orientedMarginBottom - statusBarHeight - 4;
    if (clearY < 0) {
      clearY = 0;
    }
  } else {
    clearY = orientedMarginTop;
    paddingBottom = renderer.getScreenHeight() - statusBarHeight - orientedMarginBottom - orientedMarginTop - 4;
  }
  const int clearHeight = position == StatusBarOverlayPosition::Bottom
                              ? renderer.getScreenHeight() - orientedMarginBottom - clearY
                              : statusBarHeight + 4;
  if (clearHeight > 0) {
    renderer.fillRect(0, clearY, renderer.getScreenWidth(), clearHeight, false);
  }

  const int pageCount = static_cast<int>(xtc->getPageCount());
  const int displayPage = static_cast<int>(currentPage) + 1;
  const float progress = pageCount > 0 ? (static_cast<float>(displayPage) * 100.0f) / pageCount : 0.0f;
  const auto pageInfo = getStatusBarInfo();
  // Bottom turns its glyphs a quarter turn; Top does not. FlipNzb's split modes
  // store each strip already rotated (comic.ts, Region::rotate) so it fills the
  // panel when the device is turned, but the bar is drawn by the firmware in
  // unrotated panel space -- which is why the artwork reads upright and the
  // status text reads sideways. Turning the glyphs squares the two up without
  // moving the bar. Top is left upright so an unrotated volume still has a mode
  // that reads correctly.
  const bool turnGlyphs = sb.xtcMode == KomaSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_BOTTOM;
  GUI.drawStatusBar(renderer, progress, pageInfo.currentPage, pageInfo.pageCount, pageInfo.title, paddingBottom, 0,
                    true, false, false, turnGlyphs);
}

void XtcReaderActivity::renderSplitStatusBar(const StatusBarOverlayPosition position) const {
  const auto sb = SETTINGS.statusBarSpec();
  // Bottom turns its glyphs a quarter turn to match pre-rotated artwork; Top
  // does not. Whichever applies has to be used for measuring as well as
  // drawing, which is the bug that let the title run off the panel: it was
  // measured turned and truncated against the horizontal width.
  const bool turnGlyphs = position == StatusBarOverlayPosition::Bottom;
  const auto textExtent = [this, turnGlyphs](const char* text) {
    return turnGlyphs ? renderer.getTurnedTextExtent(SMALL_FONT_ID, text) : renderer.getTextWidth(SMALL_FONT_ID, text);
  };

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);

  // Against the very outer edge of the panel, not the oriented margin: the page
  // is drawn edge to edge, so a bar inset by the margin floats in a band of
  // cleared white instead of reading as a rule along the screen's edge.
  const int barY = position == StatusBarOverlayPosition::Bottom ? screenHeight - SPLIT_BAR_HEIGHT : 0;
  const int textY = position == StatusBarOverlayPosition::Bottom ? barY - SPLIT_BAR_GAP - lineHeight
                                                                 : SPLIT_BAR_HEIGHT + SPLIT_BAR_GAP;

  const int clearY = position == StatusBarOverlayPosition::Bottom ? textY - SPLIT_BAR_GAP : 0;
  const int clearHeight =
      position == StatusBarOverlayPosition::Bottom ? screenHeight - clearY : textY + lineHeight + SPLIT_BAR_GAP;
  renderer.fillRect(0, std::max(0, clearY), screenWidth, clearHeight, false);

  std::string title = getStatusBarInfo().title;
  if (!title.empty()) {
    // Truncate against the metric the glyphs will actually be drawn with.
    // getTurnedTextExtent charges one line height per character, so a turned
    // title fits far fewer characters than its horizontal width suggests.
    const int available = screenWidth - 2 * SPLIT_TEXT_MARGIN;
    if (textExtent(title.c_str()) > available && available > 0) {
      if (turnGlyphs) {
        // One line height per glyph means the budget is a character count. Cut
        // on a UTF-8 boundary so a multi-byte character is not split in half.
        const size_t maxChars = lineHeight > 0 ? static_cast<size_t>(available / lineHeight) : 0;
        size_t chars = 0;
        size_t bytes = 0;
        while (bytes < title.size() && chars + 1 < maxChars) {
          const auto lead = static_cast<uint8_t>(title[bytes]);
          size_t width = 1;
          if ((lead & 0xF8) == 0xF0) {
            width = 4;
          } else if ((lead & 0xF0) == 0xE0) {
            width = 3;
          } else if ((lead & 0xE0) == 0xC0) {
            width = 2;
          }
          if (bytes + width > title.size()) break;
          bytes += width;
          chars++;
        }
        // resize-then-append rather than substr: assigning a prefix of a string
        // back to itself builds a whole temporary to do it, which is both a
        // cppcheck defect (uselessCallsSubstr) and a pointless allocation here.
        title.resize(bytes);
        title += "\xE2\x80\xA6";
      } else {
        title = renderer.truncatedText(SMALL_FONT_ID, title.c_str(), available);
      }
    }
    const int titleX = std::max(SPLIT_TEXT_MARGIN, (screenWidth - textExtent(title.c_str())) / 2);
    if (turnGlyphs) {
      renderer.drawTextGlyphsTurned(SMALL_FONT_ID, titleX, textY, title.c_str());
    } else {
      renderer.drawText(SMALL_FONT_ID, titleX, textY, title.c_str());
    }
  }

  // Always book progress, never chapter: the bar is the only progress readout
  // left in this mode, so it should mean the same thing in every volume.
  const int pageCount = static_cast<int>(xtc->getPageCount());
  const int bookPercent = pageCount > 0 ? (static_cast<int>(currentPage) + 1) * 100 / pageCount : 0;
  if (sb.showsProgressBar()) {
    renderer.fillRect(0, barY, screenWidth * std::clamp(bookPercent, 0, 100) / 100, SPLIT_BAR_HEIGHT, true);
  }
}

void XtcReaderActivity::renderSideStatusBar() const {
  // Text is drawn horizontally in the oriented frame, so in landscape -- which
  // is how manga is read -- a narrow column on the right holds ordinary
  // left-to-right numbers stacked down it. No rotated glyphs are involved, and
  // none are available: GfxRenderer::drawImage already cannot rotate bits.
  const auto sb = SETTINGS.statusBarSpec();

  const int pageCount = static_cast<int>(xtc->getPageCount());
  const int displayPage = static_cast<int>(currentPage) + 1;
  const int bookPercent = pageCount > 0 ? (displayPage * 100) / pageCount : 0;
  // Chapter-relative when the volume has a TOC, book-relative otherwise; the
  // horizontal bar reads the same numbers from the same place.
  const StatusBarInfo info = getStatusBarInfo();
  const int chapterPercent = info.pageCount > 0 ? (info.currentPage * 100) / info.pageCount : 0;

  // Every lane is opt-in from the same settings the horizontal bar reads, so
  // turning the page count off means off in either orientation.
  char pageText[12] = {0};
  char totalText[12] = {0};
  char percentText[8] = {0};
  char batteryText[8] = {0};
  if (sb.showChapterPageCount) {
    snprintf(pageText, sizeof(pageText), "%d", info.currentPage);
    snprintf(totalText, sizeof(totalText), "%d", info.pageCount);
  }
  if (sb.showBookProgressPercent) {
    snprintf(percentText, sizeof(percentText), "%d%%", bookPercent);
  }
  if (sb.showBattery) {
    snprintf(batteryText, sizeof(batteryText), "%u%%", powerManager.getBatteryPercentage());
  }

  const bool bookmarked = XtcBookmarks::contains(bookmarkedPages, currentPage);
  const bool hasBar = sb.showsProgressBar();
  if (pageText[0] == '\0' && percentText[0] == '\0' && batteryText[0] == '\0' && !hasBar && !bookmarked) {
    return;  // Every lane is off; leave the page its full width.
  }

  int marginTop, marginRight, marginBottom, marginLeft;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);

  // Width follows the widest thing that will actually be drawn -- a 4-digit
  // volume needs more column than a 2-digit one, and hardcoding either wastes
  // page width or clips the count.
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const char* const measured[] = {pageText, totalText, percentText, batteryText};
  int textWidth = 0;
  for (const char* text : measured) {
    if (text[0] != '\0') {
      textWidth = std::max(textWidth, renderer.getTextWidth(SMALL_FONT_ID, text));
    }
  }
  const int stripWidth = std::clamp(textWidth + 2 * SIDE_BAR_PADDING, SIDE_BAR_MIN_WIDTH, SIDE_BAR_MAX_WIDTH);

  const int stripX = renderer.getScreenWidth() - marginRight - stripWidth;
  const int stripTop = marginTop;
  const int stripBottom = renderer.getScreenHeight() - marginBottom;
  if (stripX <= 0 || stripBottom - stripTop <= 0) {
    return;
  }

  // Clear to the panel edge, not just the strip: the page underneath is drawn
  // edge to edge and any of it left showing beside the column reads as a
  // rendering fault rather than a margin.
  renderer.fillRect(stripX, 0, renderer.getScreenWidth() - stripX, renderer.getScreenHeight(), false);
  // Rule separating the column from the page, so the numbers do not look like
  // part of the artwork.
  renderer.fillRect(stripX, stripTop, 1, stripBottom - stripTop, true);

  const Rect column{stripX + SIDE_BAR_PADDING, stripTop, stripWidth - 2 * SIDE_BAR_PADDING, stripBottom - stripTop};
  int y = stripTop + SIDE_BAR_PADDING;

  if (readingRightToLeft) {
    // The horizontal bar shows reading direction by prefixing the title, and
    // this column has no title lane -- without the arrow here, switching to the
    // side bar would silently drop the only confirmation that RTL took.
    UITheme::drawCenteredText(renderer, column, SMALL_FONT_ID, y, "\xE2\x86\x90", true, EpdFontFamily::BOLD);
    y += lineHeight + SIDE_BAR_GAP;
  }

  if (pageText[0] != '\0') {
    UITheme::drawCenteredText(renderer, column, SMALL_FONT_ID, y, pageText, true, EpdFontFamily::BOLD);
    y += lineHeight;
    // Hairline standing in for the "/" of "88 / 210", which has nowhere to go
    // in a column this narrow.
    renderer.fillRect(column.x + 2, y + 1, column.width - 4, 1, true);
    y += SIDE_BAR_GAP;
    UITheme::drawCenteredText(renderer, column, SMALL_FONT_ID, y, totalText, true);
    y += lineHeight + SIDE_BAR_GAP;
  }

  if (percentText[0] != '\0') {
    UITheme::drawCenteredText(renderer, column, SMALL_FONT_ID, y, percentText, true);
    y += lineHeight + SIDE_BAR_GAP;
  }

  // Bottom-anchored cluster, filled upwards, so the bar between it and the
  // numbers takes whatever height is left rather than the bar dictating where
  // the battery ends up.
  int bottom = stripBottom - SIDE_BAR_PADDING;
  if (batteryText[0] != '\0') {
    bottom -= lineHeight;
    UITheme::drawCenteredText(renderer, column, SMALL_FONT_ID, bottom, batteryText, true);
    bottom -= SIDE_BAR_GAP;
  }
  if (bookmarked) {
    bottom -= SIDE_BAR_BOOKMARK_SIZE;
    renderer.fillRect(column.x + (column.width - SIDE_BAR_BOOKMARK_SIZE) / 2, bottom, SIDE_BAR_BOOKMARK_SIZE,
                      SIDE_BAR_BOOKMARK_SIZE, true);
    bottom -= SIDE_BAR_GAP;
  }

  if (hasBar && bottom - y > SIDE_BAR_GAP) {
    const int barX = column.x + (column.width - SIDE_BAR_BAR_WIDTH) / 2;
    const int barHeight = bottom - y;
    renderer.drawRect(barX, y, SIDE_BAR_BAR_WIDTH, barHeight, true);

    const int progress =
        sb.progressBarMode == KomaSettings::STATUS_BAR_PROGRESS_BAR::BOOK_PROGRESS ? bookPercent : chapterPercent;
    // Filled top-down: further down the column is further through the volume,
    // which is the direction the numbers above it already read in.
    const int fillHeight = (barHeight - 2) * std::clamp(progress, 0, 100) / 100;
    if (fillHeight > 0) {
      renderer.fillRect(barX + 1, y + 1, SIDE_BAR_BAR_WIDTH - 2, fillHeight, true);
    }
  }
}

bool XtcReaderActivity::fullViewActive() const {
  // Full view only means anything for a volume that was actually split. A
  // nosplit encode is one strip per page, and reassembling three of those would
  // stack three unrelated pages.
  if (SETTINGS.mangaViewMode != KomaSettings::MANGA_VIEW_MODE::MANGA_VIEW_FULL) return false;
  return canReassembleFullPage();
}

bool XtcReaderActivity::canReassembleFullPage() const {
  // A nosplit encode is one page per strip; stacking three would splice
  // unrelated pages together.
  if (xtc->getPageCount() < FullPageLayout::STRIPS_PER_PAGE) return false;

  // When the file says how many strips a page takes, believe it rather than
  // assuming three. overlapSegments emits more for an unusually tall page, and
  // reassembling those three at a time would cut pages in the wrong places.
  const xtc::XtcSplitGeometry geometry = xtc->getSplitGeometry();
  if (geometry.valid && geometry.stripsPerPage != FullPageLayout::STRIPS_PER_PAGE) return false;
  return true;
}

uint32_t XtcReaderActivity::pageStep() const { return fullViewActive() ? FullPageLayout::STRIPS_PER_PAGE : 1; }

uint32_t XtcReaderActivity::leadingStripCount() const {
  // The cover is encoded nosplit, so it is one strip on its own and every group
  // after it is offset by that. Grouping from zero instead splices the cover
  // onto the next page's first two strips, and every page from there is a mix
  // of two -- the symptom that sent this back for a second look.
  //
  // The file's own count is right for most volumes, but not all: front matter
  // the encoder did not record as a lead-in -- an extra full-page plate after
  // the cover -- leaves the grouping one strip out of step for the whole book.
  // The setting shifts this so a reader can correct that volume without a
  // re-export. Clamped at zero: a negative lead-in has no meaning, and the
  // count is compared against unsigned page indices.
  const int shifted = static_cast<int>(xtc->getSplitGeometry().leadingStrips) + sliceOffset;
  return static_cast<uint32_t>(std::max(0, shifted));
}

uint32_t XtcReaderActivity::pageGroupStart() const {
  const uint32_t step = pageStep();
  if (step <= 1) {
    return currentPage;
  }

  const uint32_t leading = leadingStripCount();
  if (currentPage < leading) {
    return currentPage;  // Still in the lead-in; each of those stands alone.
  }
  return leading + ((currentPage - leading) / step) * step;
}

bool XtcReaderActivity::renderFullPage() {
  const uint16_t stripWidth = xtc->getPageWidth();
  const uint16_t stripHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  // Prefer the encoder's own figure over the user's setting. The setting exists
  // because the overlap follows from the original page's aspect ratio, which
  // conversion consumes -- once a file records it, guessing is strictly worse.
  const xtc::XtcSplitGeometry geometry = xtc->getSplitGeometry();
  const int overlapPercent = geometry.valid ? geometry.overlapPercent() : SETTINGS.getMangaFullOverlapPercent();

  const FullPageLayout::Layout layout = FullPageLayout::plan(stripWidth, stripHeight, renderer.getScreenWidth(),
                                                             renderer.getScreenHeight(), overlapPercent);
  if (!layout.valid) {
    LOG_ERR("XTR", "Full view layout failed for %ux%u strip", stripWidth, stripHeight);
    return false;
  }

  const size_t stripBufferSize = (bitDepth == 2) ? xtc::XthPage::payloadSizeFor(stripWidth, stripHeight)
                                                 : xtc::xtgPayloadSizeFor(stripWidth, stripHeight);
  // One strip at a time, not three: each is drawn into the framebuffer and
  // then dropped, so peak use is one strip buffer plus the framebuffer rather
  // than the ~288KB three XTH strips would need at once.
  auto stripOwner = makeUniqueNoThrow<uint8_t[]>(stripBufferSize);
  if (!stripOwner) {
    LOG_ERR("XTR", "OOM: full-view strip buffer %lu bytes", (unsigned long)stripBufferSize);
    return false;
  }
  uint8_t* strip = stripOwner.get();

  const size_t xtgRowBytes = (stripWidth + 7) / 8;
  // Built once over the reused strip buffer: XthPage only stores the payload
  // pointer and the plane offsets, so it stays valid as each strip is loaded
  // into the same allocation.
  const xtc::XthPage xth{strip, stripWidth, stripHeight};

  renderer.clearScreen();

  const uint32_t firstStrip = pageGroupStart();
  // A lead-in strip is a whole page by itself; reassembling from it would pull
  // in the next page's strips. Returning false here falls back to the ordinary
  // single-strip render, which is what a lead-in page wants anyway.
  if (firstStrip < leadingStripCount()) {
    return false;
  }
  for (int i = 0; i < FullPageLayout::STRIPS_PER_PAGE; i++) {
    const FullPageLayout::StripPlacement& placement = layout.strips[i];
    if (!placement.contributes()) continue;

    const uint32_t stripIndex = firstStrip + static_cast<uint32_t>(i);
    if (stripIndex >= xtc->getPageCount()) break;
    if (xtc->loadPage(stripIndex, strip, stripBufferSize) == 0) {
      LOG_ERR("XTR", "Full view: failed to load strip %lu", (unsigned long)stripIndex);
      continue;  // Leave that band blank rather than abandoning the whole page.
    }

    // Ink level 0..3 at a stored strip pixel, whatever the bit depth.
    // Page rows count DOWN stored X: the duplicated lead sits at high X, so
    // cropping low X would leave the overlap in and drop unique art instead.
    // This now agrees with the file's rotationQuarterTurns, which records the
    // single clockwise turn the encoder applied -- the earlier both-axis mirror
    // was an over-correction that happened to fix the rotation while
    // introducing a left-to-right flip.
    // Only the page-direction axis is mirrored. The encoder stores each strip
    // with one clockwise quarter turn (comic.ts, sharp rotate(90)), which puts
    // page rows DOWN stored X but leaves page columns running UP stored Y.
    // Mirroring Y as well flipped the page left-to-right, which is what made
    // the text read backwards on hardware.
    const auto mirrorX = [stripWidth](const int x) { return stripWidth - 1 - std::clamp(x, 0, stripWidth - 1); };

    const auto levelAt = [&](const int sx, const int sy) -> int {
      if (bitDepth == 2) {
        return xth.levelInColumn(xth.columnBase(static_cast<uint16_t>(sx)), static_cast<uint16_t>(sy));
      }
      const size_t byte = static_cast<size_t>(sy) * xtgRowBytes + static_cast<size_t>(sx) / 8;
      // XTG: bit set means white, so invert into an ink level.
      return ((strip[byte] >> (7 - (sx % 8))) & 1) ? 3 : 0;
    };

    const int rowsPerDest = FullPageLayout::sourceRowsPerDestRow(placement);
    const int colsPerDest = std::max(1, stripHeight / std::max(1, layout.dstWidth));

    for (int dy = 0; dy < placement.dstCount; dy++) {
      // The strip is stored turned, so the page's vertical axis is its X.
      const int srcX = FullPageLayout::sourceRowFor(placement, dy);
      const int panelY = placement.dstStart + dy;

      for (int dx = 0; dx < layout.dstWidth; dx++) {
        const int srcY = static_cast<int>(static_cast<int64_t>(dx) * stripHeight / layout.dstWidth);

        // Box-average the 1-bit source over this output pixel's footprint. The
        // dither pattern carries local intensity, so averaging recovers an
        // approximate grey that can be re-dithered at the lower resolution;
        // point-sampling it instead produces moire on every screentone.
        int total = 0;
        int samples = 0;
        for (int ox = 0; ox < rowsPerDest; ox++) {
          const int sx = srcX + ox;
          if (sx >= placement.srcStart + placement.srcCount || sx >= stripWidth) break;
          for (int oy = 0; oy < colsPerDest; oy++) {
            const int sy = srcY + oy;
            if (sy >= stripHeight) break;
            total += levelAt(mirrorX(sx), std::clamp(sy, 0, stripHeight - 1));
            samples++;
          }
        }
        if (samples == 0) continue;

        // Ordered dither: no error-diffusion row buffer and no serpentine
        // state, which keeps this a pure function of position on a page turn
        // that is already the slowest thing the reader does.
        static constexpr uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        const int mean = (total * 16) / (samples * 3);  // 0..16 white-ness
        if (mean <= BAYER[panelY & 3][dx & 3]) {
          renderer.drawPixel(layout.dstLeft + dx, panelY, true);
        }
      }
    }
  }

  return true;
}

void XtcReaderActivity::renderPage() {
  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  // Buffer size for one page. Shares its definition with the parser that reads
  // the page (XthPixels.h) so the two can never disagree about how many bytes a
  // page occupies.
  const size_t pageBufferSize = (bitDepth == 2) ? xtc::XthPage::payloadSizeFor(pageWidth, pageHeight)
                                                : xtc::xtgPayloadSizeFor(pageWidth, pageHeight);

  // Allocate page buffer. Kept per-render rather than hoisted into onEnter():
  // this is the largest allocation the reader makes (96,000 bytes for a
  // 480x800 XTH page) and the chapter-selection activity runs nested inside
  // this one, so holding it across the whole session would shrink the heap
  // available to that child. Same-size alloc/free cycles reuse the same block,
  // so the churn is not itself a fragmentation source.
  auto pageBufferOwner = makeUniqueNoThrow<uint8_t[]>(pageBufferSize);
  if (!pageBufferOwner) {
    LOG_ERR("XTR", "OOM: page buffer %lu bytes", pageBufferSize);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_MEMORY_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }
  uint8_t* pageBuffer = pageBufferOwner.get();

  // Load page data
  size_t bytesRead = xtc->loadPage(currentPage, pageBuffer, pageBufferSize);
  if (bytesRead == 0) {
    LOG_ERR("XTR", "Failed to load page %lu: bufferSize=%lu bitDepth=%u error=%s", currentPage, pageBufferSize,
            bitDepth, xtc::errorToString(xtc->getLastError()));
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Clear screen first
  renderer.clearScreen();

  // Copy page bitmap using GfxRenderer's drawPixel
  // XTC/XTCH pages are pre-rendered with status bar included, so render full page
  const uint16_t maxSrcY = pageHeight;

  if (bitDepth == 2) {
    // XTH 2-bit mode: Two bit planes, column-major order
    // - Columns scanned right to left (x = width-1 down to 0)
    // - 8 vertical pixels per byte (MSB = topmost pixel in group)
    // - First plane: Bit1, Second plane: Bit2
    // - Pixel value = (bit1 << 1) | bit2
    // - Grayscale: 0=White, 1=Dark Grey, 2=Light Grey, 3=Black

    const xtc::XthPage page(pageBuffer, pageWidth, pageHeight);

    // Walks the page in XTH's own column-major order so the column base offset
    // -- the only multiply in the addressing -- is computed once per column
    // rather than once per pixel. Every pass below visits all width*height
    // pixels, so that removes 384k multiplies per pass at 480x800, on a
    // single-issue 160MHz core.
    //
    // `levelMask` selects which levels the pass acts on (see xtc::XthMask);
    // `ink` is the state passed to drawPixel for matching pixels.
    const auto plotWhere = [&](const uint8_t levelMask, const bool ink) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        const size_t colBase = page.columnBase(x);
        for (uint16_t y = 0; y < pageHeight; y++) {
          if (xtc::XthPage::matches(levelMask, page.levelInColumn(colBase, y))) {
            renderer.drawPixel(x, y, ink);
          }
        }
      }
    };

    // Optimized grayscale rendering without storeBwBuffer (saves 48KB peak memory)
    // Flow: BW display → LSB/MSB passes → grayscale display → re-render BW for next frame

#if LOG_LEVEL >= 2
    // Diagnostic only. Guarded because it is a full width*height pass whose
    // sole consumer is the LOG_DBG below, which compiles away in release --
    // leaving the loop to run 384k iterations per page turn for nothing.
    {
      uint32_t pixelCounts[4] = {0, 0, 0, 0};
      for (uint16_t x = 0; x < pageWidth; x++) {
        const size_t colBase = page.columnBase(x);
        for (uint16_t y = 0; y < pageHeight; y++) {
          pixelCounts[page.levelInColumn(colBase, y)]++;
        }
      }
      LOG_DBG("XTR", "Pixel distribution: White=%lu, DarkGrey=%lu, LightGrey=%lu, Black=%lu", pixelCounts[0],
              pixelCounts[1], pixelCounts[2], pixelCounts[3]);
    }
#endif

    // Pass 1: BW buffer - draw all non-white pixels as black
    plotWhere(xtc::XthMask::NON_WHITE, true);

    if (pagesUntilFullRefresh <= 1) {
      // Periodic ghost cleanup: scrub via the normal path, then run the
      // settle flavor of the grayscale base pass (DTM planes are equal after
      // the display sync, so only the gentle reinforcement cells fire).
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      renderer.preconditionGrayscale();
      pagesUntilFullRefresh = SETTINGS.getMangaRefreshFrequency();
    } else {
      // OEM grayscale pipeline base: differential "AA-pre-BW(mid)" update as
      // the page turn on X3; plain FAST refresh on X4 (previous behavior).
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      pagesUntilFullRefresh--;
    }

    // Pass 2: LSB buffer - mark DARK gray only (XTH value 1)
    // In LUT: 0 bit = apply gray effect, 1 bit = untouched
    renderer.clearScreen(0x00);
    plotWhere(xtc::XthMask::DARK_GREY, false);
    renderer.copyGrayscaleLsbBuffers();

    // Pass 3: MSB buffer - mark LIGHT AND DARK gray (XTH value 1 or 2)
    // In LUT: 0 bit = apply gray effect, 1 bit = untouched
    renderer.clearScreen(0x00);
    plotWhere(xtc::XthMask::ANY_GREY, false);
    renderer.copyGrayscaleMsbBuffers();

    // Display grayscale overlay
    renderer.displayGrayBuffer();

    // Pass 4: Re-render BW to framebuffer (restore for next frame, instead of restoreBwBuffer)
    renderer.clearScreen();
    plotWhere(xtc::XthMask::NON_WHITE, true);

    // Cleanup grayscale buffers with current frame buffer
    renderer.cleanupGrayscaleWithFrameBuffer();

    LOG_DBG("XTR", "Rendered page %lu/%lu (2-bit grayscale)", currentPage + 1, xtc->getPageCount());
    return;
  } else {
    // 1-bit mode: 8 pixels per byte, MSB first
    const size_t srcRowBytes = (pageWidth + 7) / 8;  // 60 bytes for 480 width

    for (uint16_t srcY = 0; srcY < maxSrcY; srcY++) {
      const size_t srcRowStart = srcY * srcRowBytes;

      for (uint16_t srcX = 0; srcX < pageWidth; srcX++) {
        // Read source pixel (MSB first, bit 7 = leftmost pixel)
        const size_t srcByte = srcRowStart + srcX / 8;
        const size_t srcBit = 7 - (srcX % 8);
        const bool isBlack = !((pageBuffer[srcByte] >> srcBit) & 1);  // XTC: 0 = black, 1 = white

        if (isBlack) {
          renderer.drawPixel(srcX, srcY, true);
        }
      }
    }
  }
  // White pixels are already cleared by clearScreen()

  switch (SETTINGS.statusBarSpec().xtcMode) {
    case KomaSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP:
      renderStatusBarOverlay(StatusBarOverlayPosition::Top);
      break;
    case KomaSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_RIGHT:
      renderSideStatusBar();
      break;
    default:
      // Bottom, and Hide -- renderStatusBarOverlay returns early on Hide.
      renderStatusBarOverlay(StatusBarOverlayPosition::Bottom);
      break;
  }

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, false, SETTINGS.getMangaRefreshFrequency());

  LOG_DBG("XTR", "Rendered page %lu/%lu (%u-bit)", currentPage + 1, xtc->getPageCount(), bitDepth);
}

void XtcReaderActivity::saveProgress() const {
  // The page count rides along so the home screen can show progress for a
  // volume it is not reading, without opening the XTC to find out how long it
  // is. See XtcProgress.h.
  if (!XtcProgress::write(xtc->getCachePath(), currentPage, xtc->getPageCount(), sliceOffset)) {
    LOG_ERR("XTR", "Failed to save progress: page %lu", currentPage);
  }
}

void XtcReaderActivity::loadProgress() {
  const XtcProgress::Snapshot saved = XtcProgress::read(xtc->getCachePath());

  // How the global setting and the per-book memory resolve: a setting left on
  // AUTO means "no manual override", so the volume's own remembered value
  // stands. Setting it to anything else is a deliberate act on the volume in
  // front of you, so it wins and is written back as that volume's value on the
  // next save. Leaving it on AUTO afterwards is what makes the correction stick
  // to the book rather than to the reader.
  const int setting = SETTINGS.getMangaSliceOffset();
  sliceOffset = setting != 0 ? setting : (saved.hasSliceOffset ? static_cast<int>(saved.sliceOffset) : 0);

  if (!saved.valid) {
    return;
  }

  currentPage = saved.page;
  LOG_DBG("XTR", "Loaded progress: page %lu", currentPage);

  // Validate against this file, not the stored count: the volume may have been
  // re-encoded shorter since it was last read.
  if (currentPage >= xtc->getPageCount()) {
    currentPage = 0;
  }
}

ScreenshotInfo XtcReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Xtc;
  if (xtc) {
    const std::string t = xtc->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
    const uint32_t pageCount = xtc->getPageCount();
    info.totalPages = pageCount;
    // Clamp to last valid page to avoid sentinel value (currentPage == pageCount)
    uint32_t clampedPage = (pageCount > 0 && currentPage >= pageCount) ? pageCount - 1 : currentPage;
    info.progressPercent = pageCount > 0 ? xtc->calculateProgress(clampedPage) : 0;
    info.currentPage = static_cast<int>(clampedPage) + 1;
  } else {
    info.currentPage = currentPage + 1;
  }
  return info;
}
