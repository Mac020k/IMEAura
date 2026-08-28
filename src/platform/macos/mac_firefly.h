#pragma once

#include "platform/firefly_backend.h"

#include <functional>
#include <string>

namespace imeaura {

class MacFireflyBackend : public FireflyBackend {
 public:
  MacFireflyBackend();
  ~MacFireflyBackend() override;

  FireflyCapabilities capabilities() const override;
  bool start(std::function<void()> on_toggle, const std::string& caps_mode) override;
  void stop() override;
  void set_caps_mode(const std::string& mode) override;
  void set_led_mode(const std::string& mode) override;
  void set_busy_action(const std::string& action, bool keep_display_on) override;
  void set_led(bool on) override;
  void set_dnd(bool on) override;
  void set_keep_awake(bool on, bool keep_display_on) override;
  void set_mic_mute(bool on) override;
  void trigger_voice_input() override;
  bool is_active() const override;

  void handle_toggle();
  std::string caps_mode_for_remap() const;
  bool preserved_caps_for_remap() const;

 private:
  struct Impl;
  Impl* impl_ = nullptr;

  void apply_busy_effects(const FireflyBusyEffects& fx);
  void clear_sustained_busy_effects();
};

}  // namespace imeaura
