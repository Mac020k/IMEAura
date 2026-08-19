#pragma once

#include "core/settings.h"

#include <windows.h>
#include <shellapi.h>
#include <functional>

namespace imeaura {

class WinTray {
 public:
  using CommandHandler = std::function<void(int command_id)>;

  bool create(HINSTANCE instance, HWND message_window, CommandHandler handler);
  void destroy();

 private:
  static LRESULT CALLBACK TrayWndProc(HWND, UINT, WPARAM, LPARAM);

  HWND tray_hwnd_ = nullptr;
  NOTIFYICONDATAW nid_{};
  CommandHandler handler_;
};

class WinSettingsWindow {
 public:
  using SettingsCallback = std::function<void(const Settings&)>;

  bool create(HINSTANCE instance, Settings initial, SettingsCallback on_change);
  void show();
  void hide();
  bool visible() const;
  HWND hwnd() const { return hwnd_; }

 private:
  static INT_PTR CALLBACK DialogProc(HWND, UINT, WPARAM, LPARAM);
  void sync_from_controls();
  void sync_to_controls();

  HWND hwnd_ = nullptr;
  Settings settings_;
  SettingsCallback callback_;
};

}  // namespace imeaura
