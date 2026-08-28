#include "platform/windows/win_platform.h"

#include "core/firefly.h"
#include "core/i18n.h"
#include "platform/windows/win_comp_edges.h"
#include "platform/windows/win_firefly.h"
#include "platform/windows/win_ime.h"
#include "platform/windows/win_settings_api.h"
#include "platform/windows/win_tray.h"

#include <dbt.h>
#include <memory>
#include <shellscalingapi.h>
#include <shobjidl.h>
#include <string>
#include <wtsapi32.h>

namespace imeaura {
namespace {

constexpr wchar_t kHostClass[] = L"IMEAuraHost";
constexpr wchar_t kAppClass[] = L"IMEAuraApp";
constexpr UINT kPollTimerId = 1;
constexpr UINT kCoalesceTimerId = 5;
constexpr UINT kPollMs = 100;
constexpr UINT kCoalesceMs = 40;
constexpr UINT kRefreshMessage = WM_APP + 2;
constexpr UINT kFireflyToggleMsg = WM_APP + 100;
constexpr int kCmdOpenSettings = 1001;
constexpr int kCmdQuit = 1002;
constexpr int kCmdToggleAura = 1003;
constexpr int kCmdToggleFirefly = 1004;

WinPlatformBackend* g_self = nullptr;
WinCompEdges g_edges;
WinTray g_tray;
HINSTANCE g_instance = nullptr;
HWND g_app_hwnd = nullptr;
std::unique_ptr<WinFireflyBackend> g_firefly;

void NotifyFireflyUi() {
  win_settings::set_firefly_active(g_firefly && g_firefly->is_active());
  if (g_firefly) win_settings::set_firefly_capabilities(g_firefly->capabilities());
}

bool StartFirefly(const Settings& settings) {
  if (!g_firefly) g_firefly = std::make_unique<WinFireflyBackend>();
  g_firefly->set_target_hwnd(g_app_hwnd);
  if (!g_firefly->start(
          [] {
            if (g_app_hwnd) PostMessageW(g_app_hwnd, kRefreshMessage, 0, 0);
            NotifyFireflyUi();
          },
          settings.firefly_caps_mode)) {
    g_firefly.reset();
    win_settings::set_firefly_active(false);
    return false;
  }
  g_firefly->set_led_mode(settings.firefly_led_mode);
  g_firefly->set_busy_action(settings.firefly_busy_action, settings.firefly_keep_display_on);
  win_settings::set_firefly_capabilities(g_firefly->capabilities());
  NotifyFireflyUi();
  return true;
}

void StopFirefly() {
  if (!g_firefly) return;
  g_firefly->stop();
  g_firefly.reset();
  win_settings::set_firefly_active(false);
}

Rect MonitorRectFromHMONITOR(HMONITOR mon) {
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(mon, &mi)) return {};
  return Rect{mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
              mi.rcMonitor.bottom - mi.rcMonitor.top};
}

HMONITOR MonitorFromWindowCenter(HWND hwnd) {
  RECT rc{};
  if (!GetWindowRect(hwnd, &rc)) return MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
  const POINT pt{(rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2};
  return MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
}

Rect ActiveMonitorRect() {
  const HWND fg = GetForegroundWindow();
  if (!fg) {
    return MonitorRectFromHMONITOR(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY));
  }
  return MonitorRectFromHMONITOR(MonitorFromWindowCenter(fg));
}

Rect CursorMonitorRect() {
  POINT pt{};
  GetCursorPos(&pt);
  return MonitorRectFromHMONITOR(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST));
}

bool CreateEdgeHosts(HINSTANCE instance, std::array<HWND, kEdgeHostCount>& hosts) {
  for (HWND& host : hosts) {
    host = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
            WS_EX_TRANSPARENT,
        kHostClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (!host) return false;
    win_edge_host_set_input_passthrough(host);
  }
  return true;
}

void DestroyEdgeHosts(std::array<HWND, kEdgeHostCount>& hosts) {
  for (HWND& host : hosts) {
    if (host) {
      DestroyWindow(host);
      host = nullptr;
    }
  }
}

void ShowEdgeHosts(const std::array<HWND, kEdgeHostCount>& hosts) {
  for (HWND host : hosts) {
    if (host) ShowWindow(host, SW_SHOWNOACTIVATE);
  }
}

}  // namespace

WinPlatformBackend::WinPlatformBackend() = default;
WinPlatformBackend::~WinPlatformBackend() { shutdown(); }

bool WinPlatformBackend::init_probe() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  return true;
}

