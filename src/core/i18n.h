#pragma once

#include <string>

namespace imeaura {

enum class Lang { Ja, En };

enum class StringId {
  kSettingsTitle,
  kTabAura,
  kTabGeneral,
  kColorSection,
  kColorSub,
  kColorJp,
  kColorEn,
  kColorReset,
  kColorResetDone,
  kWidthSection,
  kWidthSub,
  kWidthReset,
  kWidthResetDone,
  kDisplaySection,
  kDisplayAlways,
  kDisplayFocus,
  kDisplayHidden,
  kDisplayHover,
  kFontSection,
  kFontSub,
  kFontSmall,
  kFontMedium,
  kFontLarge,
  kLangSection,
  kLangJa,
  kLangEn,
  kAbout,
  kQuit,
  kQuitConfirmTitle,
  kQuitConfirmBody,
  kColorDialogTitle,
  kOk,
  kCancel,
  kTrayOpen,
  kTrayQuit,
  kCount
};

const wchar_t* tr(Lang lang, StringId id);
Lang lang_from_key(const std::string& key);
const wchar_t* lang_font_family(Lang lang);

}  // namespace imeaura
