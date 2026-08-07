#pragma once

#include <BoardConfig.h>
#include <HalClock.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "KOReaderCredentialStore.h"
#include "KomaSettings.h"
#include "ReaderFontSizes.h"
#include "activities/settings/SettingsActivity.h"
#include "util/DictionaryRegistry.h"

// Build the font family setting dynamically. When registry is non-null, SD card fonts
// are appended after the built-in fonts. Otherwise only built-in fonts are listed.
inline SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  // Built-in font labels (StrId)
  std::vector<StrId> enumValues = {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS};
  // Runtime string labels for SD card fonts
  std::vector<std::string> enumStringValues;

  // Reserve: first KomaSettings::BUILTIN_FONT_COUNT entries use StrId, rest use strings
  if (registry) {
    const auto& families = registry->getFamilies();
    enumStringValues.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(enumStringValues),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  // Capture the SD font count for the lambdas
  const int sdFontCount = static_cast<int>(enumStringValues.size());

  // Total option count = built-in + SD card families
  // For the combined enumStringValues: we need all entries as strings (built-in names + SD names)
  // The render code checks enumStringValues first, then enumValues. So we build enumStringValues
  // with all options when SD fonts are present.
  std::vector<std::string> allStringValues;
  if (sdFontCount > 0) {
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SERIF));
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SANS));
    allStringValues.insert(allStringValues.end(), enumStringValues.begin(), enumStringValues.end());
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_FAMILY;
  s.type = SettingType::ENUM;
  s.enumValues = std::move(enumValues);
  s.enumStringValues = std::move(allStringValues);
  s.key = "fontFamily";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-family entry it replaces

  // Capture registry families by copy for the lambdas
  std::vector<std::string> sdFamilyNames;
  if (registry) {
    const auto& families = registry->getFamilies();
    sdFamilyNames.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(sdFamilyNames),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  s.valueGetter = [sdFamilyNames]() -> uint8_t {
    // If an SD card font is selected, find its index
    if (SETTINGS.sdFontFamilyName[0] != '\0') {
      for (int i = 0; i < static_cast<int>(sdFamilyNames.size()); i++) {
        if (sdFamilyNames[i] == SETTINGS.sdFontFamilyName) {
          return static_cast<uint8_t>(KomaSettings::BUILTIN_FONT_COUNT + i);
        }
      }
      // SD font name not found in registry — fall through to built-in
    }
    return SETTINGS.fontFamily < KomaSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  };

  s.valueSetter = [sdFamilyNames](uint8_t v) {
    if (v < KomaSettings::BUILTIN_FONT_COUNT) {
      SETTINGS.fontFamily = v;
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else {
      int sdIdx = v - KomaSettings::BUILTIN_FONT_COUNT;
      if (sdIdx < static_cast<int>(sdFamilyNames.size())) {
        strncpy(SETTINGS.sdFontFamilyName, sdFamilyNames[sdIdx].c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
        SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      }
    }
  };

  return s;
}

// Build the font size setting dynamically: the options are the point sizes the
// active family actually ships, so an SD family built at 10/12/14 offers three
// sizes and a family built at 8..18 offers six. The selected point size persists
// in SETTINGS.fontPointSize (saved/loaded manually in KomaSettings::
// toJson/fromJson — the generic loop skips dynamic entries), while the ENUM
// contract shared with the web UI stays index-based.
inline SettingInfo buildFontSizeSetting(const SdCardFontRegistry* registry) {
  // Captured by copy: getSettingsList() returns by value and the lambdas outlive
  // this call, so they must not reference the registry.
  const std::vector<uint8_t> sizes = readerFontPointSizes(registry, SETTINGS.sdFontFamilyName);

  // "pt" is deliberately not translated — see the matching note in
  // TextSettingsActivity::rebuildSizeList().
  std::vector<std::string> labels;
  labels.reserve(sizes.size());
  for (const uint8_t pt : sizes) {
    labels.push_back(std::to_string(pt) + " pt");
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_SIZE;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
  s.key = "fontSize";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-size entry it replaces

  s.valueGetter = [sizes]() -> uint8_t {
    const uint8_t pt = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
    for (int i = 0; i < static_cast<int>(sizes.size()); i++) {
      if (sizes[i] == pt) return static_cast<uint8_t>(i);
    }
    return 0;
  };

  s.valueSetter = [sizes](uint8_t v) {
    if (v < sizes.size()) SETTINGS.fontPointSize = sizes[v];
  };

  return s;
}

// Build the dictionary selection setting dynamically from the folders discovered
// under /dictionaries. "None" plus one option per dictionary; the selected folder
// name persists in SETTINGS.dictionaryName (saved/loaded manually in
// KomaSettings::toJson/fromJson — the generic loop skips dynamic entries).
inline SettingInfo buildDictionarySetting(const std::vector<DictionaryEntry>& dictionaries) {
  std::vector<std::string> folderNames;
  folderNames.reserve(dictionaries.size());
  std::transform(dictionaries.begin(), dictionaries.end(), std::back_inserter(folderNames),
                 [](const DictionaryEntry& d) { return d.name; });

  SettingInfo s;
  s.nameId = StrId::STR_DICTIONARY;
  s.type = SettingType::ENUM;
  s.enumStringValues.reserve(folderNames.size() + 1);
  s.enumStringValues.push_back(I18N.get(StrId::STR_NONE_OPT));
  s.enumStringValues.insert(s.enumStringValues.end(), folderNames.begin(), folderNames.end());
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = [folderNames]() -> uint8_t {
    for (size_t i = 0; i < folderNames.size(); i++) {
      // Compare within the settings field capacity: an over-long folder name is
      // stored truncated, and must still match its list entry.
      if (strncmp(folderNames[i].c_str(), SETTINGS.dictionaryName, sizeof(SETTINGS.dictionaryName) - 1) == 0) {
        return static_cast<uint8_t>(i + 1);
      }
    }
    return 0;  // "None", also when the stored folder no longer exists
  };

  s.valueSetter = [folderNames](uint8_t v) {
    if (v == 0 || v > folderNames.size()) {
      SETTINGS.dictionaryName[0] = '\0';
      return;
    }
    strncpy(SETTINGS.dictionaryName, folderNames[v - 1].c_str(), sizeof(SETTINGS.dictionaryName) - 1);
    SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
  };

  return s;
}

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
//
// The static list is constructed exactly once (master's optimization, #1086 +
// #1636) so the per-entry SettingInfo cost is paid once; every call then copies
// it. When an SdCardFontRegistry is supplied AND has SD card fonts installed,
// the font-family entry is replaced in that copy with a registry-aware version.
// The font-size entry is always rebuilt, since its options are point sizes read
// from the active family rather than a fixed enum.
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr,
                                                const std::vector<DictionaryEntry>* dictionaries = nullptr) {
  static const std::vector<SettingInfo> baseList = [] {
    // Enum settings are persisted as numeric values. Assign these labels by enum
    // value so a reordered menu or enum cannot silently swap their behavior.
    std::vector<StrId> sleepScreenValues(KomaSettings::SLEEP_SCREEN_MODE_COUNT);
    sleepScreenValues[KomaSettings::DARK] = StrId::STR_DARK;
    sleepScreenValues[KomaSettings::LIGHT] = StrId::STR_LIGHT;
    sleepScreenValues[KomaSettings::CUSTOM] = StrId::STR_CUSTOM;
    sleepScreenValues[KomaSettings::COVER] = StrId::STR_COVER;
    sleepScreenValues[KomaSettings::COVER_CUSTOM] = StrId::STR_COVER_CUSTOM;
    sleepScreenValues[KomaSettings::BLANK] = StrId::STR_NONE_OPT;
    sleepScreenValues[KomaSettings::QUICK_RESUME] = StrId::STR_QUICK_RESUME;

    std::vector<StrId> statusBarClockValues(KomaSettings::STATUS_BAR_CLOCK_MODE_COUNT);
    statusBarClockValues[KomaSettings::STATUS_BAR_CLOCK_HIDE] = StrId::STR_HIDE;
    statusBarClockValues[KomaSettings::STATUS_BAR_CLOCK_RIGHT] = StrId::STR_DIR_RIGHT;
    statusBarClockValues[KomaSettings::STATUS_BAR_CLOCK_LEFT] = StrId::STR_DIR_LEFT;

    std::vector<SettingInfo> v = {
        // --- Display ---
        SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &KomaSettings::sleepScreen, std::move(sleepScreenValues),
                          "sleepScreen", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &KomaSettings::sleepScreenCoverMode,
                          {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &KomaSettings::sleepScreenCoverFilter,
                          {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                          "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_QUICK_RESUME_TIMEOUT, &KomaSettings::quickResumeSleepScreen,
                          {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "quickResumeSleepScreen",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &KomaSettings::hideBatteryPercentage,
                          {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(
            StrId::STR_REFRESH_FREQ, &KomaSettings::refreshFrequency,
            {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30},
            "refreshFrequency", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_UI_THEME, &KomaSettings::uiTheme,
                          {StrId::STR_THEME_CLASSIC, StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED,
                           StrId::STR_THEME_ROUNDEDRAFF, StrId::STR_THEME_COLLECTION},
                          "uiTheme", StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &KomaSettings::fadingFix, "fadingFix",
                            StrId::STR_CAT_DISPLAY),

        // --- Manga (XTC/XTCH) ---
        // Everything here affects paged-image reading only; none of it touches
        // the EPUB path, which is why it is its own category rather than more
        // entries under Reader.
        SettingInfo::Enum(StrId::STR_MANGA_READING_DIRECTION, &KomaSettings::mangaReadingDirection,
                          {StrId::STR_MANGA_DIR_AUTO, StrId::STR_MANGA_DIR_LTR, StrId::STR_MANGA_DIR_RTL},
                          "mangaReadingDirection", StrId::STR_CAT_MANGA),
        SettingInfo::Enum(StrId::STR_MANGA_REFRESH_FREQ, &KomaSettings::mangaRefreshFrequency,
                          {StrId::STR_MANGA_REFRESH_GLOBAL, StrId::STR_PAGES_1, StrId::STR_PAGES_3, StrId::STR_PAGES_5,
                           StrId::STR_PAGES_10},
                          "mangaRefreshFrequency", StrId::STR_CAT_MANGA),
        SettingInfo::Enum(StrId::STR_MANGA_SKIP_PAGES, &KomaSettings::mangaSkipPages,
                          {StrId::STR_PAGES_3, StrId::STR_PAGES_6, StrId::STR_PAGES_10, StrId::STR_PAGES_20},
                          "mangaSkipPages", StrId::STR_CAT_MANGA),
        SettingInfo::Enum(StrId::STR_MANGA_VIEW_MODE, &KomaSettings::mangaViewMode,
                          {StrId::STR_MANGA_VIEW_SPLIT, StrId::STR_MANGA_VIEW_FULL}, "mangaViewMode",
                          StrId::STR_CAT_MANGA),
        // Only meaningful in Full view. Left visible in Split so the value can
        // be tuned before switching over rather than blind afterwards.
        SettingInfo::Enum(StrId::STR_MANGA_FULL_OVERLAP, &KomaSettings::mangaFullOverlap,
                          {StrId::STR_PERCENT_20, StrId::STR_PERCENT_24, StrId::STR_PERCENT_27, StrId::STR_PERCENT_31,
                           StrId::STR_PERCENT_35},
                          "mangaFullOverlap", StrId::STR_CAT_MANGA),

        // --- Reader ---
        // Built-in font-family entry. Replaced per-call with a registry-aware
        // version when SD fonts are installed.
        SettingInfo::Enum(StrId::STR_FONT_FAMILY, &KomaSettings::fontFamily,
                          {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER)
            .withTextSettings(),
        // Placeholder: the selectable sizes depend on the active font family, so
        // this entry is always replaced by buildFontSizeSetting() below. It only
        // fixes the setting's position in the Reader category.
        SettingInfo::Enum(StrId::STR_FONT_SIZE, nullptr, {}, "fontSize", StrId::STR_CAT_READER).withTextSettings(),
        SettingInfo::Enum(StrId::STR_LINE_SPACING, &KomaSettings::lineSpacing,
                          {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE}, "lineSpacing", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Value(
            StrId::STR_SCREEN_MARGIN, &KomaSettings::screenMargin,
            {KomaSettings::SCREEN_MARGIN_MIN, KomaSettings::SCREEN_MARGIN_MAX, KomaSettings::SCREEN_MARGIN_STEP},
            "screenMargin", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &KomaSettings::paragraphAlignment,
                          {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                           StrId::STR_BOOK_S_STYLE},
                          "paragraphAlignment", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &KomaSettings::embeddedStyle, "embeddedStyle",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_FOCUS_READING, &KomaSettings::focusReadingEnabled, "focusReadingEnabled",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_HYPHENATION, &KomaSettings::hyphenationEnabled, "hyphenationEnabled",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(
            StrId::STR_ORIENTATION, &KomaSettings::orientation,
            {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW},
            "orientation", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &KomaSettings::extraParagraphSpacing, "extraParagraphSpacing",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_TEXT_AA, &KomaSettings::textAntiAliasing, "textAntiAliasing",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_IMAGES, &KomaSettings::imageRendering,
                          {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                          "imageRendering", StrId::STR_CAT_READER),
        // --- Controls ---
        SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &KomaSettings::sideButtonLayout,
                          {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED}, "sideButtonLayout",
                          StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_TOUCH_READER_CONTROLS, &KomaSettings::touchReaderControls,
                          {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "touchReaderControls", StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, &KomaSettings::frontButtonFollowOrientation,
                            "frontButtonFollowOrientation", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &KomaSettings::longPressButtonBehavior,
                          {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                           StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION},
                          "longPressButtonBehavior", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_LONG_PRESS_MENU, &KomaSettings::longPressMenuFunction,
                          {StrId::STR_KOSYNC, StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION, StrId::STR_DICTIONARY,
                           StrId::STR_MANGA_VIEW_MODE},
                          "longPressMenuFunction", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(
            StrId::STR_SHORT_PWR_BTN, &KomaSettings::shortPwrBtn,
            {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_FORCE_REFRESH, StrId::STR_FOOTNOTES},
            "shortPwrBtn", StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_PWR_BTN_FOOTNOTE_BACK, &KomaSettings::pwrBtnFootnoteBack, "pwrBtnFootnoteBack",
                            StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_BACK_SHORT_TO_FILE_BROWSER, &KomaSettings::backShortToFileBrowser,
                            "backShortToFileBrowser", StrId::STR_CAT_CONTROLS),

        // --- System ---
        SettingInfo::Value(StrId::STR_TIME_TO_SLEEP, &KomaSettings::sleepTimeoutMinutes,
                           {KomaSettings::MIN_SLEEP_TIMEOUT_MINUTES, KomaSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
                           "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &KomaSettings::showHiddenFiles, "showHiddenFiles",
                            StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_REMOVE_READ_FROM_RECENTS, &KomaSettings::removeReadBooksFromRecents,
                            "removeReadBooksFromRecents", StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_MOVE_FINISHED_TO_READ, &KomaSettings::moveFinishedToReadFolder,
                            "moveFinishedToReadFolder", StrId::STR_CAT_SYSTEM),

        // OPDS download folder: persisted + web-exposed, but category-less so it
        // is hidden from the on-device Settings screen (edited via OPDS UI).
        SettingInfo::String(StrId::STR_OPDS_DOWNLOAD_FOLDER, &SETTINGS.opdsDownloadFolder[0],
                            sizeof(SETTINGS.opdsDownloadFolder), "opdsDownloadFolder"),
        // OPDS download filename format: persisted + web-exposed, category-less so it
        // is hidden from the on-device Settings screen (cycled from the OPDS UI).
        SettingInfo::Enum(StrId::STR_OPDS_FILENAME_FORMAT, &KomaSettings::opdsFilenameFormat,
                          {StrId::STR_FMT_AUTHOR_TITLE, StrId::STR_FMT_TITLE_AUTHOR, StrId::STR_FMT_TITLE},
                          "opdsFilenameFormat"),

        // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
        SettingInfo::DynamicString(
            StrId::STR_KOREADER_USERNAME, [] { return KOREADER_STORE.getUsername(); },
            [](const std::string& v) {
              KOREADER_STORE.setCredentials(v, KOREADER_STORE.getPassword());
              KOREADER_STORE.saveToFile();
            },
            "koUsername", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicString(
            StrId::STR_KOREADER_PASSWORD, [] { return KOREADER_STORE.getPassword(); },
            [](const std::string& v) {
              KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), v);
              KOREADER_STORE.saveToFile();
            },
            "koPassword", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicString(
            StrId::STR_SYNC_SERVER_URL, [] { return KOREADER_STORE.getServerUrl(); },
            [](const std::string& v) {
              KOREADER_STORE.setServerUrl(v);
              KOREADER_STORE.saveToFile();
            },
            "koServerUrl", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
            [](uint8_t v) {
              KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(v));
              KOREADER_STORE.saveToFile();
            },
            "koMatchMethod", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_SEND_METADATA, {StrId::STR_STATE_OFF, StrId::STR_STATE_ON},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getSendMetadata()); },
            [](uint8_t v) {
              KOREADER_STORE.setSendMetadata(v != 0);
              KOREADER_STORE.saveToFile();
            },
            "koSendMetadata", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_SYNC_BEHAVIOR, {StrId::STR_ASK_EVERY_TIME, StrId::STR_SMART_SYNC},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getSyncBehavior()); },
            [](uint8_t v) {
              KOREADER_STORE.setSyncBehavior(static_cast<KOReaderSyncBehavior>(v));
              KOREADER_STORE.saveToFile();
            },
            "koSyncBehavior", StrId::STR_KOREADER_SYNC),
        // --- Status Bar Settings (web-only, uses StatusBarSettingsActivity) ---
        SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &KomaSettings::statusBarChapterPageCount,
                            "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE, &KomaSettings::statusBarBookProgressPercentage,
                            "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &KomaSettings::statusBarProgressBar,
                          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS, &KomaSettings::statusBarProgressBarThickness,
                          {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK},
                          "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_TITLE, &KomaSettings::statusBarTitle,
                          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Toggle(StrId::STR_BATTERY, &KomaSettings::statusBarBattery, "statusBarBattery",
                            StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_XTC_STATUS_BAR, &KomaSettings::xtcStatusBarMode,
                          {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP}, "xtcStatusBarMode",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        // Clock entries (web settings only; device UI uses ClockOffsetActivity for the offset).
        // Range 0..104 = quarter-hour steps from UTC-12:00 to UTC+14:00, biased by 48.
        SettingInfo::Enum(StrId::STR_CLOCK, &KomaSettings::statusBarClock, std::move(statusBarClockValues),
                          "statusBarClock", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Value(StrId::STR_CLOCK_UTC_OFFSET, &KomaSettings::clockUtcOffsetQ, {0, 104, 1}, "clockUtcOffsetQ",
                           StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_CLOCK_FORMAT, &KomaSettings::clockFormat,
                          {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H}, "clockFormat",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        // Persistence flag for NTP debounce. Resetting from the web UI forces a re-sync
        // on next WiFi connect, which is useful when crossing time zones.
        SettingInfo::Toggle(StrId::STR_CLOCK_SYNCED, &KomaSettings::clockHasBeenSynced, "clockHasBeenSynced",
                            StrId::STR_CUSTOMISE_STATUS_BAR),
    };
    // Only show tilt page turn setting when the QMI8658 IMU is present (X3)
    if (halTiltSensor.isAvailable()) {
      // Insert after the short power button setting (end of Controls section)
      for (auto it = v.begin(); it != v.end(); ++it) {
        if (it->nameId == StrId::STR_SHORT_PWR_BTN) {
          v.insert(it + 1, SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &KomaSettings::tiltPageTurn,
                                             {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_INVERTED},
                                             "tiltPageTurn", StrId::STR_CAT_CONTROLS));
          break;
        }
      }
    }
    return v;
  }();

  std::vector<SettingInfo> v = baseList;
  if (!BoardConfig::hasTouch()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) { return s.nameId == StrId::STR_TOUCH_READER_CONTROLS; }),
            v.end());
  }
  if (BoardConfig::hasTouch()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             return s.nameId == StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION ||
                                    s.nameId == StrId::STR_SUNLIGHT_FADING_FIX;
                           }),
            v.end());
  }
  if (registry && registry->getFamilyCount() > 0) {
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_FAMILY; });
    if (it != v.end()) {
      *it = buildFontFamilySetting(registry);
    }
  }
  {
    // Unconditional: even with no SD fonts installed the sizes come from the
    // built-in family rather than a fixed Small/Medium/Large/XL enum.
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_SIZE; });
    if (it != v.end()) {
      *it = buildFontSizeSetting(registry);
    }
  }
  if (dictionaries && !dictionaries->empty()) {
    // Insert at the end of the Reader category (just before the first Controls entry).
    auto it =
        std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.category == StrId::STR_CAT_CONTROLS; });
    v.insert(it, buildDictionarySetting(*dictionaries));
  }
  return v;
}
