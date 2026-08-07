# KomaOS Roadmap

KomaOS has two roadmaps running side by side.

**The manga track (Phase M)** is the reason this fork exists and is where new work goes.

**The inherited track (Phases 0-2)** is CrossPoint Reader's roadmap, kept below so upstream merges stay
legible and so contributors know which upstream commitments still hold. We follow upstream's phasing there
rather than re-litigating it.

---

## Phase M - Manga Reading — **IN PROGRESS**

**Goal:** make a converted volume as good to read as an EPUB already is, without breaking the 380KB ceiling.

**Ground rule:** decoding happens off-device. The device reads pre-rendered XTC/XTCH pages; CBZ/CBR
conversion belongs in [FlipNzb](https://github.com/0xKnowles/FlipNzb) or
[xtcjs](https://github.com/varo6/xtcjs), where there is RAM to dither properly. See
[SCOPE.md §1a](SCOPE.md).

### M1 - Reading a volume

* **Right-to-left page order**, per book, persisted with progress. The single highest-value change: most
  manga is drawn for it, and it is a page-index transform plus a settings flag, not a rendering change.
* **Strip-aware navigation.** A page in a converted volume is one of N overlapping strips of a source page.
  Surface that: jump by source page, not just by strip.
* **Two-page spread detection** from the XTC index (a page roughly twice the usual width is a spread) and
  a landscape layout for it.
* **Fit modes** — fit-width / fit-height / fill — for volumes converted at a resolution that does not match
  the panel.

### M2 - The library

* **Series shelf.** Group `Series vNN` files into one entry, resume at the right volume, offer the next
  volume at the end of one instead of dumping back to the file browser.
* **Volume covers** from the XTC first page, cached like EPUB covers already are.
* **Continue reading** across a series, not just within a file.

### M3 - The pipeline

* **XTC metadata round-trip** — series, volume number and TOC written by the converter, read and shown here.
* **On-device CBZ** for stored (uncompressed) archives, as a fallback when no XTC exists. Explicitly a
  convenience path, not the main one.
* **Wireless push from FlipNzb** to the device's existing upload endpoint, so a converted volume lands
  without touching the SD card.

Nothing in Phase M is committed until it has a heap budget written down. "It would be nice on a Kindle"
is not an argument on a device with one 48KB framebuffer.

---

## Phase 0 - Close Out Legacy Scope Items — **COMPLETE** *(inherited from CrossPoint Reader)*

**Goal:** Land the work that was already in motion under the prior, broader scope so contributors are not left
hanging, and so we enter the stricter phases with a clean slate.

**Landed in Phase 0:**

* **RTL support PRs.** The in-flight right-to-left work was reviewed, iterated, and merged.
* **Dictionary PR.** The offline dictionary lookup work was reviewed and merged.
* **Bookmarks** feature. First-class navigation markers in EPUBs.
* ~~**Transparent sleep screens.**~~ Shelved; not picked back up under the stricter phases.

Phase 0 is closed. The tighter scope in [SCOPE.md](SCOPE.md) is now fully enforced. "But it was on the old roadmap"
is not a valid argument for accepting a PR.

---

## Phase 1 - Consolidation, Footprint, and Multi-Device Support — **IN PROGRESS**

**Goal:** Reduce memory and flash usage, clean up the codebase, and land the SDK / HAL generalization work so
KomaOS runs cleanly on ESP32-based e-reader hardware beyond Xteink (X3 / X4), including ESP32-S3 class devices.

**Focus areas:**

* DRAM and heap fragmentation reduction across the reader core.
* Flash footprint reduction (dead code, redundant strings, oversized tables).
* Refactors that tighten the HAL / SDK boundary.
* Pluggable per-device SDK layers (display, input, storage, battery) and per-device build configuration without
  forking the reader core.
* Documentation for adding a new ESP32 e-reader target.
* E-ink driver refinement (ghosting, partial update behavior).

**Closed during this phase:** new themes built into firmware, new external network connectors (sync engines, cloud
storage, remote file access).

---

## Phase 2 - Languages, Fonts, and Themes

**Goal:** With the codebase smaller and portable, make reading great in every language: multi-language support,
better font support with custom fonts, UI translations, and themes loaded from the SD card instead of consuming
flash.

**Focus areas:**

* Multi-language reading support (underserved languages, complex script support where realistic on ESP32 hardware).
* Better font support and custom fonts.
* UI languages and localization.
* Moving themes off-firmware to SD-loaded assets (see SCOPE.md Section 6).
* **Moving hyphenation files off-firmware.** Hyphenation rules vary per language and the files are large (German
  alone is ~200KB). Today these eat flash budget that should be available for the reader core. The plan is to build
  a downloader analogous to the existing font downloader and store the dictionaries on SD / SPIFFS, loading on
  demand. This unlocks better hyphenation for long-word languages (German, Finnish, Norwegian, etc.) without paying
  the flash cost up front.

This phase depends on Phase 1 cleanup landing first; otherwise we generalize a moving target.

---

## Out of Roadmap

The following are explicitly *not* on the roadmap. They may live in other KomaOS forks; they will not be picked
up here:

* Interactive apps (games, calculators, notepads).
* Writing / authoring tools.
* Active connectivity features (RSS, news, browsers).
* PDF rendering as a first-class format.

See [SCOPE.md](SCOPE.md) for the full rationale.

---

## How This Roadmap Changes

* Phase boundaries are decided by maintainers, not by individual PRs.
* If a phase needs to be extended or an item carried over, that is documented here with a short note.
* Proposals for new phases or reordering should go through a Discussion first.
