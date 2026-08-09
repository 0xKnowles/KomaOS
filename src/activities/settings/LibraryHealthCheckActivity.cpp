#include "LibraryHealthCheckActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc/XtcTypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr size_t NAME_BUFFER_SIZE = 500;
// Only entries actually processed per directory count towards the yield
// below; SD I/O dominates the per-entry cost, but a card with a very large
// flat folder could still add up to a watchdog-length pass without it.
constexpr int ENTRIES_PER_YIELD = 20;

// Dot-directories (".komaos", a card reader's hidden metadata, etc.) hold no
// book content and are always skipped, independent of SETTINGS.showHiddenFiles
// -- that setting is about what a human browses, not what this scan counts.
bool isSkippableDirName(const char* name) { return name[0] == '.'; }

std::string joinPath(const std::string& dir, const char* name) {
  return dir == "/" ? "/" + std::string(name) : dir + "/" + std::string(name);
}

}  // namespace

void LibraryHealthCheckActivity::onEnter() {
  Activity::onEnter();
  state = State::SCANNING;
  // Show the "Scanning..." screen before the blocking scan() call runs.
  requestUpdateAndWait();
  scan();
}

void LibraryHealthCheckActivity::onExit() {
  // Hand the flagged-path strings back to the heap here rather than waiting for
  // the activity object's destruction: the parent screen re-renders as soon as
  // this returns, and on a card with many flagged volumes this list is the
  // largest thing the scan allocated.
  flaggedPaths.clear();
  flaggedPaths.shrink_to_fit();
  Activity::onExit();
}

void LibraryHealthCheckActivity::scan() {
  flaggedPaths.clear();
  flaggedPaths.reserve(MAX_FLAGGED);
  scannedCount = 0;
  flaggedCount = 0;
  truncated = false;

  const auto nameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!nameBuffer) {
    LOG_ERR("LIBHEALTH", "OOM: %d bytes", static_cast<int>(NAME_BUFFER_SIZE));
    state = State::RESULTS;
    requestUpdate();
    return;
  }

  // Explicit stack, not recursion: a deeply nested folder tree on the card
  // must not grow the call stack per level (see CLAUDE.md stack safety rule).
  std::vector<std::string> pendingDirs;
  pendingDirs.push_back("/");

  int entriesSinceYield = 0;
  while (!pendingDirs.empty()) {
    const std::string dirPath = std::move(pendingDirs.back());
    pendingDirs.pop_back();

    auto dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
      continue;
    }
    dir.rewindDirectory();

    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      file.getName(nameBuffer.get(), NAME_BUFFER_SIZE);
      const char* name = nameBuffer.get();

      if (file.isDirectory()) {
        if (!isSkippableDirName(name)) {
          pendingDirs.push_back(joinPath(dirPath, name));
        }
      } else if (FsHelpers::hasXtcExtension(name)) {
        xtc::XtcHeader header{};
        const int bytesRead = file.read(&header, sizeof(header));
        if (bytesRead == static_cast<int>(sizeof(header)) &&
            (header.magic == xtc::XTC_MAGIC || header.magic == xtc::XTCH_MAGIC)) {
          scannedCount++;
          if (!xtc::decodeSplitGeometry(header.splitGeometry).valid) {
            flaggedCount++;
            if (flaggedPaths.size() < MAX_FLAGGED) {
              flaggedPaths.push_back(joinPath(dirPath, name));
            } else {
              truncated = true;
            }
          }
        }
      }
      // No explicit file.close(): DESTRUCTOR_CLOSES_FILE=1 handles it when the
      // reassignment above (or loop exit) releases this iteration's handle.

      if (++entriesSinceYield >= ENTRIES_PER_YIELD) {
        entriesSinceYield = 0;
        vTaskDelay(1);
      }
    }
    dir.close();
  }

  LOG_DBG("LIBHEALTH", "Scanned %d volumes, %d missing split geometry", scannedCount, flaggedCount);
  state = State::RESULTS;
  requestUpdate();
}

void LibraryHealthCheckActivity::loop() {
  if (state == State::SCANNING) {
    return;  // scan() runs to completion inside onEnter(); nothing to do meanwhile
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (flaggedPaths.empty()) {
    return;
  }

  buttonNavigator.onNext([this] {
    selector = ButtonNavigator::nextIndex(selector, static_cast<int>(flaggedPaths.size()));
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selector = ButtonNavigator::previousIndex(selector, static_cast<int>(flaggedPaths.size()));
    requestUpdate();
  });
}

void LibraryHealthCheckActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LIBRARY_HEALTH_CHECK));

  if (state == State::SCANNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_LIBRARY_HEALTH_SCANNING));
    renderer.displayBuffer();
    return;
  }

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  if (flaggedPaths.empty()) {
    const Rect safe{0, contentTop, pageWidth, pageHeight - contentTop - metrics.verticalSpacing};
    const std::string summary =
        std::to_string(scannedCount) + " " + std::string(tr(STR_LIBRARY_HEALTH_VOLUMES_SCANNED));
    UITheme::drawCenteredText(renderer, safe, UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_LIBRARY_HEALTH_ALL_OK), true,
                              EpdFontFamily::BOLD);
    UITheme::drawCenteredText(renderer, safe, UI_10_FONT_ID, pageHeight / 2 + 10, summary.c_str());
  } else {
    std::string subtitle = std::to_string(flaggedCount) + " " + std::string(tr(STR_LIBRARY_HEALTH_NEED_OFFSET));
    if (truncated) {
      subtitle += " (" + std::string(tr(STR_LIBRARY_HEALTH_TRUNCATED)) + ")";
    }
    GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                      subtitle.c_str());

    const int listTop = contentTop + metrics.tabBarHeight;
    const int listHeight = pageHeight - listTop - metrics.verticalSpacing;
    GUI.drawList(renderer, Rect{0, listTop, pageWidth, listHeight}, static_cast<int>(flaggedPaths.size()), selector,
                 [this](const int index) { return flaggedPaths[index]; });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
