#pragma once

#include "core/policy.h"
#include "core/settings.h"

#include <functional>
#include <memory>
#include <string>

namespace imeaura {

struct ProbeState {
  std::string ime_lang = "en";
  bool ime_japanese = false;
  bool text_focused = false;
  bool text_hovered = false;
  Rect monitor_rect{};
  bool visible = false;
};

class PlatformBackend {
 public:
  virtual ~PlatformBackend() = default;

  virtual bool init() = 0;
  virtual bool init_probe() { return true; }
  virtual void shutdown() = 0;
  virtual int run() = 0;

  virtual bool prefers_reduced_motion() = 0;
  virtual std::string active_input_language() = 0;
  virtual bool is_japanese_input() { return active_input_language() == "ja"; }
  virtual bool is_text_input_focused() = 0;
  virtual bool is_text_input_hovered() = 0;
  virtual Rect get_active_monitor_rect() = 0;
  virtual Rect get_cursor_monitor_rect() = 0;

  virtual void apply_policy(const Settings& settings, const PolicyOutput& policy) = 0;
  virtual void show_settings_window() = 0;
  virtual void hide_settings_window() = 0;
  virtual bool settings_visible() const = 0;

  virtual ProbeState probe_state(const Settings& settings) = 0;

  using SettingsChangedCallback = std::function<void(const Settings&)>;
  void set_settings_changed_callback(SettingsChangedCallback cb) { settings_changed_ = std::move(cb); }

 protected:
  void notify_settings_changed(const Settings& s) {
    if (settings_changed_) settings_changed_(s);
  }

 private:
  SettingsChangedCallback settings_changed_;
};

std::unique_ptr<PlatformBackend> create_platform_backend();

}  // namespace imeaura
