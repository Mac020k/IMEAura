#include "platform/windows/win_ime.h"

#include <imm.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cwctype>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace imeaura {
namespace {

HWND FocusHwnd(HWND fg) {
  GUITHREADINFO info{};
  info.cbSize = sizeof(info);
  const DWORD tid = GetWindowThreadProcessId(fg, nullptr);
  if (GetGUIThreadInfo(tid, &info) && info.hwndFocus) return info.hwndFocus;
  return fg;
}

int SendImeControl(HWND hwnd, int command) {
  if (!hwnd) return -1;
  DWORD_PTR result = 0;
  constexpr UINT kTimeout = 25;
  const LRESULT ok = SendMessageTimeoutW(
      hwnd, 0x0283 /*WM_IME_CONTROL*/, command, 0,
      SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, kTimeout, &result);
  if (!ok) return -1;
  return static_cast<int>(result);
}

bool ImeWindowJapanese(HWND ime_wnd) {
  if (!ime_wnd) return false;
  const int status = SendImeControl(ime_wnd, 0x0005 /*IMC_GETOPENSTATUS*/);
  if (!status) return false;
  const int mode = SendImeControl(ime_wnd, 0x0001 /*IMC_GETCONVERSIONMODE*/);
  if (mode < 0) return false;
  return (mode & 0x0001 /*IME_CMODE_NATIVE*/) != 0;
}

std::wstring ClassNameLower(HWND hwnd) {
  wchar_t buf[256]{};
  if (!GetClassNameW(hwnd, buf, 256)) return L"";
  std::wstring s(buf);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
  return s;
}

bool ClassLooksLikeEdit(const std::wstring& cls) {
  static const wchar_t* kMarkers[] = {
      L"edit",         L"richedit",     L"richedit20", L"richedit50",
      L"richedit60",   L"scintilla",    L"textfield",  L"passwordbox",
      L"inputsite",    L"windows.ui.input.inputsite", L"textarea", L"textbox",
      L"monaco-editor", L"monaco-mouse-cursor-text", L"cm-content", L"cm-editor",
      L"ace_editor",    L"ql-editor", L"text-input", L"textinput", L"inputarea",
  };
  for (const auto* m : kMarkers) {
    if (cls.find(m) != std::wstring::npos) return true;
  }
  return false;
}

HWND ForegroundFocusHwnd() {
  const HWND fg = GetForegroundWindow();
  if (!fg) return nullptr;
  return FocusHwnd(fg);
}

}  // namespace

bool win_is_japanese_input() {
  const HWND fg = GetForegroundWindow();
  if (!fg) return false;
  const HWND focus = FocusHwnd(fg);
  const HWND focus_ime = ImmGetDefaultIMEWnd(focus);
  const HWND default_ime = ImmGetDefaultIMEWnd(fg);
  if (ImeWindowJapanese(focus_ime)) return true;
  if (focus_ime != default_ime && ImeWindowJapanese(default_ime)) return true;
  return false;
}

bool win_native_edit_is_focused() {
  const HWND focus = ForegroundFocusHwnd();
  if (!focus) return false;
  return ClassLooksLikeEdit(ClassNameLower(focus));
}

bool win_native_edit_is_hovered() {
  POINT pt{};
  if (!GetCursorPos(&pt)) return false;
  HWND hwnd = WindowFromPoint(pt);
  if (!hwnd) return false;
  return ClassLooksLikeEdit(ClassNameLower(hwnd));
}

namespace {

std::atomic<bool> g_ime_japanese{false};
std::atomic<bool> g_ime_stop{true};
HANDLE g_ime_wake_event = nullptr;
std::thread g_ime_thread;
std::mutex g_ime_mutex;
std::mutex g_ime_cb_mutex;
std::function<void()> g_ime_change_cb;

void ImeWorkerLoop() {
  bool prev = false;
  while (!g_ime_stop.load(std::memory_order_relaxed)) {
    const bool jp = win_is_japanese_input();
    g_ime_japanese.store(jp, std::memory_order_relaxed);
    if (jp != prev) {
      prev = jp;
      std::function<void()> cb;
      {
        std::lock_guard lk(g_ime_cb_mutex);
        cb = g_ime_change_cb;
      }
      if (cb) cb();
    }
    WaitForSingleObject(g_ime_wake_event, 80);
  }
}

}  // namespace

void win_ime_worker_start(std::function<void()> on_change) {
  std::lock_guard lk(g_ime_mutex);
  {
    std::lock_guard lk2(g_ime_cb_mutex);
    g_ime_change_cb = std::move(on_change);
  }
  if (!g_ime_stop.exchange(false)) return;
  if (!g_ime_wake_event) g_ime_wake_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g_ime_thread = std::thread(ImeWorkerLoop);
}

void win_ime_worker_stop() {
  std::lock_guard lk(g_ime_mutex);
  if (g_ime_stop.exchange(true)) return;
  if (g_ime_wake_event) SetEvent(g_ime_wake_event);
  if (g_ime_thread.joinable()) g_ime_thread.join();
}

bool win_ime_worker_japanese() {
  return g_ime_japanese.load(std::memory_order_relaxed);
}

void win_ime_worker_poke() {
  if (g_ime_wake_event) SetEvent(g_ime_wake_event);
}

}  // namespace imeaura
