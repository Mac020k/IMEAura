#include "platform/macos/mac_firefly.h"

#include "core/firefly.h"
#include "platform/macos/mac_ime.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreAudio/CoreAudio.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <dispatch/dispatch.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace imeaura {
namespace {

constexpr int64_t kFireflyInjectTag = 0x46495245u;  // 'FIRE'
constexpr CGKeyCode kVkCapsLock = 0x39;

MacFireflyBackend* g_self = nullptr;
CFMachPortRef g_tap_port = nullptr;

bool RunCommand(const std::string& cmd) { return std::system(cmd.c_str()) == 0; }

bool CapsLockOn() { return CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVkCapsLock); }

void SendTaggedVk(CGKeyCode key, bool down) {
  CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, key, down);
  if (!ev) return;
  CGEventSetIntegerValueField(ev, kCGEventSourceUserData, kFireflyInjectTag);
  CGEventPost(kCGHIDEventTap, ev);
  CFRelease(ev);
}

void SetCapsLockState(bool on) {
  if (CapsLockOn() == on) return;
  SendTaggedVk(kVkCapsLock, true);
  SendTaggedVk(kVkCapsLock, false);
}

bool ShiftDown() {
  return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 0x38) ||  // Left shift
         CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 0x3C);    // Right shift
}

bool ModifiersDown() {
  return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 0x3B) ||  // Control
         CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 0x3A) ||  // Option
         CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 0x37) ||  // Command
         CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 0x36);    // Right command
}

void SendUnicodeChar(CFStringRef ch, bool down) {
  UniChar uni[2]{};
  CFIndex len = 0;
  CFStringGetCharacters(ch, CFRangeMake(0, CFStringGetLength(ch)), uni);
  len = CFStringGetLength(ch);
  if (len <= 0) return;
  CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, 0, down);
  if (!ev) return;
  CGEventKeyboardSetUnicodeString(ev, static_cast<UniCharCount>(len), uni);
  CGEventSetIntegerValueField(ev, kCGEventSourceUserData, kFireflyInjectTag);
  CGEventPost(kCGHIDEventTap, ev);
  CFRelease(ev);
}

bool IsInjected(CGEventRef event) {
  return CGEventGetIntegerValueField(event, kCGEventSourceUserData) == kFireflyInjectTag;
}

std::string DndBackupPath() {
  const char* home = std::getenv("HOME");
  if (!home) return {};
  return (std::filesystem::path(home) / "Library" / "Application Support" / "IMEAura" / "dnd_backup.txt").string();
}

bool ReadDndState(bool& on) {
  const std::string cmd =
      "defaults -currentHost read com.apple.notificationcenterui doNotDisturb 2>/dev/null";
  FILE* f = popen(cmd.c_str(), "r");
  if (!f) return false;
  char buf[64]{};
  const bool got = fgets(buf, sizeof(buf), f) != nullptr;
  pclose(f);
  if (!got) return false;
  on = std::strncmp(buf, "1", 1) == 0 || std::strncmp(buf, "true", 4) == 0;
  return true;
}

bool WriteDndState(bool on) {
  const char* val = on ? "true" : "false";
  std::string cmd =
      std::string("defaults -currentHost write com.apple.notificationcenterui doNotDisturb -boolean ") + val;
  return RunCommand(cmd);
}

bool ProbeDnd() {
  bool dummy = false;
  return ReadDndState(dummy) || WriteDndState(false);
}

bool ProbeKeepAwake() {
  IOPMAssertionID id = 0;
  const IOReturn rc =
      IOPMAssertionCreateWithName(kIOPMAssertionTypeNoIdleSleep, kIOPMAssertionLevelOn, CFSTR("IMEAura Firefly"), &id);
  if (rc == kIOReturnSuccess) IOPMAssertionRelease(id);
  return rc == kIOReturnSuccess;
}

bool GetDefaultInputDevice(AudioDeviceID* out) {
  if (!out) return false;
  AudioObjectPropertyAddress addr = {kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
  UInt32 size = sizeof(*out);
  return AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, out) == noErr && *out != 0;
}

