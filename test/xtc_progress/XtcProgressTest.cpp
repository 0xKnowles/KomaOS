/**
 * Tests for XtcProgress, the on-SD layout of an XTC volume's progress.bin.
 *
 * Two things depend on this being right: resuming a volume on the page you left
 * it, and the Collection theme's stats box. The expensive failure is the
 * backwards-compatible one -- a 4-byte file written by older firmware must load
 * as "page N, page count unknown", never as a page count of zero dressed up as
 * a percentage.
 */

#include <gtest/gtest.h>

#include "XtcProgress.h"

namespace {

using XtcProgress::decode;
using XtcProgress::encode;
using XtcProgress::Snapshot;

TEST(XtcProgress, RoundTripsPageAndPageCount) {
  uint8_t buffer[XtcProgress::CURRENT_SIZE];
  ASSERT_EQ(encode(87, 210, buffer, sizeof(buffer)), XtcProgress::CURRENT_SIZE);

  const Snapshot decoded = decode(buffer, sizeof(buffer));
  EXPECT_TRUE(decoded.valid);
  EXPECT_EQ(decoded.page, 87u);
  EXPECT_EQ(decoded.pageCount, 210u);
}

TEST(XtcProgress, RoundTripsValuesPastOneByte) {
  // A long volume exercises every byte of both fields; a truncating shift bug
  // would survive any single-byte page number.
  uint8_t buffer[XtcProgress::CURRENT_SIZE];
  ASSERT_EQ(encode(0x01020304u, 0x0A0B0C0Du, buffer, sizeof(buffer)), XtcProgress::CURRENT_SIZE);

  const Snapshot decoded = decode(buffer, sizeof(buffer));
  EXPECT_EQ(decoded.page, 0x01020304u);
  EXPECT_EQ(decoded.pageCount, 0x0A0B0C0Du);
}

TEST(XtcProgress, WritesLittleEndianRegardlessOfHost) {
  uint8_t buffer[XtcProgress::CURRENT_SIZE];
  ASSERT_EQ(encode(0x04030201u, 0x08070605u, buffer, sizeof(buffer)), XtcProgress::CURRENT_SIZE);

  const uint8_t expected[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  for (size_t i = 0; i < sizeof(expected); i++) {
    EXPECT_EQ(buffer[i], expected[i]) << "byte " << i;
  }
}

TEST(XtcProgress, RefusesToEncodeIntoATooSmallBuffer) {
  uint8_t buffer[XtcProgress::CURRENT_SIZE] = {0xFF};
  EXPECT_EQ(encode(1, 2, buffer, XtcProgress::CURRENT_SIZE - 1), 0u);
  EXPECT_EQ(encode(1, 2, nullptr, sizeof(buffer)), 0u);
  // The buffer must be left alone on refusal, not half-written.
  EXPECT_EQ(buffer[0], 0xFF);
}

TEST(XtcProgress, ReadsLegacyFourByteFilesAsPageOnly) {
  // What every volume on an existing SD card holds today.
  const uint8_t legacy[] = {0x39, 0x05, 0x00, 0x00};
  const Snapshot decoded = decode(legacy, sizeof(legacy));

  EXPECT_TRUE(decoded.valid);
  EXPECT_EQ(decoded.page, 1337u);
  EXPECT_EQ(decoded.pageCount, 0u);
  EXPECT_FALSE(decoded.hasPageCount());
  EXPECT_EQ(decoded.percent(), XtcProgress::UNKNOWN_PERCENT);
}

TEST(XtcProgress, RejectsShortAndMissingBuffers) {
  const uint8_t truncated[] = {0x01, 0x02, 0x03};
  EXPECT_FALSE(decode(truncated, sizeof(truncated)).valid);
  EXPECT_FALSE(decode(nullptr, XtcProgress::CURRENT_SIZE).valid);
  EXPECT_FALSE(decode(truncated, 0).valid);
}

/** Braced initialisers cannot go straight into EXPECT_EQ -- the commas split the macro. */
int percentOf(const uint32_t page, const uint32_t pageCount, const bool valid = true) {
  return Snapshot{page, pageCount, valid}.percent();
}

TEST(XtcProgress, PercentCountsTheCurrentPageAsRead) {
  // Page indices are 0-based, so page 0 of 200 is one page read, not zero.
  EXPECT_EQ(percentOf(0, 200), 0);    // 0.5% floors to 0
  EXPECT_EQ(percentOf(99, 200), 50);  // 100 of 200
  EXPECT_EQ(percentOf(199, 200), 100);
}

TEST(XtcProgress, PercentNeverExceedsOneHundred) {
  // A volume re-encoded shorter than when it was last read leaves a page index
  // past the end. Reporting 340% would be worse than reporting finished.
  EXPECT_EQ(percentOf(680, 200), 100);
}

TEST(XtcProgress, PercentIsUnknownWithoutAValidSnapshot) {
  EXPECT_EQ(Snapshot{}.percent(), XtcProgress::UNKNOWN_PERCENT);
  EXPECT_EQ(percentOf(87, 210, false), XtcProgress::UNKNOWN_PERCENT);
  // Valid but with no page count: the legacy case, and the one that must not
  // divide by zero.
  EXPECT_EQ(percentOf(5, 0), XtcProgress::UNKNOWN_PERCENT);
}

TEST(XtcProgress, PercentDoesNotOverflowOnALargePageIndex) {
  // page * 100 in 32 bits would wrap here; the clamp has to happen first.
  EXPECT_EQ(percentOf(0xFFFFFFFFu, 500), 100);
  EXPECT_EQ(percentOf(0x7FFFFFFFu, 0xFFFFFFFFu), 50);
}

}  // namespace
