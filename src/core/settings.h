#pragma once

#include "core/color.h"
#include "core/firefly.h"
#include "core/tokens.h"

#include <string>

namespace imeaura {

inline constexpr const char* kDisplayModeAlways = "always";
inline constexpr const char* kDisplayModeOnFocus = "on_focus";
inline constexpr const char* kDisplayModeHidden = "hidden";

inline constexpr const char* kFontSizeSmall = "small";
inline constexpr const char* kFontSizeMedium = "medium";
inline constexpr const char* kFontSizeLarge = "large";

struct Settings {
  Rgba color_jp = kDefaultColorJp;
  Rgba color_en = kDefaultColorEn;
  std::string display_mode = kDisplayModeAlways;
  bool show_on_hover = false;
  std::string ui_font_size = kFontSizeMedium;
  int gradient_width = kDefaultGradientWidth;
  bool firefly_enabled = false;
  std::string firefly_led_mode = kFireflyLedAuto;
  std::string firefly_caps_mode = kFireflyCapsUppercase;
  std::string language = kLangJa;
};

Settings default_settings();
Settings normalize_settings(const Settings& raw);
std::string settings_path();
bool load_settings(Settings& out);
bool save_settings(const Settings& settings);
int ui_font_point_size(const std::string& size_key);

}  // namespace imeaura
