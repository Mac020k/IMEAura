#include "platform/windows/win_tray.h"

#include "platform/windows/win_icon.h"

namespace imeaura {
namespace {

constexpr UINT kTrayIconMessage = WM_APP + 1;

}  // namespace

bool WinTray::create(HINSTANCE instance, HWND message_window, CommandHandler handler) {
  handler_ = std::move(handler);
  tray_hwnd_ = message_window;

  nid_ = {};
  nid_.cbSize = sizeof(nid_);
  nid_.hWnd = tray_hwnd_;
  nid_.uID = 1;
  nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid_.uCallbackMessage = kTrayIconMessage;
  nid_.hIcon = win_load_app_icon(GetSystemMetrics(SM_CXSMICON));
  wcscpy_s(nid_.szTip, L"IME Aura");
  return Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
}

void WinTray::destroy() {
  if (nid_.hWnd) Shell_NotifyIconW(NIM_DELETE, &nid_);
  nid_ = {};
}

LRESULT CALLBACK WinTray::TrayWndProc(HWND, UINT, WPARAM, LPARAM) {
  return DefWindowProcW(nullptr, 0, 0, 0);
}

bool WinSettingsWindow::create(HINSTANCE, Settings, SettingsCallback) { return false; }
void WinSettingsWindow::show() {}
void WinSettingsWindow::hide() {}
bool WinSettingsWindow::visible() const { return false; }
INT_PTR CALLBACK WinSettingsWindow::DialogProc(HWND, UINT, WPARAM, LPARAM) { return FALSE; }
void WinSettingsWindow::sync_from_controls() {}
void WinSettingsWindow::sync_to_controls() {}

}  // namespace imeaura