bool ProbeMicMute() {
  AudioDeviceID device = 0;
  if (!GetDefaultInputDevice(&device)) return false;
  AudioObjectPropertyAddress mute_addr = {kAudioDevicePropertyMute, kAudioDevicePropertyScopeInput,
                                          kAudioObjectPropertyElementMain};
  return AudioObjectHasProperty(device, &mute_addr);
}

bool GetDefaultOutputDevice(AudioDeviceID* out) {
  if (!out) return false;
  AudioObjectPropertyAddress addr = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
  UInt32 size = sizeof(*out);
  return AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, out) == noErr && *out != 0;
}

void SetDeviceMute(AudioDeviceID device, AudioObjectPropertyScope scope, bool on, bool& saved_valid, UInt32& was_muted) {
  AudioObjectPropertyAddress mute_addr = {kAudioDevicePropertyMute, scope, kAudioObjectPropertyElementMain};
  if (!AudioObjectHasProperty(device, &mute_addr)) return;
  if (on) {
    if (!saved_valid) {
      UInt32 size = sizeof(was_muted);
      AudioObjectGetPropertyData(device, &mute_addr, 0, nullptr, &size, &was_muted);
      saved_valid = true;
    }
    const UInt32 mute = 1;
    AudioObjectSetPropertyData(device, &mute_addr, 0, nullptr, sizeof(mute), &mute);
  } else if (saved_valid) {
    AudioObjectSetPropertyData(device, &mute_addr, 0, nullptr, sizeof(was_muted), &was_muted);
    saved_valid = false;
  }
}

bool ProbeSpeakerMute() {
  AudioDeviceID device = 0;
  if (!GetDefaultOutputDevice(&device)) return false;
  AudioObjectPropertyAddress mute_addr = {kAudioDevicePropertyMute, kAudioDevicePropertyScopeOutput,
                                          kAudioObjectPropertyElementMain};
  return AudioObjectHasProperty(device, &mute_addr);
}

bool ProbeCustomKey() { return true; }

bool ProbeVoiceInput() { return true; }

void SendDictationShortcut() {
  constexpr CGKeyCode kFn = 0x3F;
  SendTaggedVk(kFn, true);
  SendTaggedVk(kFn, false);
  SendTaggedVk(kFn, true);
  SendTaggedVk(kFn, false);
}

void BackupDnd() {
  bool on = false;
  if (!ReadDndState(on)) return;
  auto path = DndBackupPath();
  if (path.empty()) return;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream f(path);
  f << (on ? "1" : "0");
}

void RestoreDnd() {
  auto path = DndBackupPath();
  if (path.empty() || !std::filesystem::exists(path)) return;
  std::ifstream f(path);
  char c = '0';
  f >> c;
  WriteDndState(c == '1');
  std::filesystem::remove(path);
}

CGEventRef EventTapCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void*) {
  if (!g_self) return event;
  if (IsInjected(event)) return event;

  if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
    if (g_tap_port) CGEventTapEnable(g_tap_port, true);
    return event;
  }

  if (type != kCGEventKeyDown && type != kCGEventKeyUp) return event;

  const CGKeyCode key = static_cast<CGKeyCode>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
  const bool down = type == kCGEventKeyDown;

  if (key == kVkCapsLock) {
    if (down) {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (g_self) g_self->handle_toggle();
      });
    }
    return nullptr;
  }

  // macOS letter keycodes are not contiguous A..Z; map known ANSI codes explicitly.
  static constexpr CGKeyCode kLetterKeys[26] = {
      0x00, 0x0B, 0x08, 0x02, 0x0E, 0x03, 0x05, 0x04, 0x22, 0x26, 0x28, 0x25, 0x2E,
      0x2D, 0x1F, 0x23, 0x0C, 0x0F, 0x01, 0x11, 0x20, 0x09, 0x0D, 0x07, 0x10, 0x06,
  };
  int letter = -1;
  for (int i = 0; i < 26; ++i) {
    if (kLetterKeys[i] == key) {
      letter = i;
      break;
    }
  }
  if (letter >= 0 && !ModifiersDown() && !mac_is_japanese_input()) {
    if (down && g_self) {
      const bool upper = firefly_want_uppercase(g_self->caps_mode_for_remap(), ShiftDown(),
                                                g_self->preserved_caps_for_remap());
      const UniChar ch = static_cast<UniChar>((upper ? 'A' : 'a') + letter);
      CFStringRef s = CFStringCreateWithCharacters(nullptr, &ch, 1);
      if (s) {
        SendUnicodeChar(s, true);
        SendUnicodeChar(s, false);
        CFRelease(s);
      }
    }
    return nullptr;
  }

  return event;
}

}  // namespace

