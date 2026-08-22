#pragma once

#include "core/color.h"
#include "core/tokens.h"

#include <string>

namespace imeaura {

inline constexpr const char* kDisplayModeAlways = "always";
inline constexpr const char* kDisplayModeOnFocus = "on_focus";
inline constexpr const char* kDisplayModeHidden = "hidden";

inline constexpr const char* kFontSizeSmall = "small";
inline constexpr const char* kFontSizeMedium = "medium";
inline constexpr const char* kFontSizeLarge = "large";

inline constexpr const char* kLangJa = "ja";
inline constexpr const char* kLangEn = "en";

struct Settings {
  Rgba color_jp = kDefaultColorJp;
  Rgba color_en = kDefaultColorEn;
  std::string display_mode = kDisplayModeAlways;
  bool show_on_hover = false;
  std::string ui_font_size = kFontSizeMedium;
  int gradient_width = kDefaultGradientWidth;
  std::string language = kLangJa;
};

Settings default_settings();
Settings normalize_settings(const Settings& raw);
std::string settings_path();
bool load_settings(Settings& out);
bool save_settings(const Settings& settings);
int ui_font_point_size(const std::string& size_key);

}  // namespace imeaura
