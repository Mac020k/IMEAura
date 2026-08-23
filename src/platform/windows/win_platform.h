#pragma once

#include "platform/backend.h"
#include "core/color.h"
#include "core/policy.h"
#include "platform/windows/win_comp_edges.h"

#include <array>
#include <windows.h>

namespace imeaura {

class WinPlatformBackend : public PlatformBackend {
 public:
  WinPlatformBackend();
  ~WinPlatformBackend() override;

  bool init() override;
  bool init_probe() override;
  void shutdown() override;
  int run() override;

  bool prefers_reduced_motion() override;
  std::string active_input_language() override;
  bool is_text_input_focused() override;
  bool is_text_input_hovered() override;
  Rect get_active_monitor_rect() override;
  Rect get_cursor_monitor_rect() override;

  void apply_policy(const Settings& settings, const PolicyOutput& policy) override;
  void show_settings_window() override;
  void hide_settings_window() override;
  bool settings_visible() const override;
  bool running() const { return running_; }

  ProbeState probe_state(const Settings& settings) override;

  void request_refresh();
  Settings& mutable_settings() { return settings_; }
  const Settings& settings() const { return settings_; }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND, LONG idObject, LONG idChild, DWORD,
                                    DWORD);

  void update_state(bool force = false);
  void on_display_changed();
  void recreate_overlay();
  void sync_text_watchers();
  void refresh_reduce_motion_cache();
  void adjust_poll_interval();

  Settings settings_{};
  std::array<HWND, kEdgeHostCount> host_hwnds_{};
  HWINEVENTHOOK foreground_hook_ = nullptr;
  HWINEVENTHOOK focus_hook_ = nullptr;
  HWINEVENTHOOK ime_hook_ = nullptr;
  bool running_ = false;
  bool ui_started_ = false;
  bool session_locked_ = false;
  PolicyOutput last_policy_{};
  PolicyInput last_input_{};
  Rect last_monitor_{};
  Rect last_layout_{};
  int last_width_ = -1;
  DWORD last_activity_tick_ = 0;
  bool reduce_motion_cached_ = false;
};

}  // namespace imeaura