struct MacFireflyBackend::Impl {
  CFMachPortRef tap_port = nullptr;
  CFRunLoopSourceRef tap_source = nullptr;
  std::function<void()> on_toggle;
  std::atomic<bool> busy{false};
  std::string caps_mode = kFireflyCapsUppercase;
  std::string led_mode = kFireflyLedAuto;
  std::string busy_action = kFireflyBusyDnd;
  bool keep_display_on = false;
  bool preserved_caps_on = false;
  bool saved_caps_on = false;
  bool dnd_ok = false;
  bool keep_awake_ok = false;
  bool mic_mute_ok = false;
  bool speaker_mute_ok = false;
  bool voice_ok = false;
  bool custom_key_ok = false;
  bool dnd_backed_up = false;
  IOPMAssertionID keep_awake_id = 0;
  IOPMAssertionID keep_display_id = 0;
  bool mic_saved_valid = false;
  UInt32 mic_was_muted = 0;
  bool speaker_saved_valid = false;
  UInt32 speaker_was_muted = 0;
  int custom_vk = 0;

  std::mutex dnd_mu;
  std::condition_variable dnd_cv;
  std::atomic<bool> dnd_stop{true};
  bool dnd_target = false;
  bool dnd_pending = false;
  std::thread dnd_thread;

  void dnd_worker() {
    while (true) {
      bool target = false;
      {
        std::unique_lock lk(dnd_mu);
        dnd_cv.wait(lk, [&] { return dnd_pending || dnd_stop.load(); });
        if (dnd_stop.load() && !dnd_pending) break;
        target = dnd_target;
        dnd_pending = false;
      }
      WriteDndState(target);
    }
  }
};

MacFireflyBackend::MacFireflyBackend() = default;
MacFireflyBackend::~MacFireflyBackend() { stop(); }

FireflyCapabilities MacFireflyBackend::capabilities() const {
  FireflyCapabilities c{};
  c.can_intercept_caps = true;
  c.can_drive_led = true;
  c.can_set_dnd = impl_ ? impl_->dnd_ok : ProbeDnd();
  c.can_keep_awake = impl_ ? impl_->keep_awake_ok : ProbeKeepAwake();
  c.can_mute_mic = impl_ ? impl_->mic_mute_ok : ProbeMicMute();
  c.can_mute_speaker = impl_ ? impl_->speaker_mute_ok : ProbeSpeakerMute();
  c.can_trigger_voice_input = impl_ ? impl_->voice_ok : ProbeVoiceInput();
  c.can_trigger_custom_key = impl_ ? impl_->custom_key_ok : ProbeCustomKey();
  return c;
}

bool MacFireflyBackend::start(std::function<void()> on_toggle, const std::string& caps_mode) {
  if (impl_) return true;
  impl_ = new Impl();
  impl_->on_toggle = std::move(on_toggle);
  impl_->caps_mode = caps_mode.empty() ? kFireflyCapsUppercase : caps_mode;
  impl_->busy.store(false);
  impl_->saved_caps_on = CapsLockOn();
  impl_->preserved_caps_on = impl_->saved_caps_on;
  impl_->dnd_ok = ProbeDnd();
  impl_->keep_awake_ok = ProbeKeepAwake();
  impl_->mic_mute_ok = ProbeMicMute();
  impl_->speaker_mute_ok = ProbeSpeakerMute();
  impl_->voice_ok = ProbeVoiceInput();
  impl_->custom_key_ok = ProbeCustomKey();

  if (impl_->caps_mode == kFireflyCapsUppercase) {
    SetCapsLockState(true);
  } else if (impl_->caps_mode == kFireflyCapsLowercase) {
    SetCapsLockState(false);
  }

  RestoreDnd();
  BackupDnd();
  impl_->dnd_backed_up = true;
  impl_->dnd_stop.store(false);
  impl_->dnd_thread = std::thread([this] { impl_->dnd_worker(); });

  g_self = this;
  const CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) |
                           CGEventMaskBit(kCGEventTapDisabledByTimeout) |
                           CGEventMaskBit(kCGEventTapDisabledByUserInput);
  impl_->tap_port = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault, mask,
                                     EventTapCallback, nullptr);
  g_tap_port = impl_->tap_port;
  if (!impl_->tap_port) {
    stop();
    return false;
  }

  impl_->tap_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, impl_->tap_port, 0);
  CFRunLoopAddSource(CFRunLoopGetMain(), impl_->tap_source, kCFRunLoopCommonModes);
  CGEventTapEnable(impl_->tap_port, true);

  set_led(false);
  set_dnd(false);
  return true;
}

