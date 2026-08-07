/**
 * SeriesTitle.h
 *
 * Splits a manga filename or title into series name and volume number.
 *
 * A manga library is a pile of "Series vNN" files. Recognising that shape is
 * what lets the shelf badge a cover with its volume, and collapse a run of
 * volumes into a single entry for the series. Nothing here touches the SD card
 * -- it is pure string work on names the caller already has, which is also why
 * it can be unit tested on the host (test/series_title).
 *
 * Recognised volume markers, case-insensitive, taken from the LAST match so a
 * series whose own name contains a number still parses:
 *
 *   Berserk v03            Berserk Vol.3         Berserk Volume 3
 *   Berserk V03            Berserk vol 3         Berserk #3
 *   Berserk - 03           20th Century Boys v05
 *
 * A bare trailing number is only accepted when it is zero-padded or at most two
 * digits. Without that guard "Fahrenheit 451" parses as volume 451 of a series
 * called "Fahrenheit", which is exactly the kind of thing that would quietly
 * merge unrelated books on the shelf.
 */

#pragma once

#include <string>
#include <string_view>

namespace SeriesTitle {

/** Largest volume number treated as plausible. */
constexpr int MAX_VOLUME = 999;

struct Parsed {
  /** Series name with the volume marker and any trailing separator removed. */
  std::string series;
  /** Volume number, or NO_VOLUME when the name carries no recognisable marker. */
  int volume;

  static constexpr int NO_VOLUME = -1;

  bool hasVolume() const { return volume != NO_VOLUME; }
};

/**
 * Parses `name`, which may be a bare title or a filename with an extension.
 *
 * Always returns a non-empty series as long as `name` is non-empty: a name with
 * no recognisable volume marker parses as the whole name with NO_VOLUME.
 */
Parsed parse(std::string_view name);

/** Formats a volume for display, zero-padded to two digits: 3 -> "v03". */
std::string badge(int volume);

/**
 * True when two names belong to the same series.
 *
 * Compares the parsed series names case-insensitively. Names that parse to an
 * empty series never match, so unparseable entries stay separate rather than
 * all collapsing together.
 */
bool sameSeries(std::string_view a, std::string_view b);

}  // namespace SeriesTitle
