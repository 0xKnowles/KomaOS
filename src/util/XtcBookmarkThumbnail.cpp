#include "XtcBookmarkThumbnail.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>
#include <Xtc/XthPixels.h>

#include <algorithm>
#include <cstring>

#include "activities/reader/ProgressFile.h"

namespace XtcBookmarkThumbnail {

namespace {

std::string thumbFileName(const uint32_t page) { return "bookmark_" + std::to_string(page) + ".bin"; }

std::string thumbPath(const std::string& cachePath, const uint32_t page) {
  return cachePath + "/" + thumbFileName(page);
}

}  // namespace

bool generate(const std::string& cachePath, const uint32_t page, const Xtc& xtc) {
  const uint16_t srcWidth = xtc.getPageWidth();
  const uint16_t srcHeight = xtc.getPageHeight();
  const uint8_t bitDepth = xtc.getBitDepth();
  if (srcWidth == 0 || srcHeight == 0) {
    return false;
  }

  const size_t pageBufferSize =
      (bitDepth == 2) ? xtc::XthPage::payloadSizeFor(srcWidth, srcHeight) : xtc::xtgPayloadSizeFor(srcWidth, srcHeight);
  auto pageBuffer = makeUniqueNoThrow<uint8_t[]>(pageBufferSize);
  if (!pageBuffer) {
    LOG_ERR("XBT", "OOM: %lu byte page buffer", static_cast<unsigned long>(pageBufferSize));
    return false;
  }
  if (xtc.loadPage(page, pageBuffer.get(), pageBufferSize) == 0) {
    LOG_ERR("XBT", "Failed to load page %lu for thumbnail", static_cast<unsigned long>(page));
    return false;
  }

  auto thumb = makeUniqueNoThrow<uint8_t[]>(SIZE);
  if (!thumb) {
    LOG_ERR("XBT", "OOM: %lu byte thumbnail buffer", static_cast<unsigned long>(SIZE));
    return false;
  }
  memset(thumb.get(), 0xFF, SIZE);  // all white (bit=1) until ink is plotted below

  const xtc::XthPage xth(pageBuffer.get(), srcWidth, srcHeight);
  const size_t xtgRowBytes = (static_cast<size_t>(srcWidth) + 7) / 8;

  // Ink level 0..3 at a stored-page pixel, whichever bit depth -- same shape as
  // XtcReaderActivity::renderFullPage's levelAt, duplicated rather than shared
  // since the two operate on different buffers (a single page vs a reassembled
  // strip run) and sharing would mean threading strip-layout state through a
  // function that has none here.
  const auto levelAt = [&](const int sx, const int sy) -> int {
    if (bitDepth == 2) {
      return xth.levelAt(static_cast<uint16_t>(sx), static_cast<uint16_t>(sy));
    }
    const size_t byte = static_cast<size_t>(sy) * xtgRowBytes + static_cast<size_t>(sx) / 8;
    return ((pageBuffer[byte] >> (7 - (sx % 8))) & 1) ? 0 : 3;  // XTG: bit set = white
  };

  for (int dy = 0; dy < HEIGHT; dy++) {
    const int srcY0 = dy * srcHeight / HEIGHT;
    const int srcY1 = std::max(srcY0 + 1, (dy + 1) * srcHeight / HEIGHT);
    for (int dx = 0; dx < WIDTH; dx++) {
      const int srcX0 = dx * srcWidth / WIDTH;
      const int srcX1 = std::max(srcX0 + 1, (dx + 1) * srcWidth / WIDTH);

      int total = 0;
      int samples = 0;
      for (int sy = srcY0; sy < srcY1 && sy < srcHeight; sy++) {
        for (int sx = srcX0; sx < srcX1 && sx < srcWidth; sx++) {
          total += levelAt(sx, sy);
          samples++;
        }
      }
      if (samples == 0) {
        continue;
      }
      // Majority-ink threshold: no dithering at 40x60 -- this is for
      // recognizing a page at a glance, not reproducing it.
      if ((total / samples) >= 2) {
        const size_t byteIdx = static_cast<size_t>(dy) * ROW_BYTES + static_cast<size_t>(dx) / 8;
        thumb[byteIdx] &= static_cast<uint8_t>(~(1 << (7 - (dx % 8))));
      }
    }
  }

  return ProgressFile::writeAtomicNamed(cachePath, thumbFileName(page).c_str(), thumb.get(), SIZE);
}

void remove(const std::string& cachePath, const uint32_t page) { Storage.remove(thumbPath(cachePath, page).c_str()); }

bool load(const std::string& cachePath, const uint32_t page, uint8_t* buffer, const size_t bufferSize) {
  if (buffer == nullptr || bufferSize < SIZE) {
    return false;
  }
  HalFile file;
  if (!Storage.openFileForRead("XBT", thumbPath(cachePath, page), file)) {
    return false;
  }
  return file.read(buffer, SIZE) == static_cast<int>(SIZE);
}

}  // namespace XtcBookmarkThumbnail
