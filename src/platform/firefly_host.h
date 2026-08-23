#pragma once

#include "core/settings.h"
#include "platform/firefly_backend.h"

#include <functional>
#include <memory>

namespace imeaura {

// Shared Firefly lifecycle: start/stop from Settings and route toggle callbacks.
class FireflyHost {
 public:
  using ToggleCallback = std::function<void()>;

  void set_on_toggle(ToggleCallback cb) { on_toggle_ = std::move(cb); }

  // Applies firefly_enabled / caps / led settings. On hook install failure,
  // clears firefly_enabled in mutable_settings and returns false.
  bool apply(const Settings& settings, Settings& mutable_settings);

  void shutdown();
  bool is_active() const;
  bool is_running() const { return backend_ != nullptr; }
  void poll();

 private:
  bool start(const Settings& settings);
  void stop();

  std::unique_ptr<FireflyBackend> backend_;
  ToggleCallback on_toggle_;
  bool was_enabled_ = false;
};

}  // namespace imeaura
