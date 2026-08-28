#include "platform/firefly_host.h"

namespace imeaura {

bool FireflyHost::start(const Settings& settings) {
  if (!backend_) {
    backend_ = create_firefly_backend();
    if (!backend_) return false;
  }
  if (!backend_->start(
          [this] {
            if (on_toggle_) on_toggle_();
          },
          settings.firefly_caps_mode)) {
    backend_.reset();
    return false;
  }
  backend_->set_led_mode(settings.firefly_led_mode);
  backend_->set_busy_action(settings.firefly_busy_action, settings.firefly_keep_display_on);
  was_enabled_ = true;
  return true;
}

void FireflyHost::stop() {
  if (!backend_) return;
  backend_->stop();
  backend_.reset();
  was_enabled_ = false;
}

bool FireflyHost::apply(const Settings& settings, Settings& mutable_settings) {
  const bool want = settings.firefly_enabled;
  if (want && !was_enabled_) {
    if (!start(settings)) {
      mutable_settings.firefly_enabled = false;
      return false;
    }
    return true;
  }
  if (!want && was_enabled_) {
    stop();
    return true;
  }
  if (want && backend_) {
    backend_->set_caps_mode(settings.firefly_caps_mode);
    backend_->set_led_mode(settings.firefly_led_mode);
    backend_->set_busy_action(settings.firefly_busy_action, settings.firefly_keep_display_on);
  }
  return true;
}

void FireflyHost::shutdown() { stop(); }

bool FireflyHost::is_active() const { return backend_ && backend_->is_active(); }

FireflyCapabilities FireflyHost::capabilities() const {
  if (backend_) return backend_->capabilities();
  auto probe = create_firefly_backend();
  if (!probe) return {};
  const FireflyCapabilities c = probe->capabilities();
  return c;
}

void FireflyHost::poll() {
  if (backend_) backend_->poll();
}

}  // namespace imeaura
