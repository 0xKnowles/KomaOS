/**
 * Guards the 0x28 header qword, which is a wire format shared with the
 * encoders: KomaCut's encodeSplitGeometry (src/lib/xtc-geometry.ts) and
 * FlipNzb's (server/src/core/convert/xtc.ts) both write these exact bits.
 * A change here that is not mirrored there silently mis-renders every page.
 */

#include <gtest/gtest.h>

#include <cstdint>

#include "Xtc/XtcTypes.h"

namespace {

using xtc::decodeSplitGeometry;
using xtc::pageStartMapBytes;
using xtc::XtcSplitGeometry;

/** Packs a qword the way the encoders do, so the test pins the layout. */
constexpr uint64_t pack(const uint8_t mode, const uint8_t strips, const uint16_t overlapPerMille,
                        const uint8_t rotation, const uint8_t leading, const bool hasMap = false) {
  return static_cast<uint64_t>(mode) | (static_cast<uint64_t>(strips) << 8) |
         (static_cast<uint64_t>(overlapPerMille) << 16) | (static_cast<uint64_t>(rotation) << 32) |
         (hasMap ? xtc::XTC_SPLIT_HAS_PAGE_START_MAP : 0ULL) | (static_cast<uint64_t>(leading) << 40);
}

TEST(XtcSplitGeometry, ZeroIsTheLegacyNotRecordedValue) {
  // Every file written before encoders recorded geometry has a zeroed qword,
  // and it must stay distinguishable from a real layout.
  const XtcSplitGeometry g = decodeSplitGeometry(0);
  EXPECT_FALSE(g.valid);
  EXPECT_EQ(g.mode, 0);
  EXPECT_FALSE(g.hasPageStartMap);
}

TEST(XtcSplitGeometry, DecodesEveryField) {
  // A 1114x1600 tankobon page: 3 strips, 302 per-mille overlap, one clockwise
  // turn, one lead-in strip for the nosplit cover.
  const XtcSplitGeometry g = decodeSplitGeometry(pack(2, 3, 302, 1, 1));

  EXPECT_TRUE(g.valid);
  EXPECT_EQ(g.mode, 2);
  EXPECT_EQ(g.stripsPerPage, 3);
  EXPECT_EQ(g.overlapPerMille, 302);
  EXPECT_EQ(g.rotationQuarterTurns, 1);
  EXPECT_EQ(g.leadingStrips, 1);
  EXPECT_FALSE(g.hasPageStartMap);
}

TEST(XtcSplitGeometry, OverlapPercentRoundsToNearest) {
  // The field is per-mille because a whole percent moves a seam a pixel or two;
  // the percent accessor is only for the layout planner, which wants percent.
  EXPECT_EQ(decodeSplitGeometry(pack(2, 3, 302, 1, 1)).overlapPercent(), 30);
  EXPECT_EQ(decodeSplitGeometry(pack(2, 3, 305, 1, 1)).overlapPercent(), 31);
  EXPECT_EQ(decodeSplitGeometry(pack(2, 3, 250, 1, 1)).overlapPercent(), 25);
}

TEST(XtcSplitGeometry, CounterClockwiseRotationSurvives) {
  // KomaCut rotates either way depending on landscapeFlipClockwise, so three
  // quarter turns is a real value and not a corrupt one.
  EXPECT_EQ(decodeSplitGeometry(pack(2, 3, 302, 3, 1)).rotationQuarterTurns, 3);
}

TEST(XtcSplitGeometry, PageStartMapFlagIsIndependentOfEveryOtherField) {
  const uint64_t without = pack(2, 3, 302, 1, 1, false);
  const uint64_t with = pack(2, 3, 302, 1, 1, true);

  const XtcSplitGeometry a = decodeSplitGeometry(without);
  const XtcSplitGeometry b = decodeSplitGeometry(with);

  EXPECT_FALSE(a.hasPageStartMap);
  EXPECT_TRUE(b.hasPageStartMap);

  // Setting the flag must not disturb anything an older build reads.
  EXPECT_EQ(a.mode, b.mode);
  EXPECT_EQ(a.stripsPerPage, b.stripsPerPage);
  EXPECT_EQ(a.overlapPerMille, b.overlapPerMille);
  EXPECT_EQ(a.rotationQuarterTurns, b.rotationQuarterTurns);
  EXPECT_EQ(a.leadingStrips, b.leadingStrips);
}

TEST(XtcSplitGeometry, FlagLivesInABitOlderBuildsIgnored) {
  // Bit 34 was masked off by the original decoder: rotation takes bits 32-33
  // and leadingStrips starts at bit 40. Pinning the position is the point —
  // moving it would break files already written.
  EXPECT_EQ(xtc::XTC_SPLIT_HAS_PAGE_START_MAP, 1ULL << 34);

  const uint64_t packed = pack(2, 3, 302, 1, 1, true);
  EXPECT_EQ((packed >> 32) & 0x03, 1u);  // rotation unaffected
  EXPECT_EQ((packed >> 40) & 0xFF, 1u);  // leadingStrips unaffected
}

TEST(XtcSplitGeometry, StripsPerPageZeroIsNotUsable) {
  // A nonzero mode with no strips is a malformed record, not a split of zero.
  EXPECT_FALSE(decodeSplitGeometry(pack(2, 0, 302, 1, 1)).valid);
}

TEST(XtcSplitGeometry, PageStartMapSizeIsOneBitPerPage) {
  EXPECT_EQ(pageStartMapBytes(0), 0u);
  EXPECT_EQ(pageStartMapBytes(1), 1u);
  EXPECT_EQ(pageStartMapBytes(8), 1u);
  EXPECT_EQ(pageStartMapBytes(9), 2u);
  // A 200-page volume at three strips a page.
  EXPECT_EQ(pageStartMapBytes(600), 75u);
}

/**
 * Mirrors XtcParser's bit test so the packing agrees with the encoder's.
 *
 * The parser's own reader needs HalStorage and a real file, so the bit order
 * is pinned here instead: MSB first within each byte, matching KomaCut's
 * buildPageStartMap.
 */
bool isPageStart(const uint8_t* map, const uint32_t index) { return ((map[index >> 3] >> (7 - (index & 7))) & 1) != 0; }

TEST(XtcSplitGeometry, PageStartMapIsMsbFirst) {
  // Cover (1 strip), two split pages (3 each), a spread (1), a split page (3):
  // starts at strips 0, 1, 4, 7, 8. This is the case leadingStrips cannot
  // express, because the shift happens mid-volume.
  const uint8_t map[2] = {0b11001001, 0b10000000};

  const bool expected[11] = {true, true, false, false, true, false, false, true, true, false, false};
  for (uint32_t i = 0; i < 11; i++) {
    EXPECT_EQ(isPageStart(map, i), expected[i]) << "strip " << i;
  }
}

}  // namespace
