#pragma once

#include "core/firefly.h"

#include <functional>
#include <memory>
#include <string>

namespace imeaura {

struct FireflyCaps {
  bool can_intercept_caps = false;
  bool can_drive_led = false;
  bool can_set_dnd = false;
  bool can_read_dnd = false;
};

class FireflyBackend {
 public:
  virtual ~FireflyBackend() = default;
  virtual FireflyCaps caps() const = 0;
  virtual bool start(std::function<void()> on_toggle, const std::string& caps_mode = kFireflyCapsUppercase) = 0;
  virtual void stop() = 0;
  virtual void set_led(bool on) = 0;
  virtual void set_dnd(bool on) = 0;
  virtual bool read_dnd(bool& out) = 0;
  virtual bool is_active() const = 0;
};

std::unique_ptr<FireflyBackend> create_firefly_backend();

}  // namespace imeaura
