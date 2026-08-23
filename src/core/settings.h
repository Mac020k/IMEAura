#pragma once

#include "core/color.h"
#include "core/firefly.h"
#include "core/input_languages.h"
#include "core/tokens.h"

#include <string>
#include <string_view>
#include <vector>

namespace imeaura {

inline constexpr const char* kDisplayModeAlways = "always";
inline constexpr const char* kDisplayModeOnFocus = "on_focus";
// Legacy JSON value; migrated to aura_enabled=false on load.
inline constexpr const char* kDisplayModeHidden = "hidden";

inline constexpr const char* kFontSizeSmall = "small";
inline constexpr const char* kFontSizeMedium = "medium";
inline constexpr const char* kFontSizeLarge = "large";

inline constexpr const char* kLangJa = kInputJa;
inline constexpr const char* kLangEn = kInputEn;
inline constexpr const char* kLangZhHans = kInputZhHans;
inline constexpr const char* kLangZhHant = kInputZhHant;
inline constexpr const char* kLangKo = kInputKo;

struct AuraColorSlot {
  std::string lang_id;
  Rgba color{};
};

struct Settings {
  std::vector<AuraColorSlot> aura_slots;
  bool aura_enabled = true;
  std::string display_mode = kDisplayModeAlways;
  bool show_on_hover = false;
  std::string ui_font_size = kFontSizeMedium;
  int gradient_width = kDefaultGradientWidth;
  bool firefly_enabled = false;
  std::string firefly_led_mode = kFireflyLedAuto;
  std::string firefly_caps_mode = kFireflyCapsUppercase;
  std::string language = kLangJa;

  Rgba color_for_lang(std::string_view lang_id) const;
  Rgba default_color_for_new_slot(size_t existing_count) const;
};

Settings default_settings();
Settings normalize_settings(const Settings& raw);
std::string normalize_language(const std::string& v);
std::string settings_path();
bool load_settings(Settings& out);
bool save_settings(const Settings& settings);
int ui_font_point_size(const std::string& size_key);

}  // namespace imeaura
