#include "XtcBookmarks.h"

#include <algorithm>
#include <cstring>

// The pure encode/decode/toggle half is host-testable and must stay free of
// firmware headers; only the load/save shell below touches storage.
#ifndef XTC_BOOKMARKS_HOST_TEST
#include <HalStorage.h>
#include <Logging.h>

#include "activities/reader/ProgressFile.h"
#endif

namespace XtcBookmarks {

size_t encode(const std::vector<uint32_t>& pages, uint8_t* out, const size_t outSize) {
  if (pages.size() > MAX_BOOKMARKS) {
    return 0;
  }
  const size_t needed = encodedSize(pages.size());
  if (out == nullptr || outSize < needed) {
    return 0;
  }

  out[0] = BOOKMARKS_VERSION;
  out[1] = static_cast<uint8_t>(pages.size());
  size_t offset = 2;
  for (const uint32_t page : pages) {
    // Byte-wise rather than memcpy of a uint32: the file is little-endian by
    // definition, independent of whatever the build target happens to be.
    out[offset++] = static_cast<uint8_t>(page & 0xFF);
    out[offset++] = static_cast<uint8_t>((page >> 8) & 0xFF);
    out[offset++] = static_cast<uint8_t>((page >> 16) & 0xFF);
    out[offset++] = static_cast<uint8_t>((page >> 24) & 0xFF);
  }
  return needed;
}

bool decode(const uint8_t* data, const size_t size, std::vector<uint32_t>& pages) {
  pages.clear();
  if (data == nullptr || size < 2 || data[0] != BOOKMARKS_VERSION) {
    return false;
  }

  const size_t count = data[1];
  if (count > MAX_BOOKMARKS || size < encodedSize(count)) {
    return false;
  }

  pages.reserve(count);
  for (size_t i = 0; i < count; i++) {
    const size_t offset = 2 + i * 4;
    pages.push_back(static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
                    (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24));
  }
  return true;
}

bool contains(const std::vector<uint32_t>& pages, const uint32_t page) {
  return std::binary_search(pages.begin(), pages.end(), page);
}

bool toggle(std::vector<uint32_t>& pages, const uint32_t page) {
  const auto it = std::lower_bound(pages.begin(), pages.end(), page);
  if (it != pages.end() && *it == page) {
    pages.erase(it);
    return false;
  }
  if (pages.size() >= MAX_BOOKMARKS) {
    // Refuse silently rather than dropping the oldest: a bookmark the user set
    // disappearing later is worse than one that visibly failed to take.
    return false;
  }
  pages.insert(it, page);
  return true;
}

#ifndef XTC_BOOKMARKS_HOST_TEST

namespace {
std::string bookmarksPath(const std::string& cachePath) { return cachePath + "/bookmarks.bin"; }
}  // namespace

bool load(const std::string& cachePath, std::vector<uint32_t>& pages) {
  pages.clear();

  HalFile file;
  if (!Storage.openFileForRead("XBM", bookmarksPath(cachePath), file)) {
    return false;  // No bookmarks yet is the normal case, not an error.
  }

  uint8_t buffer[MAX_ENCODED_SIZE];
  const size_t read = file.read(buffer, sizeof(buffer));
  if (!decode(buffer, read, pages)) {
    LOG_DBG("XBM", "Ignoring unreadable bookmarks (%u bytes)", static_cast<unsigned>(read));
    return false;
  }
  return true;
}

bool save(const std::string& cachePath, const std::vector<uint32_t>& pages) {
  uint8_t buffer[MAX_ENCODED_SIZE];
  const size_t written = encode(pages, buffer, sizeof(buffer));
  if (written == 0) {
    LOG_ERR("XBM", "Failed to encode %u bookmarks", static_cast<unsigned>(pages.size()));
    return false;
  }

  // Same crash-safe temp-then-rename as progress.bin: a torn write here would
  // lose every bookmark in the book, not just the one being added.
  return ProgressFile::writeAtomicNamed(cachePath, "bookmarks.bin", buffer, written);
}

#endif  // XTC_BOOKMARKS_HOST_TEST

}  // namespace XtcBookmarks
