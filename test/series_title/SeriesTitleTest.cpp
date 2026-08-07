/**
 * Tests for SeriesTitle, which splits "Berserk v03" into a series and a volume.
 *
 * Two things depend on this: the volume badge drawn on each shelf cover, and
 * collapsing a run of volumes into one shelf entry. A false positive is the
 * expensive failure -- it merges two unrelated books into one series and hides
 * one of them from the home screen -- so most of these pin the cases that must
 * NOT parse as a volume.
 */

#include <gtest/gtest.h>

#include "SeriesTitle.h"

namespace {

using SeriesTitle::parse;

void expectParse(const char* input, const char* series, int volume) {
  const auto p = parse(input);
  EXPECT_EQ(p.series, series) << "input: " << input;
  EXPECT_EQ(p.volume, volume) << "input: " << input;
}

TEST(SeriesTitle, ParsesTheCommonVolumeMarkers) {
  expectParse("Berserk v03", "Berserk", 3);
  expectParse("Berserk V03", "Berserk", 3);
  expectParse("Berserk v3", "Berserk", 3);
  expectParse("Berserk vol 3", "Berserk", 3);
  expectParse("Berserk Vol.3", "Berserk", 3);
  expectParse("Berserk Volume 3", "Berserk", 3);
  expectParse("Berserk #3", "Berserk", 3);
  expectParse("Berserk - 03", "Berserk", 3);
}

TEST(SeriesTitle, StripsFileExtensions) {
  expectParse("Berserk v03.xtc", "Berserk", 3);
  expectParse("Berserk v03.xtch", "Berserk", 3);
  expectParse("Berserk v03.cbz", "Berserk", 3);
  expectParse("Berserk.epub", "Berserk", SeriesTitle::Parsed::NO_VOLUME);
}

TEST(SeriesTitle, KeepsNumbersThatArePartOfTheSeriesName) {
  // The marker is taken from the LAST match, so a leading number stays put.
  expectParse("20th Century Boys v05", "20th Century Boys", 5);
  expectParse("Zone 00 v02", "Zone 00", 2);
  expectParse("Blame! v01", "Blame!", 1);
}

TEST(SeriesTitle, DoesNotInventVolumesFromOrdinaryTitles) {
  // The expensive failure mode: a false positive merges unrelated books.
  // "Fahrenheit 451" is three digits and not zero-padded, so it is rejected.
  expectParse("Fahrenheit 451", "Fahrenheit 451", SeriesTitle::Parsed::NO_VOLUME);
  expectParse("Berserk", "Berserk", SeriesTitle::Parsed::NO_VOLUME);
  expectParse("Nausicaa of the Valley of the Wind", "Nausicaa of the Valley of the Wind",
              SeriesTitle::Parsed::NO_VOLUME);
}

TEST(SeriesTitle, AcceptsBareNumbersOnlyWhenPaddedOrShort) {
  expectParse("Gantz 01", "Gantz", 1);   // zero-padded
  expectParse("Akira 3", "Akira", 3);    // single digit
  expectParse("Akira 12", "Akira", 12);  // two digits
  // Three unpadded digits is far more likely to be part of the title.
  expectParse("Apollo 440", "Apollo 440", SeriesTitle::Parsed::NO_VOLUME);
}

TEST(SeriesTitle, RejectsImplausiblyLargeVolumes) {
  // Guards against a year or an ISBN fragment being read as a volume.
  expectParse("Berserk v1997", "Berserk v1997", SeriesTitle::Parsed::NO_VOLUME);
  expectParse("Some Title 20240101", "Some Title 20240101", SeriesTitle::Parsed::NO_VOLUME);
}

TEST(SeriesTitle, KeepsTheNameWhenThereIsNothingButAMarker) {
  // Nothing left to call a series, so the UI gets the original string rather
  // than an empty entry it would have to special-case.
  expectParse("v03", "v03", SeriesTitle::Parsed::NO_VOLUME);
  expectParse("", "", SeriesTitle::Parsed::NO_VOLUME);
}

TEST(SeriesTitle, DoesNotMatchAVolumeKeywordInsideAWord) {
  // "v" must start a token, or "Love 3" would parse as volume 3 of "Lo".
  expectParse("Love", "Love", SeriesTitle::Parsed::NO_VOLUME);
  expectParse("Vinland Saga v02", "Vinland Saga", 2);
}

TEST(SeriesTitle, TrimsSeparatorsFromTheSeries) {
  expectParse("Berserk - v03", "Berserk", 3);
  expectParse("Berserk_v03", "Berserk", 3);
  expectParse("Berserk  v03", "Berserk", 3);
}

TEST(SeriesTitle, BadgeIsZeroPaddedToTwoDigits) {
  EXPECT_EQ(SeriesTitle::badge(3), "v03");
  EXPECT_EQ(SeriesTitle::badge(12), "v12");
  EXPECT_EQ(SeriesTitle::badge(120), "v120");
  EXPECT_EQ(SeriesTitle::badge(SeriesTitle::Parsed::NO_VOLUME), "");
}

TEST(SeriesTitle, SameSeriesGroupsVolumesAndSeparatesEverythingElse) {
  EXPECT_TRUE(SeriesTitle::sameSeries("Berserk v01", "Berserk v02"));
  EXPECT_TRUE(SeriesTitle::sameSeries("berserk v01", "BERSERK v09"));  // case-insensitive
  EXPECT_TRUE(SeriesTitle::sameSeries("Berserk v01.xtc", "Berserk - 02.cbz"));

  EXPECT_FALSE(SeriesTitle::sameSeries("Berserk v01", "Vagabond v01"));
  EXPECT_FALSE(SeriesTitle::sameSeries("Berserk v01", "Berserk Deluxe v01"));
}

TEST(SeriesTitle, UnparseableNamesDoNotAllCollapseTogether) {
  // Two books with no volume marker are still two books. If sameSeries treated
  // "no series" as a match, an entire mixed library would fold into one entry.
  EXPECT_FALSE(SeriesTitle::sameSeries("", ""));
  EXPECT_TRUE(SeriesTitle::sameSeries("Akira", "Akira"));
  EXPECT_FALSE(SeriesTitle::sameSeries("Akira", "Dorohedoro"));
}

}  // namespace
