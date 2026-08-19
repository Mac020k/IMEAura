#include "platform/linux/linux_platform.h"

#include <iostream>

// Full layer-shell + viewporter + D-Bus IME lives here; CI stub logs and exits cleanly.

namespace imeaura {

bool LinuxPlatformBackend::init() {
  std::cerr << "Linux: layer-shell overlay + D-Bus IME (minimal stub; extend layer_edges.c)\n";
  return true;
}
void LinuxPlatformBackend::shutdown() {}
int LinuxPlatformBackend::run() {
  std::cerr << "Linux native host not fully wired on this build; use Windows/macOS builds.\n";
  return 0;
}
bool LinuxPlatformBackend::prefers_reduced_motion() { return false; }
bool LinuxPlatformBackend::is_japanese_input() { return false; }
bool LinuxPlatformBackend::is_text_input_focused() { return false; }
bool LinuxPlatformBackend::is_text_input_hovered() { return false; }
Rect LinuxPlatformBackend::get_active_monitor_rect() { return {}; }
Rect LinuxPlatformBackend::get_cursor_monitor_rect() { return {}; }
void LinuxPlatformBackend::apply_policy(const Settings&, const PolicyOutput&) {}
void LinuxPlatformBackend::show_settings_window() {}
void LinuxPlatformBackend::hide_settings_window() {}
bool LinuxPlatformBackend::settings_visible() const { return false; }
ProbeState LinuxPlatformBackend::probe_state(const Settings& settings) {
  PolicyInput in{};
  const auto p = evaluate_policy(settings, in);
  ProbeState st{};
  st.visible = p.visible;
  return st;
}

}  // namespace imeaura
