/**
 * XtcBookmarks.h
 *
 * Bookmarks for paged image books (XTC/XTCH).
 *
 * Deliberately not BookmarkEntry. That struct exists to survive re-pagination
 * in EPUB -- XPath, spine index, visible-text offset -- because an EPUB page
 * number means nothing once the font or margins change. An XTC page is a fixed,
 * pre-rendered image: the page number IS the position, permanently. Reusing the
 * EPUB struct would mean carrying six unused fields per bookmark and picking one
 * to abuse as a page number.
 *
 * Stored per book at `<cachePath>/bookmarks.bin`, alongside progress.bin:
 *
 *   0x00  1  format version (BOOKMARKS_VERSION)
 *   0x01  1  count
 *   0x02  4 * count  page indices, little-endian, 0-based, ascending
 *
 * A version mismatch is treated as "no bookmarks" rather than an error, which
 * is how progress.bin behaves too -- losing bookmarks on a format bump is
 * annoying, refusing to open the book is worse.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace XtcBookmarks {

constexpr uint8_t BOOKMARKS_VERSION = 1;

/**
 * Upper bound on bookmarks per book.
 *
 * Caps the file at 2 + 4 * 64 = 258 bytes, so load() can use a stack buffer and
 * the reader never allocates on the bookmark path.
 */
constexpr size_t MAX_BOOKMARKS = 64;

/** Bytes a file holding `count` bookmarks occupies. */
constexpr size_t encodedSize(const size_t count) { return 2 + 4 * count; }

/** Largest possible file, for sizing a caller's buffer. */
constexpr size_t MAX_ENCODED_SIZE = encodedSize(MAX_BOOKMARKS);

/**
 * Serializes `pages` into `out`, which must hold at least
 * encodedSize(pages.size()) bytes. Returns bytes written, or 0 if the buffer is
 * too small or there are more than MAX_BOOKMARKS pages.
 */
size_t encode(const std::vector<uint32_t>& pages, uint8_t* out, size_t outSize);

/**
 * Parses `data` into `pages`, which is cleared first.
 *
 * Returns false on a short, truncated or wrong-version buffer, leaving `pages`
 * empty. Entries are returned in the order stored.
 */
bool decode(const uint8_t* data, size_t size, std::vector<uint32_t>& pages);

/**
 * Adds `page` if absent, removes it if present, keeping `pages` ascending.
 *
 * Returns true when the page ended up bookmarked. A full list refuses further
 * additions (returns false) but still allows removal.
 */
bool toggle(std::vector<uint32_t>& pages, uint32_t page);

/** True when `page` is bookmarked. */
bool contains(const std::vector<uint32_t>& pages, uint32_t page);

/** Reads `<cachePath>/bookmarks.bin`. Missing file yields an empty list. */
bool load(const std::string& cachePath, std::vector<uint32_t>& pages);

/** Writes `<cachePath>/bookmarks.bin`, creating it as needed. */
bool save(const std::string& cachePath, const std::vector<uint32_t>& pages);

}  // namespace XtcBookmarks
