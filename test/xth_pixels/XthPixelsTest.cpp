/**
 * Tests for xtc::XthPage, the addressing for XTH (2-bit grayscale) pages.
 *
 * The addressing used to be open-coded inside XtcReaderActivity::renderPage().
 * These tests pin it against that original formula, so the extraction — and the
 * loop restructuring that hoisted the column multiply out of the per-pixel path
 * — cannot silently change which pixel lands where. Getting the column order or
 * the plane order wrong produces a mirrored or sheared page, which is exactly
 * the class of bug that is obvious on hardware and invisible in review.
 */

#include <Xtc/XthPixels.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr uint16_t kWidth = 37;   // deliberately not a multiple of 8
constexpr uint16_t kHeight = 53;  // deliberately not a multiple of 8

/**
 * The addressing exactly as XtcReaderActivity::renderPage() open-coded it
 * before XthPage existed. Reproduced here as the reference implementation the
 * extracted class must agree with.
 */
uint8_t referenceLevelAt(const std::vector<uint8_t>& payload, const uint16_t width, const uint16_t height,
                         const uint16_t x, const uint16_t y) {
  // Uses the corrected plane size. The original open-coded version computed
  // ceil(width*height/8) here, which is the bug PlaneSizeMatchesWhatTheEncodersWrite
  // pins: it only equals the real plane size when height % 8 == 0.
  const size_t planeSize = static_cast<size_t>((height + 7) / 8) * width;
  const uint8_t* plane1 = payload.data();
  const uint8_t* plane2 = payload.data() + planeSize;
  const size_t colBytes = (height + 7) / 8;

  const size_t colIndex = width - 1 - x;
  const size_t byteInCol = y / 8;
  const size_t bitInByte = 7 - (y % 8);
  const size_t byteOffset = colIndex * colBytes + byteInCol;
  const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
  const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
  return static_cast<uint8_t>((bit1 << 1) | bit2);
}

/** A payload sized for width x height, filled with a deterministic pattern. */
std::vector<uint8_t> makePayload(const uint16_t width, const uint16_t height, const uint32_t seed) {
  std::vector<uint8_t> payload(xtc::XthPage::payloadSizeFor(width, height), 0);
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> byte(0, 255);
  for (auto& b : payload) {
    b = static_cast<uint8_t>(byte(rng));
  }
  return payload;
}

/** Writes a single pixel's level, so a test can plant a known value. */
void writeLevel(std::vector<uint8_t>& payload, const uint16_t width, const uint16_t height, const uint16_t x,
                const uint16_t y, const uint8_t level) {
  const size_t planeSize = xtc::XthPage::planeSizeFor(width, height);
  const size_t colBytes = (height + 7) / 8;
  const size_t byteOffset = static_cast<size_t>(width - 1 - x) * colBytes + (y / 8);
  const uint8_t mask = static_cast<uint8_t>(1 << (7 - (y % 8)));

  if (level & 2) {
    payload[byteOffset] |= mask;
  } else {
    payload[byteOffset] &= static_cast<uint8_t>(~mask);
  }
  if (level & 1) {
    payload[planeSize + byteOffset] |= mask;
  } else {
    payload[planeSize + byteOffset] &= static_cast<uint8_t>(~mask);
  }
}

TEST(XthPage, MatchesOriginalOpenCodedAddressing) {
  const auto payload = makePayload(kWidth, kHeight, 12345);
  const xtc::XthPage page(payload.data(), kWidth, kHeight);

  for (uint16_t x = 0; x < kWidth; x++) {
    for (uint16_t y = 0; y < kHeight; y++) {
      EXPECT_EQ(page.levelAt(x, y), referenceLevelAt(payload, kWidth, kHeight, x, y))
          << "mismatch at (" << x << ", " << y << ")";
    }
  }
}

