#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "SplashLayout.h"
#include "fontIds.h"
#include "images/BootSplash.h"
#include "images/Logo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  // pageHeight is no longer read: the splash pins every y coordinate, so the
  // layout no longer derives anything from the panel height.
  const auto pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  // The splash is a fixed 480x800 page of koma. Drawn first; everything below
  // lands in the two panels the artwork deliberately leaves empty.
  renderer.drawImage(BootSplash, 0, 0, SplashLayout::WIDTH, SplashLayout::HEIGHT);
  renderer.drawImage(Logo120, (pageWidth - SplashLayout::MARK_SIZE) / 2, SplashLayout::MARK_Y, SplashLayout::MARK_SIZE,
                     SplashLayout::MARK_SIZE);
  renderer.drawCenteredText(UI_10_FONT_ID, SplashLayout::WORDMARK_Y, tr(STR_KOMAOS), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, SplashLayout::STATUS_Y, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, SplashLayout::TAGLINE_Y, tr(STR_KOMAOS_TAGLINE));
  // No version line: the tagline is the last thing on the boot screen. The
  // version is still reachable in Settings > About and is what OTA compares
  // against, so nothing depends on it being shown here.
  renderer.displayBuffer();
}
