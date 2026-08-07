#include "FullPageLayout.h"

#include <algorithm>
#include <cstdint>

namespace FullPageLayout {

Layout plan(const int stripWidth, const int stripHeight, const int panelWidth, const int panelHeight,
            const int overlapPercent) {
  Layout layout{};
  if (stripWidth <= 0 || stripHeight <= 0 || panelWidth <= 0 || panelHeight <= 0) {
    return layout;  // valid stays false
  }

  // The strip is stored turned a quarter turn, so its stored WIDTH is the page
  // direction (the rows of the page this strip covers) and its stored HEIGHT is
  // the page's full width. Getting these two the wrong way round assembles the
  // page sideways and lands the scale at 0.4 instead of 0.6.
  const int stripAlongPage = stripWidth;
  const int pageAcross = stripHeight;

  const int overlap = std::clamp(overlapPercent, MIN_OVERLAP_PERCENT, MAX_OVERLAP_PERCENT);
  const int overlapColumns = stripAlongPage * overlap / 100;

  // What each strip contributes once its duplicated lead is dropped. The first
  // strip keeps everything; the rest lose the overlap from their leading edge.
  const int croppedAlong = stripAlongPage - overlapColumns;
  if (croppedAlong <= 0) {
    return layout;
  }

  const int assembledAlong = stripAlongPage + (STRIPS_PER_PAGE - 1) * croppedAlong;

  // Fit the assembled page (pageAcross wide, assembledAlong tall) into the
  // panel, preserving aspect. Integer maths throughout: a float scale would
  // round differently per strip and leave a one-pixel seam or gap.
  const int heightAtWidthFit = static_cast<int>(static_cast<int64_t>(assembledAlong) * panelWidth / pageAcross);
  if (heightAtWidthFit <= panelHeight) {
    layout.dstWidth = panelWidth;
    layout.dstHeight = heightAtWidthFit;
  } else {
    layout.dstHeight = panelHeight;
    layout.dstWidth = static_cast<int>(static_cast<int64_t>(pageAcross) * panelHeight / assembledAlong);
  }
  if (layout.dstWidth <= 0 || layout.dstHeight <= 0) {
    return layout;
  }
  layout.dstLeft = (panelWidth - layout.dstWidth) / 2;

  // Destination boundaries come from the running assembled offset rather than
  // accumulating per strip, so rounding cannot drift a seam: strip i+1 always
  // starts exactly where strip i ended.
  int assembledSoFar = 0;
  int previousDstEnd = 0;
  for (int i = 0; i < STRIPS_PER_PAGE; i++) {
    StripPlacement& placement = layout.strips[i];
    placement.srcStart = i == 0 ? 0 : overlapColumns;
    placement.srcCount = i == 0 ? stripAlongPage : croppedAlong;

    assembledSoFar += placement.srcCount;
    const int dstEnd = static_cast<int>(static_cast<int64_t>(assembledSoFar) * layout.dstHeight / assembledAlong);

    placement.dstStart = previousDstEnd;
    placement.dstCount = dstEnd - previousDstEnd;
    previousDstEnd = dstEnd;
  }

  layout.valid = true;
  return layout;
}

int sourceRowsPerDestRow(const StripPlacement& placement) {
  if (placement.dstCount <= 0) {
    return 1;
  }
  return std::max(1, placement.srcCount / placement.dstCount);
}

int sourceRowFor(const StripPlacement& placement, const int dstRow) {
  if (placement.dstCount <= 0) {
    return placement.srcStart;
  }
  const int offset = static_cast<int>(static_cast<int64_t>(dstRow) * placement.srcCount / placement.dstCount);
  // Clamped rather than trusted: dstRow comes from a render loop bounded by
  // dstCount, but an off-by-one there would read past the strip buffer.
  return placement.srcStart + std::clamp(offset, 0, placement.srcCount - 1);
}

}  // namespace FullPageLayout
