#include "core/i18n.h"

#include <cstddef>

namespace imeaura {
namespace {

// clang-format off
const wchar_t* kJa[static_cast<size_t>(StringId::kCount)] = {
  L"IME Aura",                                   // kSettingsTitle
  L"Aura",                                       // kTabAura
  L"\u4E00\u822C",                               // kTabGeneral (一般)
  L"\u8272",                                     // kColorSection (色)
  L"\u30AF\u30EA\u30C3\u30AF\u3057\u3066\u753B\u9762\u7E01\u306E\u8272\u3092\u5909\u66F4\u3057\u307E\u3059", // kColorSub
  L"\u65E5\u672C\u8A9E",                         // kColorJp (日本語)
  L"\u82F1\u8A9E",                               // kColorEn (英語)
  L"\u30C7\u30D5\u30A9\u30EB\u30C8\u306E\u8272\u306B\u623B\u3059", // kColorReset
  L"\u623B\u3057\u307E\u3057\u305F",             // kColorResetDone
  L"\u30B0\u30E9\u30C7\u30FC\u30B7\u30E7\u30F3\u306E\u5E45", // kWidthSection
  L"\u753B\u9762\u7E01\u306E\u5E2F\u306E\u539A\u3055 (1-100 px)", // kWidthSub
  L"\u30C7\u30D5\u30A9\u30EB\u30C8\u306E\u5E45\u306B\u623B\u3059", // kWidthReset
  L"\u623B\u3057\u307E\u3057\u305F",             // kWidthResetDone
  L"\u30B0\u30E9\u30C7\u30FC\u30B7\u30E7\u30F3\u8868\u793A", // kDisplaySection
  L"\u5E38\u306B\u8868\u793A",                   // kDisplayAlways
  L"\u30C6\u30AD\u30B9\u30C8\u5165\u529B\u6642\u306E\u307F", // kDisplayFocus
  L"\u975E\u8868\u793A",                         // kDisplayHidden
  L"\u30C6\u30AD\u30B9\u30C8\u30DC\u30C3\u30AF\u30B9\u3078\u30DB\u30D0\u30FC\u6642\u3082\u8868\u793A", // kDisplayHover
  L"\u6587\u5B57\u30B5\u30A4\u30BA",             // kFontSection
  L"\u3053\u306E\u30A6\u30A3\u30F3\u30C9\u30A6\u306E\u6587\u5B57\u306E\u5927\u304D\u3055", // kFontSub
  L"\u5C0F",                                     // kFontSmall
  L"\u4E2D",                                     // kFontMedium
  L"\u5927",                                     // kFontLarge
  L"\u8A00\u8A9E",                               // kLangSection
  L"\u65E5\u672C\u8A9E",                         // kLangJa
  L"English",                                    // kLangEn
  L"\u30D0\u30FC\u30B8\u30E7\u30F3\u60C5\u5831...", // kAbout
  L"\u30A2\u30D7\u30EA\u30B1\u30FC\u30B7\u30E7\u30F3\u3092\u7D42\u4E86", // kQuit
  L"IME Aura",                                   // kQuitConfirmTitle
  L"IME Aura \u3092\u7D42\u4E86\u3057\u307E\u3059\u304B\uFF1F\n\u753B\u9762\u7E01\u306E\u30B0\u30E9\u30C7\u30FC\u30B7\u30E7\u30F3\u8868\u793A\u3082\u6D88\u3048\u307E\u3059\u3002", // kQuitConfirmBody
  L"\u8272\u3092\u9078\u629E",                   // kColorDialogTitle
  L"OK",                                         // kOk
  L"\u30AD\u30E3\u30F3\u30BB\u30EB",             // kCancel
  L"\u8A2D\u5B9A\u3092\u958B\u304F",             // kTrayOpen
  L"\u7D42\u4E86",                               // kTrayQuit
};

const wchar_t* kEn[static_cast<size_t>(StringId::kCount)] = {
  L"IME Aura",                                   // kSettingsTitle
  L"Aura",                                       // kTabAura
  L"General",                                    // kTabGeneral
  L"Color",                                      // kColorSection
  L"Click to change the screen edge color",      // kColorSub
  L"Japanese",                                   // kColorJp
  L"English",                                    // kColorEn
  L"Reset to default colors",                    // kColorReset
  L"Reset",                                      // kColorResetDone
  L"Gradient Width",                             // kWidthSection
  L"Screen edge thickness (1-100 px)",           // kWidthSub
  L"Reset to default width",                     // kWidthReset
  L"Reset",                                      // kWidthResetDone
  L"Gradient Display",                           // kDisplaySection
  L"Always show",                                // kDisplayAlways
  L"Only when text input is focused",            // kDisplayFocus
  L"Hidden",                                     // kDisplayHidden
  L"Also show when hovering over text boxes",    // kDisplayHover
  L"Font Size",                                  // kFontSection
  L"Text size in this window",                   // kFontSub
  L"S",                                          // kFontSmall
  L"M",                                          // kFontMedium
  L"L",                                          // kFontLarge
  L"Language",                                   // kLangSection
  L"\u65E5\u672C\u8A9E",                         // kLangJa
  L"English",                                    // kLangEn
  L"About...",                                   // kAbout
  L"Quit Application",                           // kQuit
  L"IME Aura",                                   // kQuitConfirmTitle
  L"Quit IME Aura?\nThe screen edge gradient will also disappear.", // kQuitConfirmBody
  L"Choose Color",                               // kColorDialogTitle
  L"OK",                                         // kOk
  L"Cancel",                                     // kCancel
  L"Open Settings",                              // kTrayOpen
  L"Quit",                                       // kTrayQuit
};
// clang-format on

}  // namespace

const wchar_t* tr(Lang lang, StringId id) {
  const auto idx = static_cast<size_t>(id);
  if (idx >= static_cast<size_t>(StringId::kCount)) return L"";
  if (lang == Lang::En && kEn[idx]) return kEn[idx];
  return kJa[idx] ? kJa[idx] : L"";
}

Lang lang_from_key(const std::string& key) {
  if (key == "en") return Lang::En;
  return Lang::Ja;
}

const wchar_t* lang_font_family(Lang lang) {
  return (lang == Lang::En) ? L"Segoe UI" : L"Yu Gothic UI";
}

}  // namespace imeaura
