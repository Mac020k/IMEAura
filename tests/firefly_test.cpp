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
    EXPECT(!out.effects.want_dnd);
    EXPECT(!out.want_dnd);
    EXPECT(out.dnd_write_needed);
  }

  {
    FireflyInput in{};
    in.enabled = true;
    in.current_active = false;
    in.toggle_requested = true;
    in.busy_action = kFireflyBusyDnd;
    const auto out = evaluate_firefly(in);
    EXPECT(out.active);
    EXPECT(out.want_led_on);
    EXPECT(out.effects.want_dnd);
    EXPECT(out.want_dnd);
    EXPECT(out.dnd_write_needed);
  }

  {
    FireflyInput in{};
    in.enabled = true;
    in.current_active = true;
    in.toggle_requested = true;
    in.busy_action = kFireflyBusyDnd;
    const auto out = evaluate_firefly(in);
    EXPECT(!out.active);
    EXPECT(!out.want_led_on);
    EXPECT(!out.effects.want_dnd);
    EXPECT(!out.want_dnd);
  }

  {
    FireflyInput in{};
    in.enabled = true;
    in.current_active = false;
    in.toggle_requested = true;
    in.busy_action = kFireflyBusyKeepAwake;
    in.keep_display_on = true;
    const auto out = evaluate_firefly(in);
    EXPECT(out.effects.want_keep_awake);
    EXPECT(out.effects.keep_display_on);
    EXPECT(!out.effects.want_dnd);
  }

  {
    FireflyInput in{};
    in.enabled = true;
    in.current_active = false;
    in.toggle_requested = true;
    in.busy_action = kFireflyBusyVoiceInput;
    const auto out = evaluate_firefly(in);
    EXPECT(out.effects.trigger_voice_input);
    EXPECT(!out.effects.want_dnd);
  }

  {
    FireflyInput in{};
    in.enabled = true;
    in.current_active = true;
    in.toggle_requested = true;
    in.busy_action = kFireflyBusyVoiceInput;
    const auto out = evaluate_firefly(in);
    EXPECT(!out.effects.trigger_voice_input);
  }

  {
    FireflyInput in{};
    in.enabled = true;
    in.current_active = false;
    in.toggle_requested = true;
    in.busy_action = kFireflyBusyMeeting;
    const auto out = evaluate_firefly(in);
    EXPECT(out.effects.want_dnd);
    EXPECT(out.effects.want_mic_mute);
  }

  {
    FireflyInput in{};
    in.enabled = true;
    in.current_active = false;
    in.toggle_requested = true;
    in.busy_action = kFireflyBusyHandsFree;
    const auto out = evaluate_firefly(in);
    EXPECT(out.effects.want_dnd);
    EXPECT(out.effects.trigger_voice_input);
  }

  {
    FireflyCapabilities caps{};
    caps.can_set_dnd = true;
    caps.can_keep_awake = true;
    caps.can_mute_mic = true;
    caps.can_trigger_voice_input = true;
    EXPECT(busy_action_supported(kFireflyBusyDnd, caps));
    EXPECT(busy_action_supported(kFireflyBusyMeeting, caps));
    EXPECT(busy_action_supported(kFireflyBusyHandsFree, caps));
    caps.can_mute_mic = false;
    EXPECT(!busy_action_supported(kFireflyBusyMeeting, caps));
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
    raw.firefly_busy_action = kFireflyBusyMeeting;
    raw.firefly_keep_display_on = true;
    Settings n = normalize_settings(raw);
    EXPECT(n.firefly_enabled);
    EXPECT(n.firefly_led_mode == kFireflyLedHid);
    EXPECT(n.firefly_caps_mode == kFireflyCapsPreserve);
    EXPECT(n.firefly_busy_action == kFireflyBusyMeeting);

    raw.firefly_busy_action = "invalid";
    n = normalize_settings(raw);
    EXPECT(n.firefly_busy_action == kFireflyBusyDnd);

    raw.firefly_busy_action = kFireflyBusyVoiceInput;
    raw.firefly_keep_display_on = true;
    n = normalize_settings(raw);
    EXPECT(!n.firefly_keep_display_on);
  }

  if (failures) return 1;
  std::cout << "firefly_test: OK\n";
  return 0;
}
