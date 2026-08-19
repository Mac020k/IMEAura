#include "core/firefly.h"

#include <cstdlib>
#include <iostream>

using namespace imeaura;

static int failures = 0;
#define EXPECT(cond)                                                     \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << "FAIL: " #cond " (" __FILE__ ":" << __LINE__ << ")\n"; \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

int main() {
  // Disabled: no output
  {
    FireflyInput in{};
    in.enabled = false;
    in.toggle_requested = true;
    in.current_active = false;
    auto out = evaluate_firefly(in);
    EXPECT(!out.active);
    EXPECT(!out.want_led_on);
    EXPECT(!out.want_dnd);
    EXPECT(!out.dnd_write_needed);
  }

  // Disabled but was active: needs DND write to turn off
  {
    FireflyInput in{};
    in.enabled = false;
    in.current_active = true;
    auto out = evaluate_firefly(in);
    EXPECT(!out.active);
    EXPECT(out.dnd_write_needed);
  }

  // Enabled, toggle requested, was off -> becomes on
  {
    FireflyInput in{};
    in.enabled = true;
    in.toggle_requested = true;
    in.current_active = false;
    auto out = evaluate_firefly(in);
    EXPECT(out.active);
    EXPECT(out.want_led_on);
    EXPECT(out.want_dnd);
    EXPECT(out.dnd_write_needed);
  }

  // Enabled, toggle requested, was on -> becomes off
  {
    FireflyInput in{};
    in.enabled = true;
    in.toggle_requested = true;
    in.current_active = true;
    auto out = evaluate_firefly(in);
    EXPECT(!out.active);
    EXPECT(!out.want_led_on);
    EXPECT(!out.want_dnd);
    EXPECT(out.dnd_write_needed);
  }

  // Enabled, no toggle, stays same
  {
    FireflyInput in{};
    in.enabled = true;
    in.toggle_requested = false;
    in.current_active = true;
    auto out = evaluate_firefly(in);
    EXPECT(out.active);
    EXPECT(out.want_led_on);
    EXPECT(out.want_dnd);
    EXPECT(!out.dnd_write_needed);
  }

  if (failures) return 1;
  std::cout << "firefly_test: OK\n";
  return 0;
}
