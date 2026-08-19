#include "platform/windows/win_ime.h"

#include <imm.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string>

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

}  // namespace imeaura
