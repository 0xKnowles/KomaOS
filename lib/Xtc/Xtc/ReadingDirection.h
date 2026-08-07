/**
 * ReadingDirection.h
 *
 * Resolves which way page turns run for an XTC book.
 *
 * Manga is drawn to be read right-to-left: the reader advances towards the
 * spine, so the control that means "forward" is the one on the left. A volume
 * converted from a Japanese source therefore wants its page-turn inputs
 * mirrored relative to an English EPUB.
 *
 * Two things feed the decision:
 *
 *  - The XTC header carries a readDirection byte at offset 0x08 (XtcTypes.h).
 *    It has been parsed into the header struct since the format was added here
 *    but never read back, so it has had no effect on anything.
 *  - The user's preference, which can override the file either way.
 *
 * On the header byte: every encoder we know of -- xtcjs, and FlipNzb's
 * buildXtc -- writes 0 there today, so AUTO resolves to left-to-right for every
 * file currently in the wild and this setting changes nothing until a converter
 * starts populating it. That also means the meaning of the non-zero values is
 * inferred rather than confirmed: 1 is taken as right-to-left. If a converter
 * turns up that numbers them differently, the explicit LeftToRight and
 * RightToLeft preferences are the escape hatch, and only the constant below
 * needs revisiting.
 */

#pragma once

#include <cstdint>

namespace xtc {

/**
 * What the user asked for. Values are persisted in settings.json via
 * KomaSettings::mangaReadingDirection, so do not renumber them.
 */
enum class DirectionPreference : uint8_t {
  /** Follow the file's own readDirection byte. */
  Auto = 0,
  LeftToRight = 1,
  RightToLeft = 2,
  Count,
};

/** The XTC header readDirection value understood to mean right-to-left. */
constexpr uint8_t HEADER_READ_DIRECTION_RTL = 1;

/**
 * True when page turns should run right-to-left.
 *
 * `preference` is a DirectionPreference; an out-of-range value (a settings file
 * written by a newer build, or a corrupt one) falls back to Auto rather than
 * being trusted.
 */
constexpr bool isRightToLeft(const uint8_t preference, const uint8_t headerReadDirection) {
  switch (static_cast<DirectionPreference>(preference)) {
    case DirectionPreference::LeftToRight:
      return false;
    case DirectionPreference::RightToLeft:
      return true;
    case DirectionPreference::Auto:
    case DirectionPreference::Count:
    default:
      return headerReadDirection == HEADER_READ_DIRECTION_RTL;
  }
}

}  // namespace xtc
