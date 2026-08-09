#pragma once

#include <string>
#include <vector>

namespace NextBookFinder {

// Collects up to maxCount book files that order after currentBookPath's filename
// (natural sort, same ordering as the file browser) within the same folder.
// Returns bare filenames in sorted order; the current file itself is excluded.
// Single directory pass keeping only the maxCount best matches, so memory stays
// bounded regardless of folder size.
//
// When currentBookPath's name parses to a series and volume (SeriesTitle::parse),
// only files belonging to that same series are offered -- a shelf folder holding
// several series should suggest the next volume, not just whatever sorts next.
// A name with no recognisable volume marker has no series to match, so every
// next file in natural order is offered, as before.
std::vector<std::string> findNextBooks(const std::string& currentBookPath, size_t maxCount);

}  // namespace NextBookFinder
