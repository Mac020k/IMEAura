#pragma once

#include "platform/backend.h"

namespace imeaura {

class LinuxPlatformBackend : public PlatformBackend {
 public:
  bool init() override;
  void shutdown() override;
  int run() override;
  bool prefers_reduced_motion() override;
  std::string active_input_language() override;
  bool is_text_input_focused() override;
  bool is_text_input_hovered() override;
  Rect get_active_monitor_rect() override;
  Rect get_cursor_monitor_rect() override;
  void apply_policy(const Settings&, const PolicyOutput&) override;
  void show_settings_window() override;
  void hide_settings_window() override;
  bool settings_visible() const override;
  ProbeState probe_state(const Settings&) override;
};

}  // namespace imeaura
