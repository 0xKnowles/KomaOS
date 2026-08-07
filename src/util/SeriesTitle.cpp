#include "SeriesTitle.h"

#include <cctype>
#include <cstdio>

namespace SeriesTitle {

namespace {

char lower(const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool isDigit(const char c) { return c >= '0' && c <= '9'; }

/** Separators that sit between a series name and its volume marker. */
bool isSeparator(const char c) { return c == ' ' || c == '-' || c == '_' || c == '.' || c == ',' || c == '\t'; }

/** Drops a trailing file extension, but only a short alphanumeric one. */
std::string_view stripExtension(std::string_view name) {
  const size_t dot = name.find_last_of('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 >= name.size()) {
    return name;
  }
  const std::string_view ext = name.substr(dot + 1);
  // "Vol.3" must not lose its number to extension stripping, and a series like
  // "R.O.D" must keep its dots, so require a plausible extension: 2-4 letters.
  if (ext.size() < 2 || ext.size() > 4) {
    return name;
  }
  for (const char c : ext) {
    if (!std::isalpha(static_cast<unsigned char>(c))) {
      return name;
    }
  }
  return name.substr(0, dot);
}

std::string_view trimTrailingSeparators(std::string_view s) {
  while (!s.empty() && isSeparator(s.back())) {
    s.remove_suffix(1);
  }
  return s;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && isSeparator(s.front())) {
    s.remove_prefix(1);
  }
  return trimTrailingSeparators(s);
}

/** Reads digits at `pos`; returns false when there are none or too many. */
bool readNumber(std::string_view s, size_t pos, int& valueOut, size_t& digitsOut) {
  size_t digits = 0;
  int value = 0;
  while (pos + digits < s.size() && isDigit(s[pos + digits])) {
    value = value * 10 + (s[pos + digits] - '0');
    digits++;
    if (value > MAX_VOLUME) {
      return false;
    }
  }
  if (digits == 0) {
    return false;
  }
  valueOut = value;
  digitsOut = digits;
  return true;
}

/** Matches `prefix` case-insensitively at `pos`. */
bool matchesAt(std::string_view s, size_t pos, std::string_view prefix) {
  if (pos + prefix.size() > s.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); i++) {
    if (lower(s[pos + i]) != lower(prefix[i])) {
      return false;
    }
  }
  return true;
}

/**
 * Length of an explicit volume keyword at `pos`, or 0.
 *
 * Longest first: "volume" before "vol" before "v", or "vol" would match the
 * first three characters of "volume" and leave "ume" where a number should be.
 */
size_t volumeKeywordLength(std::string_view s, const size_t pos) {
  for (const std::string_view keyword : {std::string_view("volume"), std::string_view("vol"), std::string_view("v")}) {
    if (matchesAt(s, pos, keyword)) {
      return keyword.size();
    }
  }
  return 0;
}

/** True when `pos` starts a token, i.e. it is the start or follows a separator. */
bool atTokenStart(std::string_view s, const size_t pos) { return pos == 0 || isSeparator(s[pos - 1]); }

}  // namespace

Parsed parse(std::string_view name) {
  const std::string_view base = trim(stripExtension(name));
  Parsed result{std::string(base), Parsed::NO_VOLUME};
  if (base.empty()) {
    return result;
  }

  // Scan forwards keeping the last match, so a number inside the series name
  // ("20th Century Boys v05") loses to the real marker at the end.
  size_t bestStart = std::string_view::npos;
  int bestVolume = Parsed::NO_VOLUME;

  for (size_t i = 0; i < base.size(); i++) {
    int volume = 0;
    size_t digits = 0;

    // "#3"
    if (base[i] == '#' && readNumber(base, i + 1, volume, digits)) {
      bestStart = i;
      bestVolume = volume;
      continue;
    }

    if (!atTokenStart(base, i)) {
      continue;
    }

    // "v3" / "vol 3" / "vol.3" / "volume 3"
    if (const size_t keyword = volumeKeywordLength(base, i); keyword > 0) {
      size_t numberPos = i + keyword;
      while (numberPos < base.size() && isSeparator(base[numberPos])) {
        numberPos++;
      }
      if (readNumber(base, numberPos, volume, digits) && numberPos + digits == base.size()) {
        bestStart = i;
        bestVolume = volume;
        continue;
      }
    }

    // A bare trailing number, e.g. "Gantz 01". Only when zero-padded or at most
    // two digits -- see the header for why "Fahrenheit 451" must not match.
    //
    // Skipped when an earlier match is already in hand: in "Berserk vol 3" the
    // keyword branch above has claimed the whole "vol 3", and letting the bare
    // branch re-match its digits would move the split point and leave "vol"
    // stranded on the end of the series name.
    if (bestStart != std::string_view::npos && bestStart < i) {
      continue;
    }

    if (isDigit(base[i]) && readNumber(base, i, volume, digits) && i + digits == base.size() && i > 0) {
      const bool zeroPadded = digits > 1 && base[i] == '0';
      if (zeroPadded || digits <= 2) {
        bestStart = i;
        bestVolume = volume;
      }
    }
  }

  if (bestStart == std::string_view::npos) {
    return result;
  }

  const std::string_view series = trimTrailingSeparators(base.substr(0, bestStart));
  if (series.empty()) {
    // The whole name was the marker ("v03"); there is no series to speak of, so
    // keep the original rather than returning an empty entry the UI must guard.
    return result;
  }

  result.series = std::string(series);
  result.volume = bestVolume;
  return result;
}

std::string badge(const int volume) {
  if (volume < 0) {
    return {};
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "v%02d", volume > MAX_VOLUME ? MAX_VOLUME : volume);
  return buf;
}

bool sameSeries(std::string_view a, std::string_view b) {
  const Parsed pa = parse(a);
  const Parsed pb = parse(b);
  if (pa.series.empty() || pb.series.empty() || pa.series.size() != pb.series.size()) {
    return false;
  }
  for (size_t i = 0; i < pa.series.size(); i++) {
    if (lower(pa.series[i]) != lower(pb.series[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace SeriesTitle
