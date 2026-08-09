#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Scans the SD card for XTC/XTCH volumes whose header never recorded split
 * geometry (xtc::decodeSplitGeometry on the qword at header offset 0x28, see
 * lib/Xtc/Xtc/XtcTypes.h). Full page has to guess the strip overlap and
 * lead-in for those volumes -- this makes the volumes that need a manual
 * Slice Offset visible instead of a trial-and-error discovery per book.
 *
 * Reads only the 56-byte XtcHeader per file (one seek+read, at open), never
 * the page table, metadata or chapters, so scanning a whole card stays fast
 * regardless of how many pages each volume has.
 */
class LibraryHealthCheckActivity final : public Activity {
 public:
  explicit LibraryHealthCheckActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LibraryHealthCheck", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State { SCANNING, RESULTS };

  // Cap on stored paths: a card with hundreds of flagged volumes should still
  // report a bounded, fast-to-render list rather than growing the heap to
  // match. flaggedCount (below) keeps the true total even past this cap.
  static constexpr size_t MAX_FLAGGED = 200;

  State state = State::SCANNING;
  std::vector<std::string> flaggedPaths;
  int scannedCount = 0;
  int flaggedCount = 0;  // true total; may exceed flaggedPaths.size()
  bool truncated = false;
  int selector = 0;

  ButtonNavigator buttonNavigator;

  void scan();
};
