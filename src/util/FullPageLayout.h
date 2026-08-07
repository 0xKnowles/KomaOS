/**
 * FullPageLayout.h
 *
 * Reassembles the strips of a split manga page into one full page.
 *
 * FlipNzb splits a tall page into overlapping strips at encode time
 * (comic.ts, overlapSegments) and the device only ever receives the strips.
 * Full view mode stitches them back: each strip is cropped so the seams butt
 * instead of overlapping, then the run is scaled down to fit the panel.
 *
 * Two things this has to get right, both of which cost a page if wrong:
 *
 * 1. The overlap is NOT the 5% the encoder mentions. That figure is a floor --
 *    overlapSegments adds strips if overlap would drop below it. The real
 *    overlap follows from the arithmetic and runs 25-31% of a strip on typical
 *    pages, so a fixed small crop would leave most of the duplication in place.
 *
 * 2. The device cannot derive the overlap from the file. It knows the strip
 *    count and the strip's pixel size, but the overlap depends on the ORIGINAL
 *    page's aspect ratio, which the encoder consumed and did not record. Until
 *    the format carries it, the fraction comes from a setting; `overlapPercent`
 *    is that value, and everything here is written so that swapping it for a
 *    per-book number read from the file changes nothing else.
 *
 * The scale is not a free parameter. Strips are stored scaled to the panel's
 * long edge; the reassembled page is bound by the short edge, so a full page
 * always lands at exactly shortEdge/longEdge of the stored resolution -- 0.6 on
 * an X4, independent of the source page size. Callers do not get to pick it.
 */

#pragma once

#include <cstdint>

namespace FullPageLayout {

/** Strips per full page. FlipNzb's overlap mode emits three unless a page is
 *  extreme enough to need more, which this does not yet handle. */
constexpr int STRIPS_PER_PAGE = 3;

/** Bounds for the overlap setting, as a percentage of one strip. */
constexpr int MIN_OVERLAP_PERCENT = 0;
constexpr int MAX_OVERLAP_PERCENT = 45;
/** Mid-range of what overlapSegments produces on typical manga page ratios. */
constexpr int DEFAULT_OVERLAP_PERCENT = 27;

/**
 * Where one strip's pixels come from, and where they land on the panel.
 *
 * Everything here counts along the PAGE direction -- down the original manga
 * page. In a stored strip that is the strip's X axis, not its Y: FlipNzb writes
 * each strip already turned a quarter turn (comic.ts, Region::rotate), so the
 * strip's stored width spans the page rows it covers and its stored height
 * spans the page's full width. Stacking strips along stored Y would assemble
 * the page sideways, which is exactly the bug the 0.6 scale test catches.
 */
struct StripPlacement {
  /** First column of the strip to read (page direction). */
  int srcStart;
  /** Columns of the strip to read. Zero means the strip contributes nothing. */
  int srcCount;
  /** First panel row written, once the page is turned upright. */
  int dstStart;
  /** Panel rows written. */
  int dstCount;

  bool contributes() const { return srcCount > 0 && dstCount > 0; }
};

struct Layout {
  /** Placement per strip, in reading order. */
  StripPlacement strips[STRIPS_PER_PAGE];
  /** Left edge of the assembled page on the panel, for horizontal centring. */
  int dstLeft;
  /** Width of the assembled page on the panel (the page's own width). */
  int dstWidth;
  /** Total height of the assembled page on the panel. */
  int dstHeight;
  /** False when the inputs cannot produce a page worth drawing. */
  bool valid;
};

/**
 * Plans the reassembly.
 *
 * `stripWidth`/`stripHeight` are one strip's stored pixel dimensions; `panel`
 * dimensions are the drawable area. `overlapPercent` is clamped to the bounds
 * above.
 *
 * The whole overlap is taken off the top of each strip after the first, so the
 * first and last strips keep their full extent. Splitting it evenly across both
 * sides of a seam would be equally correct arithmetically, but cropping only
 * the leading edge keeps the two strips a reader sees most of -- the top and
 * bottom of the page -- untouched.
 */
Layout plan(int stripWidth, int stripHeight, int panelWidth, int panelHeight, int overlapPercent);

/**
 * Maps a panel row back to the source row that feeds it, for a placement.
 *
 * Nearest-row selection is deliberate here: the caller box-filters around this
 * row to recover grey from the 1-bit source, so this only has to name the
 * centre of the footprint.
 */
int sourceRowFor(const StripPlacement& placement, int dstRow);

/** Source columns that one destination row covers; at least 1. */
int sourceRowsPerDestRow(const StripPlacement& placement);

}  // namespace FullPageLayout
