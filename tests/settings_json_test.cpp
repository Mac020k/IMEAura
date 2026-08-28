#include "core/json.h"
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
  Settings raw = default_settings();
  raw.display_mode = kDisplayModeOnFocus;
  raw.show_on_hover = true;
  raw.ui_font_size = kFontSizeLarge;
  raw.gradient_width = 200;
  loaded = normalize_settings(raw);
  EXPECT(loaded.gradient_width == 100);
  EXPECT(loaded.show_on_hover);
  EXPECT(loaded.aura_slots.size() == 2);
  EXPECT(loaded.aura_slots[0].lang_id == "ja");
  EXPECT(loaded.aura_slots[1].lang_id == "en");

  raw.display_mode = kDisplayModeAlways;
  loaded = normalize_settings(raw);
  EXPECT(!loaded.show_on_hover);

  raw.language = "en";
  loaded = normalize_settings(raw);
  EXPECT(loaded.language == "en");

  raw.language = "zh-Hans";
  loaded = normalize_settings(raw);
  EXPECT(loaded.language == "zh-Hans");

  raw.language = "zz";
  loaded = normalize_settings(raw);
  EXPECT(loaded.language == "ja");

  raw.aura_slots = {{"ja", {1, 2, 3, 4}}, {"ja", {9, 9, 9, 9}}, {"ko", {10, 11, 12, 13}}};
  loaded = normalize_settings(raw);
  EXPECT(loaded.aura_slots.size() == 2);
  EXPECT(loaded.aura_slots[0].lang_id == "ja");
  EXPECT(loaded.aura_slots[1].lang_id == "ko");

  raw.firefly_enabled = true;
  raw.firefly_led_mode = "hid";
  raw.firefly_caps_mode = "preserve";
  loaded = normalize_settings(raw);
  EXPECT(loaded.firefly_enabled);
  EXPECT(loaded.firefly_led_mode == "hid");
  EXPECT(loaded.firefly_caps_mode == "preserve");

  raw.firefly_led_mode = "invalid";
  raw.firefly_caps_mode = "invalid";
  loaded = normalize_settings(raw);
  EXPECT(loaded.firefly_led_mode == "auto");
  EXPECT(loaded.firefly_caps_mode == "uppercase");

  raw.firefly_busy_action = kFireflyBusyKeepAwake;
  raw.firefly_keep_display_on = true;
  loaded = normalize_settings(raw);
  EXPECT(loaded.firefly_busy_action == kFireflyBusyKeepAwake);
  EXPECT(loaded.firefly_keep_display_on);

  raw.firefly_busy_action = "nope";
  loaded = normalize_settings(raw);
  EXPECT(loaded.firefly_busy_action == kFireflyBusyDnd);

  raw.firefly_busy_action = kFireflyBusyVoiceInput;
  raw.firefly_keep_display_on = true;
  loaded = normalize_settings(raw);
  EXPECT(!loaded.firefly_keep_display_on);

  raw.firefly_busy_action = kFireflyBusyCustomKey;
  raw.firefly_custom_vk = 0x74;
  loaded = normalize_settings(raw);
  EXPECT(loaded.firefly_busy_action == kFireflyBusyCustomKey);
  EXPECT(loaded.firefly_custom_vk == 0x74);

  raw.firefly_busy_action = "meeting";
  loaded = normalize_settings(raw);
  EXPECT(loaded.firefly_busy_action == kFireflyBusyDnd);

  raw.display_mode = kDisplayModeHidden;
  raw.aura_enabled = true;
  loaded = normalize_settings(raw);
  EXPECT(!loaded.aura_enabled);
  EXPECT(loaded.display_mode == kDisplayModeAlways);

  // nested aura_colors parse
  {
    json::Value root;
    std::string err;
    const char* text = R"({
      "aura_colors": [
        {"lang":"zh-Hans","color":[16,204,123,255]},
        {"lang":"en","color":[5,6,7,8]}
      ]
    })";
    EXPECT(json::parse(text, root, err));
    const auto* arr = root.find("aura_colors");
    EXPECT(arr && arr->type == json::Value::Type::Array);
    EXPECT(arr->elements.size() == 2);
    EXPECT(arr->elements[0].find("lang")->string_value == "zh-Hans");
  }

  EXPECT(default_settings().default_color_for_new_slot(2).r == kDefaultAuraSlotColors[0].r);
  EXPECT(default_settings().default_color_for_new_slot(3).r == kDefaultAuraSlotColors[1].r);

  (void)path;
  if (failures) return 1;
  std::cout << "settings_json_test: OK\n";
  return 0;
}
