#include "core/firefly.h"

namespace imeaura {

namespace {

FireflyBusyEffects effects_for_action(std::string_view action, bool active, bool entering_busy,
                                      bool keep_display_on) {
  FireflyBusyEffects fx{};
  if (!active) return fx;

  const std::string normalized = normalize_busy_action(action);
  if (normalized == kFireflyBusyDnd) {
    fx.want_dnd = true;
  } else if (normalized == kFireflyBusyKeepAwake) {
    fx.want_keep_awake = true;
    fx.keep_display_on = keep_display_on;
  } else if (normalized == kFireflyBusyVoiceInput) {
    fx.trigger_voice_input = entering_busy;
  } else if (normalized == kFireflyBusyMeeting) {
    fx.want_dnd = true;
    fx.want_mic_mute = true;
  } else if (normalized == kFireflyBusyHandsFree) {
    fx.want_dnd = true;
    fx.trigger_voice_input = entering_busy;
  } else {
    fx.want_dnd = true;
  }
  return fx;
}

}  // namespace

FireflyOutput evaluate_firefly(const FireflyInput& in) {
  FireflyOutput out{};
  if (!in.enabled) {
    out.active = false;
    out.want_led_on = false;
    out.effects = {};
    out.want_dnd = false;
    out.effects_write_needed = in.current_active;
    out.dnd_write_needed = in.current_active;
    return out;
  }

  const bool prev_active = in.current_active;
  if (in.toggle_requested) {
    out.active = !in.current_active;
  } else {
    out.active = in.current_active;
  }

  const bool entering_busy = out.active && (in.toggle_requested ? !prev_active : false);
  out.want_led_on = out.active;
  out.effects = effects_for_action(in.busy_action, out.active, entering_busy, in.keep_display_on);
  out.want_dnd = out.effects.want_dnd;
  out.effects_write_needed = in.toggle_requested;
  out.dnd_write_needed = in.toggle_requested;
  return out;
}

FireflyBusyEffects resolve_busy_effects(std::string_view busy_action, bool active, bool entering_busy,
                                        bool keep_display_on) {
  return effects_for_action(busy_action, active, entering_busy, keep_display_on);
}

std::string normalize_busy_action(std::string_view action) {
  if (action == kFireflyBusyKeepAwake) return kFireflyBusyKeepAwake;
  if (action == kFireflyBusyVoiceInput) return kFireflyBusyVoiceInput;
  if (action == kFireflyBusyMeeting) return kFireflyBusyMeeting;
  if (action == kFireflyBusyHandsFree) return kFireflyBusyHandsFree;
  return kFireflyBusyDnd;
}

const std::vector<std::string_view>& busy_action_catalog() {
  static const std::vector<std::string_view> kCatalog = {
      kFireflyBusyDnd, kFireflyBusyKeepAwake, kFireflyBusyVoiceInput,
      kFireflyBusyMeeting, kFireflyBusyHandsFree,
  };
  return kCatalog;
}

bool busy_action_requires_dnd(std::string_view action) {
  const std::string a = normalize_busy_action(action);
  return a == kFireflyBusyDnd || a == kFireflyBusyMeeting || a == kFireflyBusyHandsFree;
}

bool busy_action_requires_keep_awake(std::string_view action) {
  return normalize_busy_action(action) == kFireflyBusyKeepAwake;
}

bool busy_action_requires_mic_mute(std::string_view action) {
  return normalize_busy_action(action) == kFireflyBusyMeeting;
}

bool busy_action_requires_voice_input(std::string_view action) {
  const std::string a = normalize_busy_action(action);
  return a == kFireflyBusyVoiceInput || a == kFireflyBusyHandsFree;
}

bool busy_action_supported(std::string_view action, const FireflyCapabilities& caps) {
  const std::string a = normalize_busy_action(action);
  if (a == kFireflyBusyDnd) return caps.can_set_dnd;
  if (a == kFireflyBusyKeepAwake) return caps.can_keep_awake;
  if (a == kFireflyBusyVoiceInput) return caps.can_trigger_voice_input;
  if (a == kFireflyBusyMeeting) return caps.can_set_dnd && caps.can_mute_mic;
  if (a == kFireflyBusyHandsFree) return caps.can_set_dnd && caps.can_trigger_voice_input;
  return caps.can_set_dnd;
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
