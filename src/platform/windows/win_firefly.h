#pragma once

#include "platform/firefly_backend.h"

#include <windows.h>

#include <functional>
#include <string>

namespace imeaura {

class WinFireflyBackend : public FireflyBackend {
 public:
  WinFireflyBackend();
  ~WinFireflyBackend() override;

  FireflyCapabilities capabilities() const override;
  bool start(std::function<void()> on_toggle, const std::string& caps_mode) override;
  void stop() override;
  void set_caps_mode(const std::string& mode) override;
  void set_led_mode(const std::string& mode) override;
  void set_led(bool on) override;
  void set_dnd(bool on) override;
  bool is_active() const override;

  void handle_toggle();
  void set_target_hwnd(HWND hwnd);
  LRESULT filter_key(int code, WPARAM wp, LPARAM lp);

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace imeaura
