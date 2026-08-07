#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/Logo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_KOMAOS), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  // Tagline from the brand lockup. Live text rather than part of Logo120: at
  // 120px it would be a few pixels tall and illegible baked into the mark.
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 48, tr(STR_KOMAOS_TAGLINE));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, KOMAOS_VERSION);
  renderer.displayBuffer();
}
