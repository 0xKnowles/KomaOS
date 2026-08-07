/**
 * XthPixels.h
 *
 * Pixel addressing for XTH (2-bit grayscale) page payloads.
 *
 * XTH stores a page as two bitplanes laid out column-major with the columns
 * reversed, which is how the panel is wired. That addressing was previously
 * open-coded in the reader activity; keeping it here means the format is
 * described in one place, next to the rest of the XTC format code, and can be
 * unit tested on the host (test/xth_pixels).
 *
 * Layout, for a width x height page:
 *   colBytes  = (height + 7) / 8
 *   planeSize = colBytes * width
 *   plane1    = payload[0 .. planeSize)           bit 1 (the high bit)
 *   plane2    = payload[planeSize .. 2*planeSize) bit 0 (the low bit)
 *
 * Column x lives at column index (width - 1 - x); within a column, 8 vertical
 * pixels share a byte with the topmost pixel in the most significant bit.
 *
 * value = (bit1 << 1) | bit2, where 0=White, 1=DarkGrey, 2=LightGrey, 3=Black.
 *
 * NOTE ON planeSize: each column is padded to a whole number of bytes, so a
 * plane is colBytes*width, NOT the ceil(width*height/8) that the parser and the
 * reader previously computed. Those two agree only when height is a multiple of
 * 8. Every page produced so far is 800 tall, so they have always agreed in
 * practice -- but for, say, a 53-tall page the old formula under-counts by 12
 * bytes per plane and the addressing then runs off the end of the buffer. The
 * encoders are the authority here: both xtcjs and FlipNzb's encodeXth allocate
 * `colBytes * width * 2`.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace xtc {

/** The four levels an XTH pixel can take. */
enum class XthLevel : uint8_t {
  White = 0,
  DarkGrey = 1,
  LightGrey = 2,
  Black = 3,
};

/**
 * Bitmask over XthLevel values, for "act on these levels" queries.
 *
 * Bit N set means level N is included, so a caller tests one mask rather than
 * chaining equality comparisons per pixel.
 */
namespace XthMask {
constexpr uint8_t WHITE = 1 << 0;
constexpr uint8_t DARK_GREY = 1 << 1;
constexpr uint8_t LIGHT_GREY = 1 << 2;
constexpr uint8_t BLACK = 1 << 3;
/** Everything that leaves ink: DarkGrey, LightGrey, Black. */
constexpr uint8_t NON_WHITE = DARK_GREY | LIGHT_GREY | BLACK;
/** The two intermediate levels the grayscale LUT passes act on. */
constexpr uint8_t ANY_GREY = DARK_GREY | LIGHT_GREY;
}  // namespace XthMask

/**
 * Read-only view over an XTH page payload.
 *
 * Holds a borrowed pointer; the payload must outlive the view. Does no bounds
 * checking on x/y -- callers iterate over the dimensions they constructed it
 * with.
 */
class XthPage {
  const uint8_t* plane1;
  const uint8_t* plane2;
  uint16_t width;
  uint16_t height;
  size_t colBytes;

 public:
  XthPage(const uint8_t* payload, const uint16_t width, const uint16_t height)
      : plane1(payload),
        plane2(payload + planeSizeFor(width, height)),
        width(width),
        height(height),
        colBytes((height + 7) / 8) {}

  /** Bytes one payload plane occupies. Two planes follow each other. */
  static constexpr size_t planeSizeFor(const uint16_t width, const uint16_t height) {
    return static_cast<size_t>((height + 7) / 8) * width;
  }

  /** Total payload bytes for a width x height XTH page. */
  static constexpr size_t payloadSizeFor(const uint16_t width, const uint16_t height) {
    return planeSizeFor(width, height) * 2;
  }

  uint16_t getWidth() const { return width; }
  uint16_t getHeight() const { return height; }

  /**
   * Byte offset of column x within a plane.
   *
   * Hoist this out of a per-pixel loop: it is the only multiply in the
   * addressing, and a full-page pass runs width*height times.
   */
  size_t columnBase(const uint16_t x) const { return static_cast<size_t>(width - 1 - x) * colBytes; }

  /** Level of the pixel at row y of the column starting at `colBase`. */
  uint8_t levelInColumn(const size_t colBase, const uint16_t y) const {
    const size_t byteOffset = colBase + (y >> 3);
    const uint8_t bitInByte = 7 - (y & 7);
    const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
    const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
    return static_cast<uint8_t>((bit1 << 1) | bit2);
  }

  /** Level of the pixel at (x, y). Convenience for non-hot paths. */
  uint8_t levelAt(const uint16_t x, const uint16_t y) const { return levelInColumn(columnBase(x), y); }

  /** True when the pixel's level is one of those selected by `mask`. */
  static bool matches(const uint8_t mask, const uint8_t level) { return ((mask >> level) & 1) != 0; }
};

/**
 * Payload bytes for a width x height XTG (1-bit) page.
 *
 * XTG is row-major with each row padded to a whole byte, so unlike XTH this is
 * simply rowBytes*height. Lives here so both page formats are sized from one
 * place.
 */
constexpr size_t xtgPayloadSizeFor(const uint16_t width, const uint16_t height) {
  return static_cast<size_t>((width + 7) / 8) * height;
}

}  // namespace xtc
