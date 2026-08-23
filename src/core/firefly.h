#pragma once

#include <string>

namespace imeaura {

inline constexpr const char* kFireflyLedAuto = "auto";
inline constexpr const char* kFireflyLedHid = "hid";
inline constexpr const char* kFireflyLedNone = "none";

inline constexpr const char* kFireflyCapsPreserve = "preserve";
inline constexpr const char* kFireflyCapsUppercase = "uppercase";
inline constexpr const char* kFireflyCapsLowercase = "lowercase";

struct FireflyInput {
  bool enabled = false;
  bool toggle_requested = false;
  bool current_active = false;  // true = Busy / Do Not Disturb
};

struct FireflyOutput {
  bool active = false;  // Busy when true, Available when false
  bool want_led_on = false;
  bool want_dnd = false;
  bool dnd_write_needed = false;
};

// Pure state machine: Available <-> Busy while enabled.
FireflyOutput evaluate_firefly(const FireflyInput& in);

// Desired letter case for Latin input: base XOR shift.
// Returns true for uppercase, false for lowercase.
bool firefly_want_uppercase(const std::string& caps_mode, bool shift_down, bool preserved_caps_on);

}  // namespace imeaura