void MacFireflyBackend::stop() {
  if (!impl_) return;

  if (impl_->tap_source) {
    CFRunLoopRemoveSource(CFRunLoopGetMain(), impl_->tap_source, kCFRunLoopCommonModes);
    CFRelease(impl_->tap_source);
    impl_->tap_source = nullptr;
  }
  if (impl_->tap_port) {
    CGEventTapEnable(impl_->tap_port, false);
    CFRelease(impl_->tap_port);
    impl_->tap_port = nullptr;
  }
  g_self = nullptr;
  g_tap_port = nullptr;

  {
    std::lock_guard lk(impl_->dnd_mu);
    impl_->dnd_stop.store(true);
    impl_->dnd_cv.notify_one();
  }
  if (impl_->dnd_thread.joinable()) impl_->dnd_thread.join();

  if (impl_->dnd_backed_up) RestoreDnd();
  clear_sustained_busy_effects();
  SetCapsLockState(impl_->saved_caps_on);
  delete impl_;
  impl_ = nullptr;
}

void MacFireflyBackend::set_caps_mode(const std::string& mode) {
  if (!impl_) return;
  impl_->caps_mode = mode.empty() ? kFireflyCapsUppercase : mode;
  if (mode == kFireflyCapsUppercase) {
    SetCapsLockState(true);
  } else if (mode == kFireflyCapsLowercase) {
    SetCapsLockState(false);
  } else if (mode == kFireflyCapsPreserve) {
    impl_->preserved_caps_on = CapsLockOn();
  }
  set_led(impl_->busy.load());
}

void MacFireflyBackend::set_led_mode(const std::string& mode) {
  if (!impl_) return;
  impl_->led_mode = mode.empty() ? kFireflyLedAuto : mode;
  set_led(impl_->busy.load());
}

void MacFireflyBackend::set_busy_action(const std::string& action, bool keep_display_on, int custom_vk) {
  if (!impl_) return;
  const bool was_busy = impl_->busy.load();
  if (was_busy) clear_sustained_busy_effects();
  impl_->busy_action = normalize_busy_action(action);
  impl_->keep_display_on = keep_display_on;
  impl_->custom_vk = custom_vk;
  if (was_busy) {
    const auto fx = resolve_busy_effects(impl_->busy_action, true, false, impl_->keep_display_on, impl_->custom_vk);
    apply_busy_effects(fx);
  }
}

void MacFireflyBackend::set_led(bool on) {
  if (!impl_) return;
  if (impl_->led_mode == kFireflyLedNone) return;
  // macOS has no separate HID LED API in this backend; auto uses CapsLock state as Busy lamp.
  if (impl_->led_mode == kFireflyLedHid) return;
  SetCapsLockState(on);
}

void MacFireflyBackend::set_dnd(bool on) {
  if (!impl_ || !impl_->dnd_ok) return;
  std::lock_guard lk(impl_->dnd_mu);
  impl_->dnd_target = on;
  impl_->dnd_pending = true;
  impl_->dnd_cv.notify_one();
}

