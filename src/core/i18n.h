#pragma once

#include <string>

namespace imeaura {

enum class Lang { Ja, En, ZhHans, ZhHant, Ko };

enum class StringId {
  kSettingsTitle,
  kTabAura,
  kTabFirefly,
  kTabGeneral,
  kAuraTitle,
  kAuraSub,
  kAuraEnable,
  kColorSection,
  kColorSub,
  kColorJp,
  kColorEn,
  kColorReset,
  kColorResetDone,
  kAddColorSlot,
  kRemoveColorSlot,
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
  kLangChange,
  kLangBack,
  kLangJa,
  kLangEn,
  kLangZhHans,
  kLangZhHant,
  kLangKo,
  kEasyQuit,
  kAbout,
  kQuit,
  kQuitConfirmTitle,
  kQuitConfirmBody,
  kFireflyTitle,
  kFireflySub,
  kFireflyEnable,
  kFireflyStateBusy,
  kFireflyStateAvailable,
  kFireflyCapsSection,
  kFireflyCapsPreserve,
  kFireflyCapsUppercase,
  kFireflyCapsLowercase,
  kFireflyCapsOk,
  kFireflyLedOk,
  kFireflyDndOk,
  kFireflyUnsupported,
  kFireflyBusySection,
  kFireflyBusyChange,
  kFireflyBusyBack,
  kFireflyBusyDnd,
  kFireflyBusyKeepAwake,
  kFireflyBusyVoiceInput,
  kFireflyBusyAudioMute,
  kFireflyBusyCustomKey,
  kFireflyKeepDisplayOn,
  kFireflyKeepAwakeOk,
  kFireflyVoiceOk,
  kFireflyMicOk,
  kFireflySpeakerOk,
  kFireflyCustomKeyPrompt,
  kColorDialogTitle,
  kColorDialogRgb,
  kColorDialogHsb,
  kColorDialogPresets,
  kOk,
  kCancel,
  kTrayOpen,
  kTrayQuit,
  kCount
};

const wchar_t* tr(Lang lang, StringId id);
Lang lang_from_key(const std::string& key);
const wchar_t* lang_font_family(Lang lang);
StringId string_id_for_ui_lang(const std::string& key);
StringId string_id_for_busy_action(const std::string& action_key);

}  // namespace imeaura
