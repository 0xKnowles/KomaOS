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
  // The splash is drawn first, in panel space (see SplashLayout.h -- the bitmap
  // is stored pre-rotated because drawImage does not rotate bits). Everything
  // below is in logical coordinates and lands in the panels the artwork leaves
  // empty.
  renderer.drawImage(BootSplash, SplashLayout::BLIT_X, SplashLayout::BLIT_Y, SplashLayout::BLIT_W,
                     SplashLayout::BLIT_H);
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
