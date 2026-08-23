#include "platform/linux/linux_platform.h"

#include "platform/firefly_host.h"
#include "platform/linux/linux_ime.h"

#include <X11/Xlib.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace imeaura {
namespace {

Settings g_settings;
LinuxPlatformBackend* g_self = nullptr;
FireflyHost g_firefly;
bool g_running = false;

Rect primary_monitor_rect(Display* dpy) {
  if (!dpy) return {};
  const int screen = DefaultScreen(dpy);
  return Rect{0, 0, DisplayWidth(dpy, screen), DisplayHeight(dpy, screen)};
}

void refresh_policy() {
  if (!g_self) return;
  PolicyInput in{};
  in.ime_japanese = linux_is_japanese_input();
  in.reduce_motion = g_self->prefers_reduced_motion();
  const auto policy = evaluate_policy(g_settings, in);
  g_self->apply_policy(g_settings, policy);
  g_firefly.poll();
}

}  // namespace

bool LinuxPlatformBackend::init() {
  g_self = this;
  load_settings(g_settings);
  g_firefly.set_on_toggle([] { refresh_policy(); });
  if (g_settings.firefly_enabled) {
    if (!g_firefly.apply(g_settings, g_settings)) {
      save_settings(g_settings);
    }
  }
  std::cerr << "Linux: IME Aura host (X11 Firefly + overlay stub)\n";
  return true;
}

void LinuxPlatformBackend::shutdown() {
  g_firefly.shutdown();
  g_self = nullptr;
}

int LinuxPlatformBackend::run() {
  g_running = true;
  refresh_policy();

  while (g_running) {
    g_firefly.poll();
    refresh_policy();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}

bool LinuxPlatformBackend::prefers_reduced_motion() { return false; }
bool LinuxPlatformBackend::is_japanese_input() { return linux_is_japanese_input(); }
bool LinuxPlatformBackend::is_text_input_focused() { return false; }
bool LinuxPlatformBackend::is_text_input_hovered() { return false; }

Rect LinuxPlatformBackend::get_active_monitor_rect() {
  Display* dpy = XOpenDisplay(nullptr);
  if (!dpy) return {};
  const Rect r = primary_monitor_rect(dpy);
  XCloseDisplay(dpy);
  return r;
}

Rect LinuxPlatformBackend::get_cursor_monitor_rect() { return get_active_monitor_rect(); }

void LinuxPlatformBackend::apply_policy(const Settings& settings, const PolicyOutput& policy) {
  g_settings = settings;
  (void)policy;
}

void LinuxPlatformBackend::show_settings_window() {}
void LinuxPlatformBackend::hide_settings_window() {}
bool LinuxPlatformBackend::settings_visible() const { return false; }

ProbeState LinuxPlatformBackend::probe_state(const Settings& settings) {
  PolicyInput in{};
  in.ime_japanese = linux_is_japanese_input();
  const auto p = evaluate_policy(settings, in);
  ProbeState st{};
  st.ime_japanese = in.ime_japanese;
  st.visible = p.visible;
  Display* dpy = XOpenDisplay(nullptr);
  if (dpy) {
    st.monitor_rect = primary_monitor_rect(dpy);
    XCloseDisplay(dpy);
  }
  return st;
}

}  // namespace imeaura
