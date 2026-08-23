#include "platform/linux/linux_platform.h"

#include "platform/firefly_host.h"
#include "platform/linux/linux_ime.h"
#include "platform/linux/linux_settings.h"
#include "platform/linux/linux_x11_edges.h"

#include <X11/Xlib.h>
#include <gtk/gtk.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace imeaura {
namespace {

Settings g_settings;
LinuxPlatformBackend* g_self = nullptr;
FireflyHost g_firefly;
LinuxX11Edges g_edges;
Display* g_dpy = nullptr;
bool g_running = false;
Rect g_last_monitor{};
int g_last_width = -1;
Rgba g_last_color{};
bool g_last_visible = false;

Rect primary_monitor_rect(Display* dpy) {
  if (!dpy) return {};
  const int screen = DefaultScreen(dpy);
  return Rect{0, 0, DisplayWidth(dpy, screen), DisplayHeight(dpy, screen)};
}

void refresh_policy() {
  if (!g_self) return;
  PolicyInput in{};
  in.ime_lang = linux_active_input_language();
  in.reduce_motion = g_self->prefers_reduced_motion();
  const auto policy = evaluate_policy(g_settings, in);
  g_self->apply_policy(g_settings, policy);
  g_firefly.poll();
}

}  // namespace

bool LinuxPlatformBackend::init() {
  g_self = this;
  load_settings(g_settings);

  g_dpy = XOpenDisplay(nullptr);
  if (!g_dpy) {
    std::cerr << "Linux: XOpenDisplay failed\n";
    return false;
  }
  if (!g_edges.init(g_dpy)) {
    XCloseDisplay(g_dpy);
    g_dpy = nullptr;
    return false;
  }

  g_firefly.set_on_toggle([] {
    refresh_policy();
    linux_settings::set_firefly_active(g_firefly.is_active());
  });
  if (g_settings.firefly_enabled) {
    if (!g_firefly.apply(g_settings, g_settings)) {
      save_settings(g_settings);
    }
  }
  linux_settings::create(g_settings, [](const Settings& s) {
    const bool was = g_settings.firefly_enabled;
    g_settings = s;
    save_settings(g_settings);
    if (was != g_settings.firefly_enabled) {
      g_firefly.apply(g_settings, g_settings);
    }
    linux_settings::set_firefly_active(g_firefly.is_active());
    refresh_policy();
  });
  std::cerr << "Linux: IME Aura host (X11 edges + Firefly)\n";
  return true;
}

void LinuxPlatformBackend::shutdown() {
  g_firefly.shutdown();
  linux_settings::destroy();
  g_edges.shutdown();
  if (g_dpy) {
    XCloseDisplay(g_dpy);
    g_dpy = nullptr;
  }
  g_self = nullptr;
}

int LinuxPlatformBackend::run() {
  g_running = true;
  refresh_policy();

  while (g_running) {
    while (g_main_context_pending(nullptr)) {
      g_main_context_iteration(nullptr, FALSE);
    }
    g_firefly.poll();
    refresh_policy();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}

bool LinuxPlatformBackend::prefers_reduced_motion() { return false; }
std::string LinuxPlatformBackend::active_input_language() { return linux_active_input_language(); }
bool LinuxPlatformBackend::is_text_input_focused() { return false; }
bool LinuxPlatformBackend::is_text_input_hovered() { return false; }

Rect LinuxPlatformBackend::get_active_monitor_rect() {
  if (g_dpy) return primary_monitor_rect(g_dpy);
  Display* dpy = XOpenDisplay(nullptr);
  if (!dpy) return {};
  const Rect r = primary_monitor_rect(dpy);
  XCloseDisplay(dpy);
  return r;
}

Rect LinuxPlatformBackend::get_cursor_monitor_rect() { return get_active_monitor_rect(); }

void LinuxPlatformBackend::apply_policy(const Settings& settings, const PolicyOutput& policy) {
  g_settings = settings;
  const Rect mon = get_active_monitor_rect();
  if (mon != g_last_monitor || settings.gradient_width != g_last_width) {
    g_edges.layout(mon, settings.gradient_width);
    g_last_monitor = mon;
    g_last_width = settings.gradient_width;
  }
  if (policy.target_color != g_last_color) {
    g_edges.set_color(policy.target_color);
    g_last_color = policy.target_color;
  }
  if (policy.visible != g_last_visible) {
    g_edges.set_visible(policy.visible);
    g_last_visible = policy.visible;
  }
}

void LinuxPlatformBackend::show_settings_window() { linux_settings::show(); }
void LinuxPlatformBackend::hide_settings_window() { linux_settings::hide(); }
bool LinuxPlatformBackend::settings_visible() const { return linux_settings::visible(); }

ProbeState LinuxPlatformBackend::probe_state(const Settings& settings) {
  PolicyInput in{};
  in.ime_lang = linux_active_input_language();
  const auto p = evaluate_policy(settings, in);
  ProbeState st{};
  st.ime_lang = in.ime_lang;
  st.ime_japanese = (in.ime_lang == "ja");
  st.visible = p.visible;
  if (g_dpy) {
    st.monitor_rect = primary_monitor_rect(g_dpy);
  } else {
    Display* dpy = XOpenDisplay(nullptr);
    if (dpy) {
      st.monitor_rect = primary_monitor_rect(dpy);
      XCloseDisplay(dpy);
    }
  }
  return st;
}

}  // namespace imeaura
