#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace imeaura {

inline constexpr const char* kFireflyLedAuto = "auto";
inline constexpr const char* kFireflyLedHid = "hid";
inline constexpr const char* kFireflyLedNone = "none";

inline constexpr const char* kFireflyCapsPreserve = "preserve";
inline constexpr const char* kFireflyCapsUppercase = "uppercase";
inline constexpr const char* kFireflyCapsLowercase = "lowercase";

inline constexpr const char* kFireflyBusyDnd = "dnd";
inline constexpr const char* kFireflyBusyKeepAwake = "keep_awake";
inline constexpr const char* kFireflyBusyVoiceInput = "voice_input";
inline constexpr const char* kFireflyBusyAudioMute = "audio_mute";
inline constexpr const char* kFireflyBusyCustomKey = "custom_key";

struct FireflyCapabilities {
  bool can_intercept_caps = false;
  bool can_drive_led = false;
  bool can_set_dnd = false;
  bool can_keep_awake = false;
  bool can_mute_mic = false;
  bool can_mute_speaker = false;
  bool can_trigger_voice_input = false;
  bool can_trigger_custom_key = false;
};

struct FireflyBusyEffects {
  bool want_dnd = false;
  bool want_keep_awake = false;
  bool want_mic_mute = false;
  bool want_speaker_mute = false;
  bool keep_display_on = false;
  bool trigger_voice_input = false;  // pulse on Active entry only
  bool trigger_custom_key = false;   // pulse on Active entry only
  int custom_vk = 0;
};

struct FireflyInput {
  bool enabled = false;
  bool toggle_requested = false;
  bool current_active = false;  // true = Active
  std::string busy_action = kFireflyBusyDnd;
  bool keep_display_on = false;
  int custom_vk = 0;
};

struct FireflyOutput {
  bool active = false;  // Active when true, inactive when false
  bool want_led_on = false;
  FireflyBusyEffects effects{};
  bool effects_write_needed = false;
  // Legacy fields for callers that still read these:
  bool want_dnd = false;
  bool dnd_write_needed = false;
};

// Pure state machine: inactive <-> Active while enabled.
FireflyOutput evaluate_firefly(const FireflyInput& in);

// Resolve sustained/pulse effects for a busy action when active or inactive.
FireflyBusyEffects resolve_busy_effects(std::string_view busy_action, bool active, bool entering_active,
                                        bool keep_display_on, int custom_vk);

std::string normalize_busy_action(std::string_view action);
const std::vector<std::string_view>& busy_action_catalog();

bool busy_action_requires_dnd(std::string_view action);
bool busy_action_requires_keep_awake(std::string_view action);
bool busy_action_requires_mic_mute(std::string_view action);
bool busy_action_requires_speaker_mute(std::string_view action);
bool busy_action_requires_voice_input(std::string_view action);
bool busy_action_requires_custom_key(std::string_view action);

bool busy_action_supported(std::string_view action, const FireflyCapabilities& caps, int custom_vk = 0);

// Desired letter case for Latin input: base XOR shift.
// Returns true for uppercase, false for lowercase.
bool firefly_want_uppercase(const std::string& caps_mode, bool shift_down, bool preserved_caps_on);

}  // namespace imeaura
