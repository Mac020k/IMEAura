#pragma once

#include <windows.h>

#include <string>

#include "platform/firefly_backend.h"

namespace imeaura {

class WinFireflyBackend : public FireflyBackend {
 public:
  WinFireflyBackend();
  ~WinFireflyBackend() override;

  FireflyCaps caps() const override;
  bool start(std::function<void()> on_toggle, const std::string& caps_mode = kFireflyCapsUppercase) override;
  void stop() override;
  void set_led(bool on) override;
  void set_dnd(bool on) override;
  bool read_dnd(bool& out) override;
  bool is_active() const override;

  void set_caps_mode(const std::string& mode);
  void handle_toggle();
  void set_target_hwnd(HWND hwnd);

 private:
  struct Impl;
  Impl* impl_ = nullptr;
  bool led_for_active() const;
  bool led_for_inactive() const;
  void apply_led_for_state(bool active);
};

}  // namespace imeaura
