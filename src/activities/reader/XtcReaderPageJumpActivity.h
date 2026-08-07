#pragma once
#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Page-number picker for jumping within a volume.
 *
 * Deliberately pages rather than the percent slider the EPUB reader uses: an
 * EPUB page number is meaningless once the font changes, so percent is the only
 * stable handle there. An XTC page is fixed, and "page 88" is what you actually
 * want when hunting a scene.
 *
 * Front Left/Right step one page; the side buttons step by LARGE_STEP, which is
 * what makes a 600-strip volume navigable without holding a button for a minute.
 */
class XtcReaderPageJumpActivity final : public Activity {
 public:
  explicit XtcReaderPageJumpActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint32_t initialPage,
                                     uint32_t pageCount)
      : Activity("XtcReaderPageJump", renderer, mappedInput),
        page(static_cast<int>(initialPage)),
        pageCount(static_cast<int>(pageCount)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int SMALL_STEP = 1;
  static constexpr int LARGE_STEP = 10;

  void adjustPage(int delta);

  // 0-based, matching XtcReaderActivity::currentPage. Displayed 1-based.
  int page = 0;
  int pageCount = 0;

  ButtonNavigator buttonNavigator;
};
