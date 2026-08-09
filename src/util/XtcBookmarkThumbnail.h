#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class Xtc;

/**
 * Small 1-bit thumbnails for bookmarked XTC/XTCH pages.
 *
 * "Page 212" means nothing in a 575-page volume; a thumbnail does. Generated
 * once when a bookmark is set (XtcReaderActivity::toggleBookmarkForCurrentPage)
 * and cached at `<cachePath>/bookmark_<page>.bin`, alongside bookmarks.bin, so
 * XtcReaderBookmarksActivity can show it without re-decoding the page.
 *
 * Deliberately not a BMP: at 40x60 1bpp the whole thumbnail is 300 bytes with
 * no header, which is smaller than a BMP header alone. Xtc::generateThumbBmp
 * solves a different problem (a shelf-sized cover art thumbnail) and is not
 * reused here.
 */
namespace XtcBookmarkThumbnail {

constexpr int WIDTH = 40;
constexpr int HEIGHT = 60;
constexpr size_t ROW_BYTES = WIDTH / 8;      // 5; WIDTH is byte-aligned, no row padding
constexpr size_t SIZE = ROW_BYTES * HEIGHT;  // 300

/**
 * Downscales `page`'s decoded bitmap (box-averaged ink level, thresholded) into
 * a WIDTHxHEIGHT 1bpp thumbnail and writes it to the cache directory.
 * Overwrites any existing thumbnail for that page. Returns false on OOM or a
 * page load/write failure; the caller (a bookmark toggle) still keeps the
 * bookmark either way -- a missing thumbnail is a fallback, not an error state.
 */
bool generate(const std::string& cachePath, uint32_t page, const Xtc& xtc);

/** Deletes the cached thumbnail for `page`, if any. Best-effort; no return value. */
void remove(const std::string& cachePath, uint32_t page);

/**
 * Loads the cached thumbnail for `page` into `buffer` (at least SIZE bytes).
 * Returns false when there is no thumbnail (bookmark predates this feature, or
 * generate() failed) -- callers should fall back to a placeholder, not an error.
 */
bool load(const std::string& cachePath, uint32_t page, uint8_t* buffer, size_t bufferSize);

}  // namespace XtcBookmarkThumbnail
