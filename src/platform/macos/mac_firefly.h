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
  void set_led(bool on) override;
  void set_dnd(bool on) override;
  bool is_active() const override;

  void handle_toggle();
  std::string caps_mode_for_remap() const;
  bool preserved_caps_for_remap() const;

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace imeaura
