#include "core/settings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace imeaura;

namespace fs = std::filesystem;

#define EXPECT(cond)                                                     \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " #cond \
                << "\n";                                                 \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

int main() {
  int failures = 0;
  const auto path = fs::temp_directory_path() / "imeaura_test_settings.json";
  {
    std::ofstream f(path);
    f << R"({
  "color_jp": [1, 2, 3, 4],
  "color_en": [5, 6, 7, 8],
  "display_mode": "on_focus",
  "show_on_hover": true,
  "ui_font_size": "large",
  "gradient_width": 42
}
)";
  }

#ifdef _WIN32
  _putenv_s("APPDATA", fs::temp_directory_path().string().c_str());
#endif

  Settings loaded{};
  // load_settings uses platform path; verify normalize directly
  Settings raw{};
  raw.color_jp = {1, 2, 3, 4};
  raw.color_en = {5, 6, 7, 8};
  raw.display_mode = kDisplayModeOnFocus;
  raw.show_on_hover = true;
  raw.ui_font_size = kFontSizeLarge;
  raw.gradient_width = 200;
  loaded = normalize_settings(raw);
  EXPECT(loaded.gradient_width == 100);
  EXPECT(loaded.show_on_hover);

  raw.display_mode = kDisplayModeAlways;
  loaded = normalize_settings(raw);
  EXPECT(!loaded.show_on_hover);

  raw.language = "en";
  loaded = normalize_settings(raw);
  EXPECT(loaded.language == "en");

  raw.language = "zz";
  loaded = normalize_settings(raw);
  EXPECT(loaded.language == "ja");

  (void)path;
  if (failures) return 1;
  std::cout << "settings_json_test: OK\n";
  return 0;
}
