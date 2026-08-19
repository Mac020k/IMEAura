#pragma once

namespace imeaura {

inline constexpr const char* kFireflyLedAuto = "auto";
inline constexpr const char* kFireflyLedHid = "hid";
inline constexpr const char* kFireflyLedNone = "none";

inline constexpr const char* kLangJa = "ja";
inline constexpr const char* kLangEn = "en";

struct FireflyInput {
  bool enabled = false;
  bool toggle_requested = false;
  bool current_active = false;
};

struct FireflyOutput {
  bool active = false;
  bool want_led_on = false;
  bool want_dnd = false;
  bool dnd_write_needed = false;
};

FireflyOutput evaluate_firefly(const FireflyInput& in);

}  // namespace imeaura
