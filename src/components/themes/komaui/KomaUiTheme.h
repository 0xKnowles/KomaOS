#pragma once

#include <array>

#include "components/themes/lyra/LyraTheme.h"
#include "util/XtcProgress.h"

class GfxRenderer;

/**
 * KomaUI: the home screen as a page of manga panels.
 *
 * Three portrait cover slots across the top, a stats koma under them, and four
 * menu bars below that -- all of it ink from src/images/KomaBackground.h, with
 * this theme drawing only the art and text that lands inside.
 *
 * The slots are ~0.40 aspect where a manga cover is ~0.70, so a cover is
 * width-limited in them and leaves vertical slack. That slack is where the
 * series name and volume go; see drawPanel in the .cpp for why the layout is
 * driven by which slack dimension is larger rather than by a fixed split.
 *
 * The stats koma reports the selected volume, not a library total: the recent
 * list this theme is handed is already capped and collapsed to one entry per
 * series, so a "total" drawn from it would be a count of this block wearing a
 * library's name. Nothing time-based is possible either -- the X4 has no RTC
 * and nothing on the card is timestamped, so streaks and pages-per-day are not
 * computable, however well they would suit the genre.
 *
 * Everything else (lists, keyboard, popups) is inherited from Lyra; this
 * overrides the home block, the menu styling and the header rule.
 */

namespace KomaUiMetrics {

/**
 * Panel interiors of src/images/KomaBackground.h, in logical coordinates.
 *
 * Measured off the converted image's pixels, not the artwork that produced it:
 * the generator crops, thresholds and rotates, so the drawing's intent and the
 * bytes on the device are not guaranteed to agree. Re-measure if the background
 * is regenerated -- the static_asserts below catch a panel that has moved off
 * the screen, but not one that has shifted by twenty pixels.
 */
struct Panel {
  int x, y, w, h;
  // constexpr, not just const: the static_asserts below and the metrics lambda
  // both call these in constant expressions, and a member function is not
  // implicitly constexpr.
  constexpr int right() const { return x + w; }
  constexpr int bottom() const { return y + h; }
};

/** Three portrait cover slots across the top. */
constexpr int PANEL_COUNT = 3;
constexpr Panel COVER[PANEL_COUNT] = {
    {16, 40, 121, 305},
    {183, 40, 116, 305},
    {343, 40, 119, 305},
};

constexpr Panel STATS{18, 372, 443, 118};

constexpr int MENU_ROWS = 4;
constexpr Panel MENU[MENU_ROWS] = {
    {18, 508, 443, 43},
    {18, 565, 443, 40},
    {18, 621, 444, 55},
    {18, 694, 444, 63},
};

/**
 * Thumbnail height.
 *
 * The cover slots are ~0.40 aspect, narrower than a manga cover's ~0.70, so a
 * cover is width-limited in them: 121px of slot width takes about 173px of
 * height. Generating at 180 keeps the draw close to 1:1 rather than resampling a
 * 305-tall thumbnail down by half.
 */
constexpr int COVER_HEIGHT = 180;
/** Inset of text from a panel's inked border. */
constexpr int PAD = 8;

constexpr int PORTRAIT_PANEL_HEIGHT = 800;
static_assert(MENU[MENU_ROWS - 1].bottom() <= PORTRAIT_PANEL_HEIGHT - LyraMetrics::values.buttonHintsHeight,
              "The background's last menu row overlaps the button hints.");
static_assert(STATS.bottom() <= MENU[0].y, "The stats panel overlaps the first menu row.");
static_assert(COVER[PANEL_COUNT - 1].bottom() <= STATS.y, "A cover slot overlaps the stats panel.");
static_assert(COVER[0].right() <= COVER[1].x && COVER[1].right() <= COVER[2].x, "The cover slots overlap.");

constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeCoverHeight = COVER_HEIGHT;
  // The theme paints the whole page, so it takes the whole page as its rect
  // and HomeActivity is left with nothing to position.
  v.homeTopPadding = 0;
  // Everything above the first menu bar -- covers and the stats koma -- is the
  // cover tile as far as HomeActivity is concerned. It only uses this to place
  // the menu below and to hit-test a tap on the shelf.
  v.homeCoverTileHeight = MENU[0].y;
  v.homeMenuTopOffset = 0;
  // HomeActivity hit-tests menu taps on a uniform pitch from homeCoverTileHeight,
  // but the drawn bars are hand-inked and not evenly spaced (rows start at 508,
  // 565, 621, 694). A pitch of 62 with a 40px band is the fit that puts all four
  // bands inside their own bar; drawButtonMenu still draws labels at the measured
  // positions rather than deriving them from these.
  v.menuRowHeight = 40;
  v.menuSpacing = 22;
  v.homeRecentBooksCount = PANEL_COUNT;
  v.homeGroupRecentsBySeries = true;
  return v;
}();

}  // namespace KomaUiMetrics

class KomaUiTheme : public LyraTheme {
  // Progress for the four volumes on the shelf, read once when the shelf is
  // composed rather than on every selector move -- the stats box reads whichever
  // volume is selected, and four SD opens per frame to redraw one 44px box is
  // not a trade worth making. Mutable because the draw methods are const, the
  // same reason LyraTheme::coverWidth is.
  mutable std::array<XtcProgress::Snapshot, KomaUiMetrics::PANEL_COUNT> shelfProgress{};
  mutable bool shelfProgressLoaded = false;

  void drawStatsKoma(const GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                     int selectorIndex) const;

 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  // Lyra's menu is rows of icon-and-label with a light fill on the selected one
  // and nothing else, which reads as very flat under a shelf of cover art. This
  // delegates to Lyra for the icons and labels -- the icon lookup is file-local
  // to LyraTheme.cpp -- then overlays the styling.
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  // Carries the shelf's ledge motif onto every other screen.
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
};