void MacFireflyBackend::set_keep_awake(bool on, bool keep_display_on) {
  if (!impl_ || !impl_->keep_awake_ok) return;
  if (on) {
    if (!impl_->keep_awake_id) {
      IOPMAssertionCreateWithName(kIOPMAssertionTypeNoIdleSleep, kIOPMAssertionLevelOn, CFSTR("IMEAura Firefly"),
                                  &impl_->keep_awake_id);
    }
    if (keep_display_on && !impl_->keep_display_id) {
      IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn, CFSTR("IMEAura Firefly"),
                                  &impl_->keep_display_id);
    }
  } else {
    if (impl_->keep_display_id) {
      IOPMAssertionRelease(impl_->keep_display_id);
      impl_->keep_display_id = 0;
    }
    if (impl_->keep_awake_id) {
      IOPMAssertionRelease(impl_->keep_awake_id);
      impl_->keep_awake_id = 0;
    }
  }
  if (on && !keep_display_on && impl_->keep_display_id) {
    IOPMAssertionRelease(impl_->keep_display_id);
    impl_->keep_display_id = 0;
  }
}

void MacFireflyBackend::set_mic_mute(bool on) {
  if (!impl_ || !impl_->mic_mute_ok) return;
  AudioDeviceID device = 0;
  if (!GetDefaultInputDevice(&device)) return;
  SetDeviceMute(device, kAudioDevicePropertyScopeInput, on, impl_->mic_saved_valid, impl_->mic_was_muted);
}

void MacFireflyBackend::set_speaker_mute(bool on) {
  if (!impl_ || !impl_->speaker_mute_ok) return;
  AudioDeviceID device = 0;
  if (!GetDefaultOutputDevice(&device)) return;
  SetDeviceMute(device, kAudioDevicePropertyScopeOutput, on, impl_->speaker_saved_valid, impl_->speaker_was_muted);
}

void MacFireflyBackend::trigger_voice_input() {
  if (!impl_ || !impl_->voice_ok) return;
  SendDictationShortcut();
}

void MacFireflyBackend::trigger_custom_key(int vk) {
  if (!impl_ || !impl_->custom_key_ok || vk <= 0) return;
  const CGKeyCode key = static_cast<CGKeyCode>(vk);
  CGEventRef down = CGEventCreateKeyboardEvent(nullptr, key, true);
  CGEventRef up = CGEventCreateKeyboardEvent(nullptr, key, false);
  if (down) {
    CGEventSetIntegerValueField(down, kCGEventSourceUserData, static_cast<int64_t>(kFireflyInjectTag));
    CGEventPost(kCGHIDEventTap, down);
    CFRelease(down);
  }
  if (up) {
    CGEventSetIntegerValueField(up, kCGEventSourceUserData, static_cast<int64_t>(kFireflyInjectTag));
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(up);
  }
}

void MacFireflyBackend::clear_sustained_busy_effects() {
  set_dnd(false);
  set_keep_awake(false, false);
  set_mic_mute(false);
  set_speaker_mute(false);
}

void MacFireflyBackend::apply_busy_effects(const FireflyBusyEffects& fx) {
  set_dnd(fx.want_dnd);
  set_keep_awake(fx.want_keep_awake, fx.keep_display_on);
  set_mic_mute(fx.want_mic_mute);
  set_speaker_mute(fx.want_speaker_mute);
  if (fx.trigger_voice_input) trigger_voice_input();
  if (fx.trigger_custom_key) trigger_custom_key(fx.custom_vk);
}

bool MacFireflyBackend::is_active() const { return impl_ && impl_->busy.load(); }

std::string MacFireflyBackend::caps_mode_for_remap() const {
  return impl_ ? impl_->caps_mode : kFireflyCapsUppercase;
}

bool MacFireflyBackend::preserved_caps_for_remap() const { return impl_ ? impl_->preserved_caps_on : false; }

void MacFireflyBackend::handle_toggle() {
  if (!impl_) return;
  const bool prev = impl_->busy.load();
  const bool next = !prev;
  impl_->busy.store(next);
  FireflyInput in{};
  in.enabled = true;
  in.toggle_requested = true;
  in.current_active = prev;
  in.busy_action = impl_->busy_action;
  in.keep_display_on = impl_->keep_display_on;
  in.custom_vk = impl_->custom_vk;
  const FireflyOutput out = evaluate_firefly(in);
  set_led(out.want_led_on);
  apply_busy_effects(out.effects);
  if (impl_->on_toggle) impl_->on_toggle();
}

}  // namespace imeaura