TEST(XthPage, HoistedColumnBaseMatchesPerPixelLookup) {
  // The render loop hoists columnBase() out of the inner loop. That is only
  // safe if the hoisted form is identical to the direct one for every pixel.
  const auto payload = makePayload(kWidth, kHeight, 999);
  const xtc::XthPage page(payload.data(), kWidth, kHeight);

  for (uint16_t x = 0; x < kWidth; x++) {
    const size_t colBase = page.columnBase(x);
    for (uint16_t y = 0; y < kHeight; y++) {
      EXPECT_EQ(page.levelInColumn(colBase, y), page.levelAt(x, y)) << "mismatch at (" << x << ", " << y << ")";
    }
  }
}

TEST(XthPage, RoundTripsEveryLevel) {
  std::vector<uint8_t> payload(xtc::XthPage::payloadSizeFor(kWidth, kHeight), 0);
  const xtc::XthPage page(payload.data(), kWidth, kHeight);

  for (uint8_t level = 0; level < 4; level++) {
    writeLevel(payload, kWidth, kHeight, 5, 11, level);
    EXPECT_EQ(page.levelAt(5, 11), level);
  }
}

TEST(XthPage, ColumnsAreReversed) {
  // Column order is the thing that silently mirrors a page when it is wrong:
  // x=0 must address the LAST column in the payload.
  std::vector<uint8_t> payload(xtc::XthPage::payloadSizeFor(kWidth, kHeight), 0);
  const xtc::XthPage page(payload.data(), kWidth, kHeight);
  const size_t colBytes = (kHeight + 7) / 8;

  EXPECT_EQ(page.columnBase(0), static_cast<size_t>(kWidth - 1) * colBytes);
  EXPECT_EQ(page.columnBase(kWidth - 1), 0u);
}

TEST(XthPage, TopmostPixelInAByteIsTheMostSignificantBit) {
  std::vector<uint8_t> payload(xtc::XthPage::payloadSizeFor(kWidth, kHeight), 0);
  const xtc::XthPage page(payload.data(), kWidth, kHeight);

  // Set only bit 7 of column 0's first byte, in both planes -> Black at y=0.
  const size_t colBase = page.columnBase(0);
  payload[colBase] = 0x80;
  payload[xtc::XthPage::planeSizeFor(kWidth, kHeight) + colBase] = 0x80;

  EXPECT_EQ(page.levelAt(0, 0), static_cast<uint8_t>(xtc::XthLevel::Black));
  EXPECT_EQ(page.levelAt(0, 1), static_cast<uint8_t>(xtc::XthLevel::White));
}

TEST(XthPage, PlaneOrderPutsBit1InTheFirstPlane) {
  // plane1 carries the high bit. Swapping the planes turns DarkGrey into
  // LightGrey and vice versa — a subtle tone inversion rather than a crash.
  std::vector<uint8_t> payload(xtc::XthPage::payloadSizeFor(kWidth, kHeight), 0);
  const xtc::XthPage page(payload.data(), kWidth, kHeight);

  const size_t colBase = page.columnBase(3);
  payload[colBase] = 0x80;  // plane1 only

  EXPECT_EQ(page.levelAt(3, 0), static_cast<uint8_t>(xtc::XthLevel::LightGrey));

  payload[colBase] = 0x00;
  payload[xtc::XthPage::planeSizeFor(kWidth, kHeight) + colBase] = 0x80;  // plane2 only

  EXPECT_EQ(page.levelAt(3, 0), static_cast<uint8_t>(xtc::XthLevel::DarkGrey));
}

