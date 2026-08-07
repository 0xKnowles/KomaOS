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
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"
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

void XtcReaderActivity::toggleBookmarkForCurrentPage() {
  XtcBookmarks::toggle(bookmarkedPages, currentPage);
  if (!XtcBookmarks::save(xtc->getCachePath(), bookmarkedPages)) {
    LOG_ERR("XTR", "Failed to save bookmarks for page %lu", currentPage);
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
      startActivityForResult(
          std::make_unique<XtcReaderBookmarksActivity>(renderer, mappedInput, bookmarkedPages, xtc->getPageCount()),
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

    case XtcReaderMenuActivity::MenuAction::SCREENSHOT:
      ScreenshotUtil::takeScreenshot(renderer);
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
    openReaderMenu();
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
  const int skipAmount = skipPages ? SETTINGS.getMangaSkipPages() : 1;

  if (prevTriggered) {
    if (currentPage >= static_cast<uint32_t>(skipAmount)) {
      currentPage -= skipAmount;
    } else {
      currentPage = 0;
    }
    requestUpdate();
  } else if (nextTriggered) {
    currentPage += skipAmount;
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

  renderPage();
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
  if (!XtcProgress::write(xtc->getCachePath(), currentPage, xtc->getPageCount())) {
    LOG_ERR("XTR", "Failed to save progress: page %lu", currentPage);
  }
}

void XtcReaderActivity::loadProgress() {
  const XtcProgress::Snapshot saved = XtcProgress::read(xtc->getCachePath());
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
