/**
 * Tests for FullPageLayout, which reassembles split manga strips into a page.
 *
 * The expensive failures are geometric and silent: a seam that overlaps redraws
 * art the crop was supposed to remove, and a seam that gaps drops a band of the
 * page entirely. Both look like a bad scan rather than a bug, so most of these
 * pin the seam arithmetic rather than the happy path.
 */

#include <gtest/gtest.h>

#include "FullPageLayout.h"

namespace {

using namespace FullPageLayout;

// One strip as stored for an X4. It is written turned a quarter turn, so
// STRIP_W spans the page rows the strip covers and STRIP_H spans the page's
// full width -- which is why the crops below are measured against STRIP_W.
constexpr int STRIP_W = 480;
constexpr int STRIP_H = 800;
constexpr int PANEL_W = 480;
constexpr int PANEL_H = 800;

Layout planDefault(const int overlapPercent = DEFAULT_OVERLAP_PERCENT) {
  return plan(STRIP_W, STRIP_H, PANEL_W, PANEL_H, overlapPercent);
}

TEST(FullPageLayout, StripsTileTheDestinationWithoutGapOrOverlap) {
  const Layout layout = planDefault();
  ASSERT_TRUE(layout.valid);

  // Every destination row belongs to exactly one strip: each strip starts where
  // the previous ended, and together they cover the whole page.
  EXPECT_EQ(layout.strips[0].dstStart, 0);
  for (int i = 1; i < STRIPS_PER_PAGE; i++) {
    EXPECT_EQ(layout.strips[i].dstStart, layout.strips[i - 1].dstStart + layout.strips[i - 1].dstCount)
        << "seam before strip " << i;
  }
  const StripPlacement& last = layout.strips[STRIPS_PER_PAGE - 1];
  EXPECT_EQ(last.dstStart + last.dstCount, layout.dstHeight);
}

TEST(FullPageLayout, CropsTheOverlapOffEveryStripButTheFirst) {
  const Layout layout = planDefault(27);
  ASSERT_TRUE(layout.valid);

  const int expectedOverlap = STRIP_W * 27 / 100;
  EXPECT_EQ(layout.strips[0].srcStart, 0);
  EXPECT_EQ(layout.strips[0].srcCount, STRIP_W);
  for (int i = 1; i < STRIPS_PER_PAGE; i++) {
    EXPECT_EQ(layout.strips[i].srcStart, expectedOverlap) << "strip " << i;
    EXPECT_EQ(layout.strips[i].srcCount, STRIP_W - expectedOverlap) << "strip " << i;
  }
}

TEST(FullPageLayout, ZeroOverlapKeepsEveryStripWhole) {
  const Layout layout = planDefault(0);
  ASSERT_TRUE(layout.valid);
  for (const auto& strip : layout.strips) {
    EXPECT_EQ(strip.srcStart, 0);
    EXPECT_EQ(strip.srcCount, STRIP_W);
  }
}

TEST(FullPageLayout, ClampsAnOutOfRangeOverlap) {
  // Both ends clamp rather than producing a degenerate or inverted layout.
  const Layout high = planDefault(90);
  const Layout atMax = planDefault(MAX_OVERLAP_PERCENT);
  ASSERT_TRUE(high.valid);
  EXPECT_EQ(high.strips[1].srcStart, atMax.strips[1].srcStart);

  const Layout low = planDefault(-10);
  ASSERT_TRUE(low.valid);
  EXPECT_EQ(low.strips[1].srcStart, 0);
}

TEST(FullPageLayout, LandsAtSixTenthsOfStoredResolutionOnAnX4) {
  // The scale is forced, not chosen: strips are stored scaled to the panel's
  // long edge and the reassembled page is bound by the short edge. If this ever
  // stops being 0.6 the assumption in the header is wrong.
  const Layout layout = planDefault();
  ASSERT_TRUE(layout.valid);
  EXPECT_LE(layout.dstHeight, PANEL_H);
  EXPECT_LE(layout.dstWidth, PANEL_W);

  // Measured against STRIP_H: that is the page's across-extent in a turned
  // strip, so it is what the panel width has to accommodate.
  const double scale = static_cast<double>(layout.dstWidth) / STRIP_H;
  EXPECT_NEAR(scale, 0.6, 0.02);
  // And the assembled page really is taller than one strip's worth of panel.
  EXPECT_GT(layout.dstHeight, layout.dstWidth);
}

TEST(FullPageLayout, CentresAPageNarrowerThanThePanel) {
  const Layout layout = planDefault();
  ASSERT_TRUE(layout.valid);
  EXPECT_EQ(layout.dstLeft, (PANEL_W - layout.dstWidth) / 2);
  EXPECT_GE(layout.dstLeft, 0);
}

TEST(FullPageLayout, RejectsDegenerateInputs) {
  EXPECT_FALSE(plan(0, STRIP_H, PANEL_W, PANEL_H, 27).valid);
  EXPECT_FALSE(plan(STRIP_W, 0, PANEL_W, PANEL_H, 27).valid);
  EXPECT_FALSE(plan(STRIP_W, STRIP_H, 0, PANEL_H, 27).valid);
  EXPECT_FALSE(plan(STRIP_W, STRIP_H, PANEL_W, 0, 27).valid);
}

TEST(FullPageLayout, SourceRowStaysInsideTheStripAcrossTheWholeRun) {
  const Layout layout = planDefault();
  ASSERT_TRUE(layout.valid);

  for (const auto& strip : layout.strips) {
    ASSERT_TRUE(strip.contributes());
    for (int dstRow = 0; dstRow < strip.dstCount; dstRow++) {
      const int srcRow = sourceRowFor(strip, dstRow);
      EXPECT_GE(srcRow, strip.srcStart);
      EXPECT_LT(srcRow, strip.srcStart + strip.srcCount);
      EXPECT_LT(srcRow, STRIP_W) << "would read past the strip buffer";
    }
  }
}

TEST(FullPageLayout, SourceRowIsMonotonicSoRowsAreNeverReordered) {
  const Layout layout = planDefault();
  ASSERT_TRUE(layout.valid);

  for (const auto& strip : layout.strips) {
    int previous = -1;
    for (int dstRow = 0; dstRow < strip.dstCount; dstRow++) {
      const int srcRow = sourceRowFor(strip, dstRow);
      EXPECT_GE(srcRow, previous);
      previous = srcRow;
    }
  }
}

TEST(FullPageLayout, SourceRowClampsPastTheEndInsteadOfOverrunning) {
  const Layout layout = planDefault();
  const StripPlacement& strip = layout.strips[0];
  // One past the last valid row: a render loop off-by-one must not index
  // outside the strip.
  EXPECT_LT(sourceRowFor(strip, strip.dstCount), strip.srcStart + strip.srcCount);
  EXPECT_LT(sourceRowFor(strip, strip.dstCount * 4), strip.srcStart + strip.srcCount);
}

TEST(FullPageLayout, ReportsAtLeastOneSourceRowPerDestinationRow) {
  const Layout layout = planDefault();
  for (const auto& strip : layout.strips) {
    EXPECT_GE(sourceRowsPerDestRow(strip), 1);
  }
  // Downscaling ~0.6 means each output row averages roughly 1-2 source rows.
  EXPECT_LE(sourceRowsPerDestRow(layout.strips[0]), 3);
}

TEST(FullPageLayout, HandlesAWidePanelByFittingHeightInstead) {
  // A panel shorter than the assembled aspect must bind on height and leave
  // side margins, not overflow.
  const Layout layout = plan(STRIP_W, STRIP_H, 800, 480, 27);
  ASSERT_TRUE(layout.valid);
  EXPECT_LE(layout.dstHeight, 480);
  EXPECT_LE(layout.dstWidth, 800);
  EXPECT_GT(layout.dstLeft, 0) << "narrow page on a wide panel should be centred";
}

}  // namespace