TEST(XthMask, SelectsTheIntendedLevels) {
  using xtc::XthPage;
  constexpr auto white = static_cast<uint8_t>(xtc::XthLevel::White);
  constexpr auto dark = static_cast<uint8_t>(xtc::XthLevel::DarkGrey);
  constexpr auto light = static_cast<uint8_t>(xtc::XthLevel::LightGrey);
  constexpr auto black = static_cast<uint8_t>(xtc::XthLevel::Black);

  // NON_WHITE drives the BW passes: everything that leaves ink.
  EXPECT_FALSE(XthPage::matches(xtc::XthMask::NON_WHITE, white));
  EXPECT_TRUE(XthPage::matches(xtc::XthMask::NON_WHITE, dark));
  EXPECT_TRUE(XthPage::matches(xtc::XthMask::NON_WHITE, light));
  EXPECT_TRUE(XthPage::matches(xtc::XthMask::NON_WHITE, black));

  // DARK_GREY drives the LSB pass: level 1 alone.
  EXPECT_FALSE(XthPage::matches(xtc::XthMask::DARK_GREY, white));
  EXPECT_TRUE(XthPage::matches(xtc::XthMask::DARK_GREY, dark));
  EXPECT_FALSE(XthPage::matches(xtc::XthMask::DARK_GREY, light));
  EXPECT_FALSE(XthPage::matches(xtc::XthMask::DARK_GREY, black));

  // ANY_GREY drives the MSB pass: levels 1 and 2, but not black.
  EXPECT_FALSE(XthPage::matches(xtc::XthMask::ANY_GREY, white));
  EXPECT_TRUE(XthPage::matches(xtc::XthMask::ANY_GREY, dark));
  EXPECT_TRUE(XthPage::matches(xtc::XthMask::ANY_GREY, light));
  EXPECT_FALSE(XthPage::matches(xtc::XthMask::ANY_GREY, black));
}

TEST(XthPage, PlaneSizeMatchesWhatTheEncodersWrite) {
  // xtcjs and FlipNzb's encodeXth both allocate colBytes*width per plane. The
  // parser used to compute ceil(width*height/8) instead, which is equal only
  // when height is a multiple of 8 -- true for every 800-tall page shipped so
  // far, which is why this never surfaced.
  for (auto [w, h] : {std::pair<uint16_t, uint16_t>{480, 800}, {kWidth, kHeight}, {1, 1}, {7, 9}, {100, 53}}) {
    const size_t encoderPlaneSize = static_cast<size_t>((h + 7) / 8) * w;
    EXPECT_EQ(xtc::XthPage::planeSizeFor(w, h), encoderPlaneSize) << "w=" << w << " h=" << h;
    EXPECT_EQ(xtc::XthPage::payloadSizeFor(w, h), encoderPlaneSize * 2) << "w=" << w << " h=" << h;
  }

  // The 480x800 case both formulas agree on, pinned so a future change to the
  // sizing cannot silently alter the only geometry in the wild today.
  EXPECT_EQ(xtc::XthPage::planeSizeFor(480, 800), 48000u);
  EXPECT_EQ(xtc::XthPage::payloadSizeFor(480, 800), 96000u);
}

TEST(XthPage, LastPixelStaysInsideThePayload) {
  // Column stride is ceil(height/8), so the highest byte the addressing can
  // touch is (width-1)*colBytes + (height-1)/8. That must be < planeSize, or a
  // full-page pass reads off the end of the page buffer.
  for (auto [w, h] : {std::pair<uint16_t, uint16_t>{480, 800}, {kWidth, kHeight}, {1, 1}, {7, 9}, {100, 53}}) {
    const size_t colBytes = (h + 7) / 8;
    const size_t highestByte = static_cast<size_t>(w - 1) * colBytes + (h - 1) / 8;
    EXPECT_LT(highestByte, xtc::XthPage::planeSizeFor(w, h)) << "w=" << w << " h=" << h;
  }
}

TEST(Xtg, PayloadSizePadsEachRowToAByte) {
  // XTG is row-major: rowBytes*height, matching FlipNzb's encodeXtg.
  EXPECT_EQ(xtc::xtgPayloadSizeFor(480, 800), 48000u);
  EXPECT_EQ(xtc::xtgPayloadSizeFor(37, 53), static_cast<size_t>(5 * 53));
  EXPECT_EQ(xtc::xtgPayloadSizeFor(1, 1), 1u);
}

}  // namespace
