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

}  // namespace imeaura
