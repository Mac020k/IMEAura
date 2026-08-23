#include "core/firefly.h"
#include "core/settings.h"

#include <iostream>

using namespace imeaura;

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

  {
    FireflyInput in{};
    in.enabled = false;
    in.current_active = true;
    const auto out = evaluate_firefly(in);
    EXPECT(!out.active);
    EXPECT(!out.want_led_on);
    EXPECT(!out.want_dnd);
    EXPECT(out.dnd_write_needed);
  }

  {
    FireflyInput in{};
    in.enabled = true;
    in.current_active = false;
    in.toggle_requested = true;
    const auto out = evaluate_firefly(in);
    EXPECT(out.active);
    EXPECT(out.want_led_on);
    EXPECT(out.want_dnd);
    EXPECT(out.dnd_write_needed);
  }

  {
    FireflyInput in{};
    in.enabled = true;
    in.current_active = true;
    in.toggle_requested = true;
    const auto out = evaluate_firefly(in);
    EXPECT(!out.active);
    EXPECT(!out.want_led_on);
    EXPECT(!out.want_dnd);
  }

  EXPECT(firefly_want_uppercase(kFireflyCapsUppercase, false, false));
  EXPECT(!firefly_want_uppercase(kFireflyCapsUppercase, true, false));
  EXPECT(!firefly_want_uppercase(kFireflyCapsLowercase, false, false));
  EXPECT(firefly_want_uppercase(kFireflyCapsLowercase, true, false));
  EXPECT(firefly_want_uppercase(kFireflyCapsPreserve, false, true));
  EXPECT(!firefly_want_uppercase(kFireflyCapsPreserve, false, false));

  {
    Settings raw{};
    raw.firefly_enabled = true;
    raw.firefly_led_mode = "hid";
    raw.firefly_caps_mode = "preserve";
    Settings n = normalize_settings(raw);
    EXPECT(n.firefly_enabled);
    EXPECT(n.firefly_led_mode == kFireflyLedHid);
    EXPECT(n.firefly_caps_mode == kFireflyCapsPreserve);

    raw.firefly_led_mode = "nope";
    raw.firefly_caps_mode = "nope";
    n = normalize_settings(raw);
    EXPECT(n.firefly_led_mode == kFireflyLedAuto);
    EXPECT(n.firefly_caps_mode == kFireflyCapsUppercase);
  }

  if (failures) return 1;
  std::cout << "firefly_test: OK\n";
  return 0;
}
