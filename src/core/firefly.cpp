#include "core/firefly.h"

namespace imeaura {

FireflyOutput evaluate_firefly(const FireflyInput& in) {
  FireflyOutput out{};
  if (!in.enabled) {
    out.active = false;
    out.want_led_on = false;
    out.want_dnd = false;
    out.dnd_write_needed = in.current_active;
    return out;
  }
  if (in.toggle_requested) {
    out.active = !in.current_active;
  } else {
    out.active = in.current_active;
  }
  out.want_led_on = out.active;
  out.want_dnd = out.active;
  out.dnd_write_needed = in.toggle_requested;
  return out;
}

bool firefly_want_uppercase(const std::string& caps_mode, bool shift_down, bool preserved_caps_on) {
  bool base_upper = true;
  if (caps_mode == kFireflyCapsLowercase) {
    base_upper = false;
  } else if (caps_mode == kFireflyCapsPreserve) {
    base_upper = preserved_caps_on;
  } else {
    base_upper = true;  // uppercase default
  }
  return base_upper != shift_down;
}

}  // namespace imeaura
