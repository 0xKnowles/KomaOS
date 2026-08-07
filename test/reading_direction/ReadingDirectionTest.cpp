/**
 * Tests for xtc::isRightToLeft, which decides whether an XTC book's page turns
 * run right-to-left.
 *
 * The decision combines a user setting with the file's own readDirection byte,
 * and getting it backwards silently reverses every page turn in the reader.
 * These pin the truth table, the out-of-range fallback, and — importantly — the
 * "changes nothing today" property that makes the default safe to ship.
 */

#include <Xtc/ReadingDirection.h>
#include <gtest/gtest.h>

#include <cstdint>

namespace {

constexpr auto AUTO = static_cast<uint8_t>(xtc::DirectionPreference::Auto);
constexpr auto LTR = static_cast<uint8_t>(xtc::DirectionPreference::LeftToRight);
constexpr auto RTL = static_cast<uint8_t>(xtc::DirectionPreference::RightToLeft);

/** What every encoder in the wild writes into the header today. */
constexpr uint8_t HEADER_DEFAULT = 0;

TEST(ReadingDirection, AutoFollowsTheFileHeader) {
  EXPECT_FALSE(xtc::isRightToLeft(AUTO, HEADER_DEFAULT));
  EXPECT_TRUE(xtc::isRightToLeft(AUTO, xtc::HEADER_READ_DIRECTION_RTL));
}

TEST(ReadingDirection, ExplicitPreferenceOverridesTheFile) {
  // The escape hatch: if a converter numbers readDirection differently from
  // what we infer, the user can still force either direction.
  EXPECT_FALSE(xtc::isRightToLeft(LTR, xtc::HEADER_READ_DIRECTION_RTL));
  EXPECT_TRUE(xtc::isRightToLeft(RTL, HEADER_DEFAULT));

  // And it stays honoured regardless of what the header says.
  for (uint8_t header = 0; header < 4; header++) {
    EXPECT_FALSE(xtc::isRightToLeft(LTR, header)) << "header=" << static_cast<int>(header);
    EXPECT_TRUE(xtc::isRightToLeft(RTL, header)) << "header=" << static_cast<int>(header);
  }
}

TEST(ReadingDirection, DefaultsToNoChangeForEveryFileInTheWild) {
  // xtcjs and FlipNzb both write readDirection = 0. With the setting at its
  // default (Auto), every existing volume must keep turning left-to-right --
  // this is what makes shipping the setting a no-op until someone opts in.
  EXPECT_FALSE(xtc::isRightToLeft(AUTO, HEADER_DEFAULT));
  EXPECT_EQ(AUTO, 0) << "Auto must be the zero value so a settings file that predates this key defaults to it";
}

TEST(ReadingDirection, UnknownHeaderValuesAreTreatedAsLeftToRight) {
  // Only value 1 is understood to mean right-to-left. Anything else -- a
  // vertical-writing flag, junk, a value from a future encoder -- must not
  // silently reverse the reader.
  for (uint8_t header = 0; header < 255; header++) {
    if (header == xtc::HEADER_READ_DIRECTION_RTL) continue;
    EXPECT_FALSE(xtc::isRightToLeft(AUTO, header)) << "header=" << static_cast<int>(header);
  }
}

TEST(ReadingDirection, OutOfRangePreferenceFallsBackToAuto) {
  // A settings.json written by a newer build, or a corrupted one, must not be
  // trusted into an undefined branch.
  for (uint8_t pref = static_cast<uint8_t>(xtc::DirectionPreference::Count); pref < 255; pref++) {
    EXPECT_FALSE(xtc::isRightToLeft(pref, HEADER_DEFAULT)) << "pref=" << static_cast<int>(pref);
    EXPECT_TRUE(xtc::isRightToLeft(pref, xtc::HEADER_READ_DIRECTION_RTL)) << "pref=" << static_cast<int>(pref);
  }
}

TEST(ReadingDirection, PreferenceValuesMatchTheSettingsEnum) {
  // KomaSettings::MANGA_READING_DIRECTION duplicates these values so that
  // src/ does not have to include the Xtc library's headers. The values are
  // persisted in settings.json, so a divergence would silently reinterpret
  // saved preferences. KomaSettings.h cannot be included here (it pulls in the
  // Arduino toolchain), so the numbering is asserted directly.
  EXPECT_EQ(static_cast<uint8_t>(xtc::DirectionPreference::Auto), 0);
  EXPECT_EQ(static_cast<uint8_t>(xtc::DirectionPreference::LeftToRight), 1);
  EXPECT_EQ(static_cast<uint8_t>(xtc::DirectionPreference::RightToLeft), 2);
  EXPECT_EQ(static_cast<uint8_t>(xtc::DirectionPreference::Count), 3);
}

TEST(ReadingDirection, IsUsableInAConstexprContext) {
  // Keeping it constexpr means the LTR/RTL branches fold away wherever the
  // preference is known at compile time, and costs nothing where it is not.
  static_assert(!xtc::isRightToLeft(LTR, xtc::HEADER_READ_DIRECTION_RTL));
  static_assert(xtc::isRightToLeft(RTL, HEADER_DEFAULT));
  static_assert(xtc::isRightToLeft(AUTO, xtc::HEADER_READ_DIRECTION_RTL));
  SUCCEED();
}

}  // namespace
