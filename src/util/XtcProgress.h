/**
 * XtcProgress.h
 *
 * Read/write access to an XTC volume's `progress.bin`.
 *
 * The reader used to persist a bare 4-byte page index, which is enough to
 * resume but not to say anything about a book you are *not* currently reading:
 * "page 88" means nothing without the page count, and the page count lives in
 * the XTC header, which costs an open-and-parse of a multi-megabyte file. That
 * is why the home screen could never show progress for the volumes on the shelf.
 *
 * So the current layout appends the volume's page count:
 *
 *   [0..3]  current page index, little-endian, 0-based
 *   [4..7]  page count for the volume, little-endian
 *
 * Files written by older firmware are 4 bytes and still load -- they simply
 * report no page count, and callers show a placeholder rather than a wrong
 * percentage. Nothing needs migrating: the first page turn rewrites the file.
 *
 * XTC only. EPUB and TXT write different structures to the same filename
 * (EpubReaderActivity.cpp:175 stores a spine index and a chapter page total),
 * so `cachePathFor` deliberately refuses anything that is not an XTC volume
 * instead of decoding another reader's bytes as a page number.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace XtcProgress {

/** Layout written by older firmware: page index only. Read, never written. */
constexpr size_t LEGACY_SIZE = 4;
/** Current layout: page index followed by the volume's page count. */
constexpr size_t CURRENT_SIZE = 8;

/** Returned by percent() when the page count is unknown. */
constexpr int UNKNOWN_PERCENT = -1;

struct Snapshot {
  /** 0-based index of the page the reader was last on. */
  uint32_t page = 0;
  /** Pages in the volume, or 0 when the file predates the page count. */
  uint32_t pageCount = 0;
  /** False when there was no readable progress at all. */
  bool valid = false;

  bool hasPageCount() const { return valid && pageCount > 0; }

  /**
   * Progress through the volume as 0-100, or UNKNOWN_PERCENT.
   *
   * Counts the current page as read, so the last page reports 100 rather than
   * stalling one page short of it.
   */
  int percent() const;
};

/**
 * Serialises `page` and `pageCount` into `out`.
 *
 * Returns the number of bytes written, or 0 if `out` is null or too small.
 */
size_t encode(uint32_t page, uint32_t pageCount, uint8_t* out, size_t outSize);

/** Parses either layout. A short or null buffer yields an invalid Snapshot. */
Snapshot decode(const uint8_t* data, size_t size);

#ifndef XTC_PROGRESS_HOST_TEST

/**
 * Cache directory for an XTC volume, matching Xtc's own construction
 * (Xtc.h:32). Returns an empty string for any other file type.
 *
 * Lets callers that hold only a path -- the home screen's recent list, say --
 * find a volume's progress without constructing an Xtc and parsing its header.
 */
std::string cachePathFor(const std::string& bookPath);

/** Reads `<cachePath>/progress.bin`. A missing file is not an error. */
Snapshot read(const std::string& cachePath);

/** Writes progress atomically (temp-then-rename, via ProgressFile). */
bool write(const std::string& cachePath, uint32_t page, uint32_t pageCount);

#endif  // XTC_PROGRESS_HOST_TEST

}  // namespace XtcProgress
