/**
 * Tests for XtcBookmarks' pure half: the on-disk format and the toggle logic.
 *
 * The format is what a user's bookmarks live in across firmware updates, so the
 * byte layout is pinned explicitly rather than only round-tripped -- a
 * round-trip test passes happily even if both sides change together.
 *
 * load()/save() are excluded (XTC_BOOKMARKS_HOST_TEST) because they need
 * HalStorage; what they wrap is exactly what is tested here.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "XtcBookmarks.h"

namespace {

TEST(XtcBookmarks, EncodesTheDocumentedByteLayout) {
  const std::vector<uint32_t> pages = {0, 1, 300};
  uint8_t buffer[XtcBookmarks::MAX_ENCODED_SIZE] = {};

  const size_t written = XtcBookmarks::encode(pages, buffer, sizeof(buffer));
  ASSERT_EQ(written, XtcBookmarks::encodedSize(3));

  EXPECT_EQ(buffer[0], XtcBookmarks::BOOKMARKS_VERSION);
  EXPECT_EQ(buffer[1], 3);
  // Little-endian, four bytes each, in order.
  EXPECT_EQ(buffer[2], 0x00);
  EXPECT_EQ(buffer[6], 0x01);
  EXPECT_EQ(buffer[10], 300 & 0xFF);
  EXPECT_EQ(buffer[11], (300 >> 8) & 0xFF);
}

TEST(XtcBookmarks, RoundTrips) {
  const std::vector<uint32_t> pages = {0, 7, 42, 1000, 65535};
  uint8_t buffer[XtcBookmarks::MAX_ENCODED_SIZE] = {};
  const size_t written = XtcBookmarks::encode(pages, buffer, sizeof(buffer));
  ASSERT_GT(written, 0u);

  std::vector<uint32_t> decoded;
  ASSERT_TRUE(XtcBookmarks::decode(buffer, written, decoded));
  EXPECT_EQ(decoded, pages);
}

TEST(XtcBookmarks, RoundTripsAnEmptyList) {
  uint8_t buffer[XtcBookmarks::MAX_ENCODED_SIZE] = {};
  const size_t written = XtcBookmarks::encode({}, buffer, sizeof(buffer));
  ASSERT_EQ(written, 2u);

  std::vector<uint32_t> decoded{99};  // must be cleared
  EXPECT_TRUE(XtcBookmarks::decode(buffer, written, decoded));
  EXPECT_TRUE(decoded.empty());
}

TEST(XtcBookmarks, RejectsAWrongVersionWithoutCrashing) {
  uint8_t buffer[XtcBookmarks::MAX_ENCODED_SIZE] = {};
  XtcBookmarks::encode({1, 2}, buffer, sizeof(buffer));
  buffer[0] = XtcBookmarks::BOOKMARKS_VERSION + 1;

  std::vector<uint32_t> decoded{5};
  EXPECT_FALSE(XtcBookmarks::decode(buffer, sizeof(buffer), decoded));
  EXPECT_TRUE(decoded.empty()) << "a rejected file must not leave stale entries behind";
}

TEST(XtcBookmarks, RejectsTruncatedData) {
  // The count byte is attacker- and corruption-controlled; trusting it would
  // read past the end of the caller's buffer.
  uint8_t buffer[XtcBookmarks::MAX_ENCODED_SIZE] = {};
  const size_t written = XtcBookmarks::encode({1, 2, 3}, buffer, sizeof(buffer));

  std::vector<uint32_t> decoded;
  EXPECT_FALSE(XtcBookmarks::decode(buffer, written - 1, decoded));
  EXPECT_FALSE(XtcBookmarks::decode(buffer, 1, decoded));
  EXPECT_FALSE(XtcBookmarks::decode(nullptr, 10, decoded));

  buffer[1] = 200;  // count claims far more than the buffer holds
  EXPECT_FALSE(XtcBookmarks::decode(buffer, written, decoded));
}

TEST(XtcBookmarks, EncodeRefusesAnUndersizedBuffer) {
  uint8_t small[4] = {};
  EXPECT_EQ(XtcBookmarks::encode({1, 2, 3}, small, sizeof(small)), 0u);
  EXPECT_EQ(XtcBookmarks::encode({1}, nullptr, 100), 0u);
}

TEST(XtcBookmarks, ToggleAddsRemovesAndStaysSorted) {
  std::vector<uint32_t> pages;

  EXPECT_TRUE(XtcBookmarks::toggle(pages, 10));
  EXPECT_TRUE(XtcBookmarks::toggle(pages, 3));
  EXPECT_TRUE(XtcBookmarks::toggle(pages, 7));
  // Sorted order is what lets contains() binary-search and the list render in
  // page order without a separate sort.
  EXPECT_EQ(pages, (std::vector<uint32_t>{3, 7, 10}));

  EXPECT_FALSE(XtcBookmarks::toggle(pages, 7)) << "toggling an existing page removes it";
  EXPECT_EQ(pages, (std::vector<uint32_t>{3, 10}));
}

TEST(XtcBookmarks, ContainsTracksToggle) {
  std::vector<uint32_t> pages;
  EXPECT_FALSE(XtcBookmarks::contains(pages, 5));

  XtcBookmarks::toggle(pages, 5);
  EXPECT_TRUE(XtcBookmarks::contains(pages, 5));

  XtcBookmarks::toggle(pages, 5);
  EXPECT_FALSE(XtcBookmarks::contains(pages, 5));
}

TEST(XtcBookmarks, PageZeroIsBookmarkable) {
  // Page 0 is the cover, and an off-by-one that treated 0 as "no bookmark"
  // would make the first page the one page you cannot mark.
  std::vector<uint32_t> pages;
  EXPECT_TRUE(XtcBookmarks::toggle(pages, 0));
  EXPECT_TRUE(XtcBookmarks::contains(pages, 0));
  EXPECT_EQ(pages.size(), 1u);
}

TEST(XtcBookmarks, RefusesToExceedTheCapButStillAllowsRemoval) {
  std::vector<uint32_t> pages;
  for (uint32_t i = 0; i < XtcBookmarks::MAX_BOOKMARKS; i++) {
    ASSERT_TRUE(XtcBookmarks::toggle(pages, i)) << "at " << i;
  }
  EXPECT_EQ(pages.size(), XtcBookmarks::MAX_BOOKMARKS);

  // A full list refuses new pages rather than evicting one: a bookmark
  // vanishing later is worse than one that visibly failed to take.
  EXPECT_FALSE(XtcBookmarks::toggle(pages, 9999));
  EXPECT_EQ(pages.size(), XtcBookmarks::MAX_BOOKMARKS);
  EXPECT_FALSE(XtcBookmarks::contains(pages, 9999));

  // Removal must still work when full, or the list becomes unrecoverable.
  EXPECT_FALSE(XtcBookmarks::toggle(pages, 0));
  EXPECT_EQ(pages.size(), XtcBookmarks::MAX_BOOKMARKS - 1);
}

TEST(XtcBookmarks, AFullListStillEncodes) {
  std::vector<uint32_t> pages;
  for (uint32_t i = 0; i < XtcBookmarks::MAX_BOOKMARKS; i++) {
    pages.push_back(i);
  }
  uint8_t buffer[XtcBookmarks::MAX_ENCODED_SIZE] = {};
  const size_t written = XtcBookmarks::encode(pages, buffer, sizeof(buffer));
  EXPECT_EQ(written, XtcBookmarks::MAX_ENCODED_SIZE);

  std::vector<uint32_t> decoded;
  ASSERT_TRUE(XtcBookmarks::decode(buffer, written, decoded));
  EXPECT_EQ(decoded, pages);
}

}  // namespace
