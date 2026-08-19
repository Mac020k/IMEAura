#include "platform/windows/win_ime.h"

#include <ole2.h>
#include <UIAutomation.h>
#include <oleauto.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace imeaura {
namespace {

constexpr CONTROLTYPEID kEdit = 50004;
constexpr CONTROLTYPEID kComboBox = 50003;
constexpr CONTROLTYPEID kSpinner = 50016;
constexpr CONTROLTYPEID kDocument = 50030;
constexpr CONTROLTYPEID kCustom = 50025;

std::wstring LowerCopy(BSTR s) {
  if (!s) return L"";
  std::wstring out(s, SysStringLen(s));
  std::transform(out.begin(), out.end(), out.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
  return out;
}

bool ClassnameSuggestsText(const std::wstring& cn) {
  static const wchar_t* kMarkers[] = {
      L"textarea",
      L"textbox",
      L"text-input",
      L"textinput",
      L"inputarea",
      L"monaco-editor",
      L"monaco-mouse-cursor-text",
      L"cm-content",
      L"cm-editor",
      L"ace_editor",
      L"ql-editor",
      L"composer-bar editor",
  };
  for (const auto* m : kMarkers) {
    if (cn.find(m) != std::wstring::npos) return true;
  }
  return false;
}

bool AriaIsText(const std::wstring& aria) {
  return aria == L"textbox" || aria == L"searchbox" || aria == L"combobox" || aria == L"search";
}

bool HasPattern(IUIAutomationElement* el, PATTERNID id) {
  IUnknown* punk = nullptr;
  if (FAILED(el->GetCurrentPattern(id, &punk)) || !punk) return false;
  punk->Release();
  return true;
}

bool ElementIsTextInput(IUIAutomationElement* el) {
  if (!el) return false;

  CONTROLTYPEID type = 0;
  if (FAILED(el->get_CurrentControlType(&type))) return false;

  BOOL focusable = FALSE;
  el->get_CurrentIsKeyboardFocusable(&focusable);

  BSTR class_bstr = nullptr;
  std::wstring class_name;
  if (SUCCEEDED(el->get_CurrentClassName(&class_bstr)) && class_bstr) {
    class_name = LowerCopy(class_bstr);
    SysFreeString(class_bstr);
  }

  BSTR aria_bstr = nullptr;
  std::wstring aria;
  if (SUCCEEDED(el->get_CurrentAriaRole(&aria_bstr)) && aria_bstr) {
    aria = LowerCopy(aria_bstr);
    SysFreeString(aria_bstr);
  }
  while (!aria.empty() && iswspace(aria.front())) aria.erase(aria.begin());
  while (!aria.empty() && iswspace(aria.back())) aria.pop_back();

  const bool has_text = HasPattern(el, UIA_TextPatternId) || HasPattern(el, UIA_TextPattern2Id);
  const bool has_value = HasPattern(el, UIA_ValuePatternId);

  if (type == kEdit || type == kComboBox || type == kSpinner) return true;
  if (type == kDocument) {
    if (class_name.find(L"richedit") != std::wstring::npos) return true;
    if (ClassnameSuggestsText(class_name)) return true;
    if (AriaIsText(aria)) return true;
    if (aria == L"document" && class_name.empty()) return false;
    if (!class_name.empty() && has_text && has_value) return true;
    return false;
  }
  if (has_text) return true;
  if (AriaIsText(aria)) return true;
  if (ClassnameSuggestsText(class_name)) return true;
  return has_value && focusable && type == kCustom;
}

class UiaSession {
 public:
  UiaSession() = default;
  ~UiaSession() { reset(); }
  UiaSession(const UiaSession&) = delete;
  UiaSession& operator=(const UiaSession&) = delete;

  bool ensure() {
    if (uia_) return true;
    IUIAutomation* created = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                reinterpret_cast<void**>(&created))) ||
        !created) {
      return false;
    }
    uia_ = created;
    return true;
  }

  void reset() {
    if (uia_) {
      uia_->Release();
      uia_ = nullptr;
    }
  }

  bool focused_is_text() {
    if (!ensure()) return false;
    IUIAutomationElement* el = nullptr;
    if (FAILED(uia_->GetFocusedElement(&el)) || !el) return false;
    const bool ok = ElementIsTextInput(el);
    el->Release();
    return ok;
  }

  bool point_is_text(POINT pt) {
    if (!ensure()) return false;
    IUIAutomationElement* el = nullptr;
    if (FAILED(uia_->ElementFromPoint(pt, &el)) || !el) return false;
    const bool ok = ElementIsTextInput(el);
    el->Release();
    return ok;
  }

 private:
  IUIAutomation* uia_ = nullptr;
};

std::atomic<bool> g_focused{false};
std::atomic<bool> g_hovered{false};
std::atomic<bool> g_stop{true};
std::thread g_worker;
std::mutex g_callback_mutex;
std::function<void()> g_changed_callback;

void EmitChangedIfNeeded(bool prev_focused, bool focused, bool prev_hovered, bool hovered) {
  if (prev_focused == focused && prev_hovered == hovered) return;
  std::function<void()> cb;
  {
    std::lock_guard lock(g_callback_mutex);
    cb = g_changed_callback;
  }
  if (cb) cb();
}

void WorkerLoop() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  UiaSession uia;
  bool prev_focused = false;
  bool prev_hovered = false;
  while (!g_stop.load(std::memory_order_relaxed)) {
    bool focused = win_native_edit_is_focused();
    bool hovered = win_native_edit_is_hovered();
    if (!focused) {
      try {
        focused = uia.focused_is_text();
      } catch (...) {
        focused = false;
      }
    }
    if (!hovered) {
      POINT pt{};
      if (GetCursorPos(&pt)) {
        try {
          hovered = uia.point_is_text(pt);
        } catch (...) {
          hovered = false;
        }
      }
    }
    g_focused.store(focused, std::memory_order_relaxed);
    g_hovered.store(hovered, std::memory_order_relaxed);
    EmitChangedIfNeeded(prev_focused, focused, prev_hovered, hovered);
    prev_focused = focused;
    prev_hovered = hovered;
    Sleep((focused || hovered) ? 250 : 50);
  }
  uia.reset();
  CoUninitialize();
}

}  // namespace

void win_text_input_start() {
  if (!g_stop.exchange(false)) return;
  g_worker = std::thread(WorkerLoop);
}

void win_text_input_stop() {
  if (g_stop.exchange(true)) return;
  if (g_worker.joinable()) g_worker.join();
}

bool win_text_input_focused() {
  if (win_native_edit_is_focused()) return true;
  if (g_stop.load(std::memory_order_relaxed)) return false;
  return g_focused.load(std::memory_order_relaxed);
}

bool win_text_input_hovered() {
  if (win_native_edit_is_hovered()) return true;
  if (g_stop.load(std::memory_order_relaxed)) return false;
  return g_hovered.load(std::memory_order_relaxed);
}

void win_text_input_set_changed_callback(std::function<void()> cb) {
  std::lock_guard lock(g_callback_mutex);
  g_changed_callback = std::move(cb);
}

}  // namespace imeaura