bool WinPlatformBackend::init() {
  g_self = this;
  load_settings(settings_);
  refresh_reduce_motion_cache();
  last_activity_tick_ = GetTickCount();

  if (!win_check_os_and_compositor()) {
    MessageBoxW(nullptr,
                L"IME Aura requires Windows 10 version 1803 or later with Windows.UI.Composition support.",
                L"IME Aura", MB_ICONERROR);
    return false;
  }

  SetCurrentProcessExplicitAppUserModelID(L"imestateviewer.app.1.0");
  g_instance = GetModuleHandleW(nullptr);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  WNDCLASSEXW app_wc{};
  app_wc.cbSize = sizeof(app_wc);
  app_wc.lpfnWndProc = WndProc;
  app_wc.hInstance = g_instance;
  app_wc.lpszClassName = kAppClass;
  RegisterClassExW(&app_wc);
  g_app_hwnd = CreateWindowExW(0, kAppClass, L"IMEAuraApp", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, g_instance,
                               this);
  if (!g_app_hwnd) return false;
  ShowWindow(g_app_hwnd, SW_HIDE);
  SetTimer(g_app_hwnd, kPollTimerId, kPollMs, nullptr);
  WTSRegisterSessionNotification(g_app_hwnd, NOTIFY_FOR_THIS_SESSION);

  win_text_input_set_changed_callback([] {
    if (g_app_hwnd) PostMessageW(g_app_hwnd, kRefreshMessage, 0, 0);
  });

  win_ime_worker_start([] {
    if (g_app_hwnd) PostMessageW(g_app_hwnd, kRefreshMessage, 0, 0);
  });

  WNDCLASSEXW host_wc{};
  host_wc.cbSize = sizeof(host_wc);
  host_wc.lpfnWndProc = HostWndProc;
  host_wc.hInstance = g_instance;
  host_wc.hbrBackground = nullptr;
  host_wc.lpszClassName = kHostClass;
  RegisterClassExW(&host_wc);

  foreground_hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  focus_hook_ = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr, WinEventProc, 0, 0,
                                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  ime_hook_ = SetWinEventHook(EVENT_OBJECT_IME_CHANGE, EVENT_OBJECT_IME_CHANGE, nullptr, WinEventProc, 0, 0,
                              WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  g_tray.create(g_instance, g_app_hwnd, [](int cmd) {
    if (!g_self) return;
    if (cmd == kCmdOpenSettings) g_self->show_settings_window();
    if (cmd == kCmdQuit) PostMessageW(g_app_hwnd, WM_CLOSE, 0, 0);
  });

  if (!CreateEdgeHosts(g_instance, host_hwnds_)) return false;
  if (!g_edges.init(host_hwnds_.data())) {
    DestroyEdgeHosts(host_hwnds_);
    return false;
  }

  if (!win_settings::create(g_instance, settings_, [this](const Settings& s) { apply_settings(s); })) {
    return false;
  }

  if (settings_.firefly_enabled) {
    if (!StartFirefly(settings_)) {
      settings_.firefly_enabled = false;
      save_settings(settings_);
      win_settings::sync(settings_);
    }
  }

  sync_text_watchers();
  update_state();
  ui_started_ = true;
  return true;
}

void WinPlatformBackend::shutdown() {
  if (!ui_started_) return;
  ui_started_ = false;
  if (g_app_hwnd) {
    KillTimer(g_app_hwnd, kPollTimerId);
    WTSUnRegisterSessionNotification(g_app_hwnd);
  }
  auto unhook = [](HWINEVENTHOOK& h) {
    if (h) {
      UnhookWinEvent(h);
      h = nullptr;
    }
  };
  unhook(foreground_hook_);
  unhook(focus_hook_);
  unhook(ime_hook_);
  StopFirefly();
  g_edges.shutdown();
  g_tray.destroy();
  win_ime_worker_stop();
  win_text_input_stop();
  win_settings::destroy();
  DestroyEdgeHosts(host_hwnds_);
  if (g_app_hwnd) DestroyWindow(g_app_hwnd);
  g_app_hwnd = nullptr;
  g_self = nullptr;
}

int WinPlatformBackend::run() {
  running_ = true;
  ShowEdgeHosts(host_hwnds_);
  win_settings::show();
  MSG msg{};
  while (running_ && GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

bool WinPlatformBackend::prefers_reduced_motion() {
  return reduce_motion_cached_;
}

void WinPlatformBackend::refresh_reduce_motion_cache() {
  BOOL enabled = TRUE;
  if (SystemParametersInfoW(0x1042, 0, &enabled, 0))
    reduce_motion_cached_ = (enabled == FALSE);
}

std::string WinPlatformBackend::active_input_language() { return win_ime_worker_language(); }
bool WinPlatformBackend::is_text_input_focused() { return win_text_input_focused(); }
bool WinPlatformBackend::is_text_input_hovered() {
  if (!settings_.show_on_hover) return false;
  return win_text_input_hovered();
}

Rect WinPlatformBackend::get_active_monitor_rect() { return ActiveMonitorRect(); }
Rect WinPlatformBackend::get_cursor_monitor_rect() { return CursorMonitorRect(); }

void WinPlatformBackend::apply_policy(const Settings& settings, const PolicyOutput& policy) {
  Rect mon = policy.follow == FollowTarget::Cursor ? get_cursor_monitor_rect() : get_active_monitor_rect();
  if (!(mon == last_layout_) || last_width_ != settings.gradient_width || !(mon == last_monitor_)) {
    g_edges.layout(mon, settings.gradient_width);
    last_layout_ = mon;
    last_monitor_ = mon;
    last_width_ = settings.gradient_width;
  }
  g_edges.set_color(policy.target_color, policy.blend_ms);
  g_edges.set_visible(policy.visible, policy.fade_ms);
  last_policy_ = policy;
}

void WinPlatformBackend::show_settings_window() { win_settings::show(); }
void WinPlatformBackend::hide_settings_window() { win_settings::hide(); }
bool WinPlatformBackend::settings_visible() const { return win_settings::visible(); }

ProbeState WinPlatformBackend::probe_state(const Settings& settings) {
  PolicyInput in{};
  in.ime_lang = active_input_language();
  in.text_focused = is_text_input_focused();
  in.text_hovered = is_text_input_hovered();
  in.reduce_motion = prefers_reduced_motion();
  const auto policy = evaluate_policy(settings, in);
  ProbeState st{};
  st.ime_lang = in.ime_lang;
  st.ime_japanese = (in.ime_lang == "ja");
  st.text_focused = in.text_focused;
  st.text_hovered = in.text_hovered;
  st.visible = policy.visible;
  st.monitor_rect = policy.follow == FollowTarget::Cursor ? get_cursor_monitor_rect() : get_active_monitor_rect();
  return st;
}

void WinPlatformBackend::request_refresh() { update_state(); }

void WinPlatformBackend::apply_settings(const Settings& s) {
  const bool was_ff = settings_.firefly_enabled;
  const std::string prev_caps = settings_.firefly_caps_mode;
  const std::string prev_led = settings_.firefly_led_mode;
  const std::string prev_busy = settings_.firefly_busy_action;
  const bool prev_keep_display = settings_.firefly_keep_display_on;
  settings_ = s;
  save_settings(settings_);
  notify_settings_changed(settings_);
  sync_text_watchers();
  if (s.firefly_enabled && !was_ff) {
    if (!StartFirefly(settings_)) {
      settings_.firefly_enabled = false;
      save_settings(settings_);
      win_settings::sync(settings_);
    }
  } else if (!s.firefly_enabled && was_ff) {
    StopFirefly();
  } else if (s.firefly_enabled && g_firefly) {
    if (s.firefly_caps_mode != prev_caps) g_firefly->set_caps_mode(s.firefly_caps_mode);
    if (s.firefly_led_mode != prev_led) g_firefly->set_led_mode(s.firefly_led_mode);
    if (s.firefly_busy_action != prev_busy || s.firefly_keep_display_on != prev_keep_display) {
      g_firefly->set_busy_action(s.firefly_busy_action, s.firefly_keep_display_on);
    }
    win_settings::set_firefly_capabilities(g_firefly->capabilities());
  }
  win_settings::sync(settings_);
  update_state(true);
}

void WinPlatformBackend::update_state(bool force) {
  PolicyInput in{};
  in.ime_lang = active_input_language();
  in.text_focused = is_text_input_focused();
  in.text_hovered = is_text_input_hovered();
  in.reduce_motion = reduce_motion_cached_;
  if (!force && in == last_input_) return;
  last_input_ = in;
  last_activity_tick_ = GetTickCount();
  apply_policy(settings_, evaluate_policy(settings_, in));
}

void WinPlatformBackend::on_display_changed() {
  last_monitor_ = {};
  last_layout_ = {};
  last_input_ = {};
  update_state();
}

void WinPlatformBackend::recreate_overlay() {
  g_edges.shutdown();
  if (g_edges.init(host_hwnds_.data())) {
    last_layout_ = {};
    last_monitor_ = {};
    last_width_ = -1;
    last_policy_ = {};
    update_state();
  }
}

void WinPlatformBackend::sync_text_watchers() {
  if (settings_.display_mode == kDisplayModeOnFocus) {
    win_text_input_start();
    win_text_input_set_hover_enabled(settings_.show_on_hover);
  } else {
    win_text_input_stop();
  }
}

void WinPlatformBackend::adjust_poll_interval() {
  if (session_locked_) return;
  const DWORD elapsed = GetTickCount() - last_activity_tick_;
  const UINT desired = (elapsed < 2000) ? kPollMs : 500;
  SetTimer(g_app_hwnd, kPollTimerId, desired, nullptr);
}

LRESULT CALLBACK WinPlatformBackend::HostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_ERASEBKGND:
      return 1;
    case WM_SETCURSOR:
      return TRUE;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_ACTIVATE:
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

LRESULT CALLBACK WinPlatformBackend::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  WinPlatformBackend* self = reinterpret_cast<WinPlatformBackend*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    self = reinterpret_cast<WinPlatformBackend*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

  switch (msg) {
    case WM_APP + 1:
      if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
        HMENU menu = CreatePopupMenu();
        const auto l = lang_from_key(self->settings_.language);
        AppendMenuW(menu, MF_STRING | (self->settings_.aura_enabled ? MF_CHECKED : MF_UNCHECKED), kCmdToggleAura,
                    tr(l, StringId::kAuraEnable));
        AppendMenuW(menu, MF_STRING | (self->settings_.firefly_enabled ? MF_CHECKED : MF_UNCHECKED),
                    kCmdToggleFirefly, tr(l, StringId::kFireflyEnable));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCmdOpenSettings, tr(l, StringId::kTrayOpen));
        AppendMenuW(menu, MF_STRING, kCmdQuit, tr(l, StringId::kTrayQuit));
        POINT pt{};
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd);
        TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
      } else if (lp == WM_LBUTTONUP) {
        self->show_settings_window();
      }
      return 0;
    case kRefreshMessage:
      self->update_state();
      return 0;
    case kFireflyToggleMsg:
      if (g_firefly) g_firefly->handle_toggle();
      return 0;
    case WM_TIMER:
      if (wp == kPollTimerId) {
        self->update_state();
        self->adjust_poll_interval();
      }
      if (wp == kCoalesceTimerId) {
        KillTimer(hwnd, kCoalesceTimerId);
        self->update_state();
      }
      return 0;
    case WM_COMMAND:
      if (LOWORD(wp) == kCmdOpenSettings) self->show_settings_window();
      if (LOWORD(wp) == kCmdQuit) PostMessageW(g_app_hwnd, WM_CLOSE, 0, 0);
      if (LOWORD(wp) == kCmdToggleAura) {
        Settings next = self->settings_;
        next.aura_enabled = !next.aura_enabled;
        self->apply_settings(next);
      }
      if (LOWORD(wp) == kCmdToggleFirefly) {
        Settings next = self->settings_;
        next.firefly_enabled = !next.firefly_enabled;
        self->apply_settings(next);
      }
      return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
    case WM_DEVICECHANGE:
      self->on_display_changed();
      return 0;
    case WM_SETTINGCHANGE:
      self->refresh_reduce_motion_cache();
      self->on_display_changed();
      return 0;
    case WM_POWERBROADCAST:
      if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND || wp == PBT_APMRESUMECRITICAL) {
        self->recreate_overlay();
      }
      return TRUE;
    case WM_WTSSESSION_CHANGE:
      if (wp == WTS_SESSION_LOCK) {
        self->session_locked_ = true;
        KillTimer(hwnd, kPollTimerId);
      }
      if (wp == WTS_SESSION_UNLOCK || wp == WTS_CONSOLE_CONNECT) {
        self->session_locked_ = false;
        SetTimer(hwnd, kPollTimerId, kPollMs, nullptr);
        self->recreate_overlay();
      }
      return 0;
    case WM_CLOSE: {
      if (!self->settings_.easy_quit) {
        const auto l = lang_from_key(self->settings_.language);
        if (MessageBoxW(hwnd, tr(l, StringId::kQuitConfirmBody), tr(l, StringId::kQuitConfirmTitle),
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
          return 0;
        }
      }
      self->running_ = false;
      PostQuitMessage(0);
      return 0;
    }
    case WM_DESTROY:
      if (hwnd == g_app_hwnd) PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void CALLBACK WinPlatformBackend::WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
  if (g_app_hwnd) {
    KillTimer(g_app_hwnd, kCoalesceTimerId);
    SetTimer(g_app_hwnd, kCoalesceTimerId, kCoalesceMs, nullptr);
  }
  win_ime_worker_poke();
}

}  // namespace imeaura
