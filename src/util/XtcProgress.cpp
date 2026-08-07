#include "XtcProgress.h"

// The pure encode/decode half is host-testable and must stay free of firmware
// headers; only the path/read/write shell below touches storage.
#ifndef XTC_PROGRESS_HOST_TEST
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <functional>

#include "activities/reader/ProgressFile.h"
#endif

namespace XtcProgress {

int Snapshot::percent() const {
  if (!hasPageCount()) {
    return UNKNOWN_PERCENT;
  }
  // Clamp before multiplying: a page index past the end (a volume re-encoded
  // shorter than when it was last read) would otherwise report over 100.
  const uint32_t pagesRead = page >= pageCount ? pageCount : page + 1;
  return static_cast<int>((static_cast<uint64_t>(pagesRead) * 100) / pageCount);
}

size_t encode(const uint32_t page, const uint32_t pageCount, uint8_t* out, const size_t outSize) {
  if (out == nullptr || outSize < CURRENT_SIZE) {
    return 0;
  }
  // Byte-wise rather than memcpy of a uint32: the file is little-endian by
  // definition, independent of whatever the build target happens to be.
  out[0] = static_cast<uint8_t>(page & 0xFF);
  out[1] = static_cast<uint8_t>((page >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((page >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((page >> 24) & 0xFF);
  out[4] = static_cast<uint8_t>(pageCount & 0xFF);
  out[5] = static_cast<uint8_t>((pageCount >> 8) & 0xFF);
  out[6] = static_cast<uint8_t>((pageCount >> 16) & 0xFF);
  out[7] = static_cast<uint8_t>((pageCount >> 24) & 0xFF);
  return CURRENT_SIZE;
}

Snapshot decode(const uint8_t* data, const size_t size) {
  Snapshot snapshot;
  if (data == nullptr || size < LEGACY_SIZE) {
    return snapshot;
  }

  snapshot.page = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                  (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
  snapshot.valid = true;

  if (size >= CURRENT_SIZE) {
    snapshot.pageCount = static_cast<uint32_t>(data[4]) | (static_cast<uint32_t>(data[5]) << 8) |
                         (static_cast<uint32_t>(data[6]) << 16) | (static_cast<uint32_t>(data[7]) << 24);
  }
  return snapshot;
}

#ifndef XTC_PROGRESS_HOST_TEST

std::string cachePathFor(const std::string& bookPath) {
  if (!FsHelpers::hasXtcExtension(bookPath)) {
    return {};
  }
  // Must stay in step with Xtc's constructor (Xtc.h:32) -- same base directory,
  // same "xtc_" prefix, same hash of the full path.
  return "/.komaos/xtc_" + std::to_string(std::hash<std::string>{}(bookPath));
}

Snapshot read(const std::string& cachePath) {
  if (cachePath.empty()) {
    return {};
  }

  HalFile file;
  if (!Storage.openFileForRead("XPR", cachePath + "/progress.bin", file)) {
    return {};  // A book that has never been opened is the normal case.
  }

  uint8_t buffer[CURRENT_SIZE];
  const size_t bytesRead = file.read(buffer, sizeof(buffer));
  return decode(buffer, bytesRead);
}

bool write(const std::string& cachePath, const uint32_t page, const uint32_t pageCount) {
  uint8_t buffer[CURRENT_SIZE];
  const size_t written = encode(page, pageCount, buffer, sizeof(buffer));
  if (written == 0) {
    LOG_ERR("XPR", "Failed to encode progress: page %lu/%lu", static_cast<unsigned long>(page),
            static_cast<unsigned long>(pageCount));
    return false;
  }
  return ProgressFile::writeAtomic(cachePath, buffer, written);
}

#endif  // XTC_PROGRESS_HOST_TEST

}  // namespace XtcProgress
