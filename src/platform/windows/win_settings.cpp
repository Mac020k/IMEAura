#include "platform/windows/win_settings_api.h"

#include "core/firefly.h"
#include "core/i18n.h"
#include "core/input_languages.h"
#include "core/tokens.h"
#include "platform/windows/win_about.h"
#include "platform/windows/win_color_dialog.h"
#include "platform/windows/win_icon.h"

#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace imeaura {
namespace {

constexpr wchar_t kSettingsClass[] = L"IMEAuraSettings";
constexpr UINT kDwmWindowCornerPreference = 33;
constexpr UINT kDwmCornerRound = 2;
constexpr UINT kEntranceTimerId = 2;
constexpr UINT kRevealTimerId = 3;
constexpr UINT kFlashTimerId = 4;
constexpr UINT kEntranceFrameMs = 16;

constexpr UINT kSaveTimerId = 6;
constexpr UINT kSaveDebounceMs = 400;
constexpr UINT kSegmentTimerId = 7;
constexpr UINT kSegmentFrameMs = 16;
constexpr float kSegmentAnimDurationMs = 200.f;
constexpr UINT kTabAnimTimerId = 8;
constexpr UINT kTabAnimFrameMs = 16;
constexpr float kTabAnimDurationMs = 200.f;
constexpr UINT kSlotAnimTimerId = 9;
constexpr UINT kSlotAnimFrameMs = 16;
constexpr float kSlotAnimDurationMs = 240.f;
constexpr UINT kToggleAnimTimerId = 10;
constexpr UINT kToggleAnimFrameMs = 16;
constexpr float kToggleAnimDurationMs = 200.f;

enum class ToggleId : int { Aura = 0, Firefly = 1, EasyQuit = 2, Count = 3 };

enum class Tab { Aura, Firefly, General };
enum class SlotAnimKind { None, Add, Remove };

struct SlotRowRects {
  RECT lang{};
  RECT swatch{};
  RECT remove{};
};

struct SlotExitGhost {
  bool active = false;
  AuraColorSlot slot{};
  SlotRowRects from{};
  SlotRowRects to{};
};
enum class Page { Main, LangPicker, FireflyBusyPicker };

enum class Hit : int {
  None = 0,
  TabAura,
  TabFirefly,
  TabGeneral,
  ResetColors,
  WidthTrack,
  WidthValue,
  ResetWidth,
  ModeAlways,
  ModeFocus,
  AuraToggle,
  Hover,
  FontSmall,
  FontMedium,
  FontLarge,
  LangChange,
  LangBack,
  EasyQuitToggle,
  About,
  Quit,
  ScrollBar,
  FireflyToggle,
  FireflyCapsPreserve,
  FireflyCapsUppercase,
  FireflyCapsLowercase,
  FireflyBusyChange,
  FireflyBusyBack,
  FireflyKeepDisplay,
  FireflyCustomKeyCapture,
  AddSlot,
  SlotLang0 = 100,
  SlotSwatch0 = 200,
  SlotRemove0 = 300,
  LangPick0 = 400,
  BusyPick0 = 500,
};

enum class AlignH { Left, Center };
enum class AlignV { Top, Center };

struct UiMetrics {
  int title_h = 22;
  int body_h = 18;
  int sub_h = 16;
  int row_h = 32;
  int value_w = 64;
  int font_bar_h = 52;
  int font_r_s = 14;
  int font_r_m = 18;
  int font_r_l = 22;
  int swatch_w = 104;
  int swatch_h = 32;
  int lang_drop_w = 140;
  int back_btn_w = 72;
  int add_slot_w = 120;
};

D2D1_COLOR_F C(const Rgba& c, float a_scale = 1.f) {
  return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, (c.a / 255.f) * a_scale);
}

D2D1_ROUNDED_RECT RoundRect(const D2D1_RECT_F& r, float radius) {
  return D2D1::RoundedRect(r, radius, radius);
}

bool prefers_reduced_motion() {
  BOOL enabled = TRUE;
  if (SystemParametersInfoW(0x1042, 0, &enabled, 0)) return enabled == FALSE;
  return false;
}

std::wstring vk_label(int vk) {
  if (vk <= 0) return L"\u2014";
  UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
  LONG lparam = static_cast<LONG>(scan << 16);
  switch (vk) {
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_END:
    case VK_HOME:
    case VK_INSERT:
    case VK_DELETE:
    case VK_DIVIDE:
      lparam |= 0x01000000;
      break;
    default:
      break;
  }
  wchar_t name[64]{};
  if (GetKeyNameTextW(lparam, name, 64) > 0) return name;
  wchar_t buf[16];
  swprintf_s(buf, L"VK 0x%02X", vk);
  return buf;
}

float ease_out_cubic(float t) {
  const float inv = 1.f - t;
  return 1.f - inv * inv * inv;
}

float lerp_f(float a, float b, float t) { return a + (b - a) * t; }

D2D1_COLOR_F lerp_color(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
  return D2D1::ColorF(lerp_f(a.r, b.r, t), lerp_f(a.g, b.g, t), lerp_f(a.b, b.b, t), lerp_f(a.a, b.a, t));
}

bool rect_valid(const RECT& r) { return r.right > r.left && r.bottom > r.top; }

bool rect_empty(const RECT& r) { return r.right <= r.left && r.bottom <= r.top; }

RECT lerp_rect(const RECT& a, const RECT& b, float t) {
  if (rect_empty(a)) return b;
  if (rect_empty(b)) return a;
  return RECT{static_cast<LONG>(std::lround(lerp_f(static_cast<float>(a.left), static_cast<float>(b.left), t))),
              static_cast<LONG>(std::lround(lerp_f(static_cast<float>(a.top), static_cast<float>(b.top), t))),
              static_cast<LONG>(std::lround(lerp_f(static_cast<float>(a.right), static_cast<float>(b.right), t))),
              static_cast<LONG>(std::lround(lerp_f(static_cast<float>(a.bottom), static_cast<float>(b.bottom), t)))};
}

SlotRowRects lerp_slot_row(const SlotRowRects& a, const SlotRowRects& b, float t) {
  return {lerp_rect(a.lang, b.lang, t), lerp_rect(a.swatch, b.swatch, t), lerp_rect(a.remove, b.remove, t)};
}

SlotRowRects collapse_slot_row(const SlotRowRects& row) {
  auto collapse = [](RECT r) {
    if (r.right <= r.left) return r;
    const int cy = r.bottom > r.top ? (r.top + r.bottom) / 2 : r.top;
    return RECT{r.left, cy, r.right, cy};
  };
  return {collapse(row.lang), collapse(row.swatch), collapse(row.remove)};
}

RECT collapse_rect_h(const RECT& r) {
  if (r.right <= r.left) return r;
  const int cy = r.bottom > r.top ? (r.top + r.bottom) / 2 : r.top;
  return RECT{r.left, cy, r.right, cy};
}

class SettingsUi {
 public:
  bool create(HINSTANCE instance, Settings initial, std::function<void(const Settings&)> cb) {
    settings_ = normalize_settings(initial);
    prev_display_mode_ = settings_.display_mode;
    callback_ = std::move(cb);
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kSettingsClass;
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);

    constexpr DWORD kWndStyle = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;
    const int sys_dpi = static_cast<int>(GetDpiForSystem());
    const int scaled_w = MulDiv(kUiDefaultWindowW, sys_dpi, 96);
    const int scaled_h = MulDiv(kUiMinWindowH, sys_dpi, 96);
    RECT desired{0, 0, scaled_w, scaled_h};
    AdjustWindowRectExForDpi(&desired, kWndStyle, FALSE, 0, static_cast<UINT>(sys_dpi));
    const int w = desired.right - desired.left;
    const int h = desired.bottom - desired.top;
    hwnd_ = CreateWindowExW(0, kSettingsClass, L"IME Aura",
                            kWndStyle, CW_USEDEFAULT, CW_USEDEFAULT, w,
                            h, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    DWORD corner = kDwmCornerRound;
    DwmSetWindowAttribute(hwnd_, kDwmWindowCornerPreference, &corner, sizeof(corner));
    win_set_window_icons(hwnd_);
    sync_toggle_v_from_settings();
    return true;
  }

  void destroy() {
    release_target();
    dwrite_.Reset();
    d2d_.Reset();
    if (hwnd_) {
      DestroyWindow(hwnd_);
      hwnd_ = nullptr;
    }
  }

  void show() {
    if (!hwnd_) return;
    previous_fg_ = GetForegroundWindow();
    if (previous_fg_ == hwnd_) previous_fg_ = nullptr;
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    if (!entrance_played_) {
      entrance_played_ = true;
      if (prefers_reduced_motion()) {
        entrance_alpha_ = 255;
      } else {
        LONG ex = GetWindowLongW(hwnd_, GWL_EXSTYLE);
        SetWindowLongW(hwnd_, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        entrance_alpha_ = 0;
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
        SetTimer(hwnd_, kEntranceTimerId, kEntranceFrameMs, nullptr);
      }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void hide() {
    if (!hwnd_) return;
    const HWND restore = previous_fg_;
    previous_fg_ = nullptr;
    ShowWindow(hwnd_, SW_HIDE);
    if (restore && IsWindow(restore) && restore != hwnd_) {
      SetForegroundWindow(restore);
    }
  }

  bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
  HWND hwnd() const { return hwnd_; }

  void sync(const Settings& s) {
    // apply_settings() echoes back through sync() after every emit(); skip while animating.
    if (slot_anim_pending_ || slot_anim_active()) return;
    settings_ = normalize_settings(s);
    sync_toggle_v_from_settings();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_GETMINMAXINFO) {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
      int min_dpi = static_cast<int>(GetDpiForWindow(hwnd));
      if (min_dpi == 0) min_dpi = static_cast<int>(GetDpiForSystem());
      SettingsUi* self = reinterpret_cast<SettingsUi*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      const int min_client_w = self ? self->min_client_width_dip() : kUiMinWindowW;
      RECT min_rc{0, 0, MulDiv(min_client_w, min_dpi, 96), MulDiv(kUiMinWindowH, min_dpi, 96)};
      AdjustWindowRectExForDpi(&min_rc, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, FALSE, 0, static_cast<UINT>(min_dpi));
      mmi->ptMinTrackSize.x = min_rc.right - min_rc.left;
      mmi->ptMinTrackSize.y = min_rc.bottom - min_rc.top;
      return 0;
    }
    SettingsUi* self = reinterpret_cast<SettingsUi*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      self = reinterpret_cast<SettingsUi*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd_ = hwnd;
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->on_message(msg, wp, lp);
  }

  LRESULT on_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
      case WM_CREATE:
        ensure_factories();
        return 0;
      case WM_SIZE:
        release_target();
        clamp_scroll();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      case WM_DISPLAYCHANGE:
        release_target();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      case WM_DPICHANGED: {
        dpi_ = static_cast<int>(HIWORD(wp));
        release_target();
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      }
      case WM_ERASEBKGND:
        return 1;
      case WM_PAINT:
        paint();
        return 0;
      case WM_MOUSEWHEEL:
        scroll_by(-GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * dip(24));
        return 0;
      case WM_MOUSEMOVE:
        on_mouse(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp & MK_LBUTTON, false);
        return 0;
      case WM_LBUTTONDOWN:
        SetCapture(hwnd_);
        on_mouse(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), true, true);
        return 0;
      case WM_LBUTTONUP:
        if (GetCapture() == hwnd_) ReleaseCapture();
        dragging_width_ = false;
        dragging_scroll_ = false;
        return 0;
      case WM_CHAR:
        if (width_editing_) {
          if (wp >= L'0' && wp <= L'9' && width_edit_buf_.size() < 3) {
            width_edit_buf_ += static_cast<wchar_t>(wp);
            InvalidateRect(hwnd_, nullptr, FALSE);
          }
        }
        return 0;
      case WM_KEYDOWN:
        if (firefly_key_capture_) {
          if (wp == VK_ESCAPE) {
            firefly_key_capture_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
          }
          if (wp != VK_SHIFT && wp != VK_CONTROL && wp != VK_MENU && wp != VK_LWIN && wp != VK_RWIN &&
              wp != VK_CAPITAL) {
            settings_.firefly_custom_vk = static_cast<int>(wp);
            settings_ = normalize_settings(settings_);
            firefly_key_capture_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
          }
          return 0;
        }
        if (wp == VK_TAB && (GetKeyState(VK_CONTROL) & 0x8000)) {
          cycle_tab((GetKeyState(VK_SHIFT) & 0x8000) == 0);
          return 0;
        }
        if (width_editing_) {
          if (wp == VK_BACK && !width_edit_buf_.empty()) {
            width_edit_buf_.pop_back();
            InvalidateRect(hwnd_, nullptr, FALSE);
          } else if (wp == VK_RETURN) {
            commit_width_edit();
          } else if (wp == VK_ESCAPE) {
            width_editing_ = false;
            width_edit_buf_.clear();
            InvalidateRect(hwnd_, nullptr, FALSE);
          }
        }
        return 0;
      case WM_TIMER:
        if (wp == kEntranceTimerId) tick_entrance();
        if (wp == kRevealTimerId) tick_reveal();
        if (wp == kFlashTimerId) {
          reset_colors_flash_ = false;
          reset_width_flash_ = false;
          KillTimer(hwnd_, kFlashTimerId);
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        if (wp == kSaveTimerId) {
          save_pending_ = false;
          KillTimer(hwnd_, kSaveTimerId);
        }
        if (wp == kSegmentTimerId) tick_segment();
        if (wp == kTabAnimTimerId) tick_tab_anim();
        if (wp == kSlotAnimTimerId) tick_slot_anim();
        if (wp == kToggleAnimTimerId) tick_toggle_anim();
        return 0;
      case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
          POINT pt{};
          GetCursorPos(&pt);
          ScreenToClient(hwnd_, &pt);
          const Hit h = hit_test(MulDiv(pt.x, 96, dpi_), MulDiv(pt.y, 96, dpi_));
          SetCursor(LoadCursorW(nullptr, h == Hit::None ? IDC_ARROW : IDC_HAND));
          return TRUE;
        }
        break;
      case WM_CLOSE:
        if (settings_.easy_quit) {
          PostQuitMessage(0);
        } else {
          hide();
        }
        return 0;
      case WM_DESTROY:
        KillTimer(hwnd_, kSlotAnimTimerId);
        KillTimer(hwnd_, kToggleAnimTimerId);
        hwnd_ = nullptr;
        return 0;
      default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
  }

  int dip(int v) const { return v; }

  int content_width() const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int w = MulDiv(rc.right, 96, dpi_);
    // Equal left/right page margins; scrollbar overlays the right margin.
    return std::max(0, w - dip(kUiMargin) * 2);
  }

  void ensure_factories() {
    dpi_ = static_cast<int>(GetDpiForWindow(hwnd_));
    if (!d2d_) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf());
    if (!dwrite_) {
      DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                          reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    }
  }

  bool ensure_target() {
    ensure_factories();
    if (rt_) return true;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return false;
    const float dpi_f = static_cast<float>(dpi_);
    if (!SUCCEEDED(d2d_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                         D2D1::PixelFormat(), dpi_f, dpi_f),
            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(static_cast<UINT>(rc.right), static_cast<UINT>(rc.bottom)),
                                             D2D1_PRESENT_OPTIONS_NONE),
            rt_.GetAddressOf())))
      return false;
    return true;
  }

  void release_target() {
    tab_icon_round_stroke_.Reset();
    rt_.Reset();
  }

  ComPtr<IDWriteTextFormat> make_format(int pt, DWRITE_FONT_WEIGHT weight) {
    ComPtr<IDWriteTextFormat> fmt;
    const float px = static_cast<float>(MulDiv(pt, 96, 72));
    dwrite_->CreateTextFormat(L"Yu Gothic UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                              px, L"ja-jp", fmt.GetAddressOf());
    if (!fmt) {
      dwrite_->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                px, L"en-us", fmt.GetAddressOf());
    }
    return fmt;
  }

  DWRITE_TEXT_METRICS measure_text(IDWriteTextFormat* fmt, const wchar_t* text, float max_w = 16384.f,
                                   bool wrap = false) const {
    DWRITE_TEXT_METRICS metrics{};
    if (!dwrite_ || !fmt || !text) return metrics;
    ComPtr<IDWriteTextLayout> layout;
    const UINT32 n = static_cast<UINT32>(wcslen(text));
    if (FAILED(dwrite_->CreateTextLayout(text, n, fmt, std::max(1.f, max_w), 16384.f, layout.GetAddressOf())) ||
        !layout) {
      return metrics;
    }
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->GetMetrics(&metrics);
    return metrics;
  }

  int text_height(IDWriteTextFormat* fmt) const {
    // Include a little slack so glyph ascent/descent is not clipped by the draw box.
    return std::max(1, static_cast<int>(std::ceil(measure_text(fmt, L"Agあ漢字Å").height)) + 2);
  }

  int text_height_wrapped(IDWriteTextFormat* fmt, const wchar_t* text, int max_w) const {
    return std::max(1, static_cast<int>(std::ceil(measure_text(fmt, text, static_cast<float>(max_w), true).height)));
  }

  int text_width(IDWriteTextFormat* fmt, const wchar_t* text) const {
    return std::max(1, static_cast<int>(std::ceil(measure_text(fmt, text).widthIncludingTrailingWhitespace)));
  }

  ComPtr<IDWriteTextFormat> make_lang_format(int pt, DWRITE_FONT_WEIGHT weight) const {
    ComPtr<IDWriteTextFormat> fmt;
    if (!dwrite_) return fmt;
    const float px = static_cast<float>(MulDiv(pt, 96, 72));
    const wchar_t* family = lang_font_family(lang());
    if (FAILED(dwrite_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, px, L"en-us",
                                         fmt.GetAddressOf())) ||
        !fmt) {
      dwrite_->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                DWRITE_FONT_STRETCH_NORMAL, px, L"en-us", fmt.GetAddressOf());
    }
    return fmt;
  }

  // Tab bar: icon + gap + label per cell, centered in each third of the client width.
  int min_client_width_dip() {
    ensure_factories();
    const int body = ui_font_point_size(settings_.ui_font_size);
    auto body_fmt = make_lang_format(body, DWRITE_FONT_WEIGHT_NORMAL);
    if (!body_fmt) return kUiMinWindowW;
    const int gap = dip(6);
    const int body_h = text_height(body_fmt.Get());
    const StringId tab_ids[] = {StringId::kTabAura, StringId::kTabFirefly, StringId::kTabGeneral};
    int max_cell = 0;
    for (StringId id : tab_ids) {
      const int label_w = text_width(body_fmt.Get(), tr(lang(), id));
      max_cell = std::max(max_cell, body_h + gap + label_w);
    }
    // kUiTabPadX matches the tab indicator inset; extra margin keeps icon glow off neighbors.
    const int glow = static_cast<int>(std::ceil(body_h * 0.5f)) + dip(4);
    const int tab_min = max_cell + dip(kUiTabPadX) * 2 + glow;
    const int tab_bar_min = tab_min * 3;
    // Aura color row: language dropdown + swatch + remove button.
    const int body_min = dip(kUiMargin) * 2 + dip(kUiIndent) + dip(140) + dip(kUiRowGap) +
                         dip(kUiSwatchW) + dip(4) + dip(22);
    return std::max({tab_bar_min, body_min, kUiMinWindowW});
  }

  void enforce_min_client_width() {
    if (!hwnd_) return;
    const int min_dip = min_client_width_dip();
    RECT min_rc{0, 0, MulDiv(min_dip, dpi_, 96), 0};
    AdjustWindowRectExForDpi(&min_rc, GetWindowStyle(hwnd_), FALSE, GetWindowExStyle(hwnd_),
                             static_cast<UINT>(dpi_));
    const int min_frame_w = min_rc.right - min_rc.left;
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    if ((wr.right - wr.left) >= min_frame_w) return;
    SetWindowPos(hwnd_, nullptr, 0, 0, min_frame_w, wr.bottom - wr.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  UiMetrics make_metrics(IDWriteTextFormat* title_fmt, IDWriteTextFormat* body_fmt, IDWriteTextFormat* sub_fmt) {
    UiMetrics u;
    const int pad_y = dip(kUiButtonPadY);
    const int pad_x = dip(kUiButtonPadX);
    u.title_h = text_height(title_fmt);
    u.body_h = text_height(body_fmt);
    u.sub_h = text_height(sub_fmt);
    u.row_h = std::max(dip(kUiHitMin), u.body_h + pad_y * 2);
    u.value_w = text_width(body_fmt, L"100 px") + pad_x * 2 + dip(kUiRowGap);
    // Tapered font-size control: keep small; enlarge medium/large steps.
    u.font_r_s = std::max(dip(12), u.body_h / 2 + dip(2));
    u.font_r_m = u.font_r_s + dip(8);
    u.font_r_l = u.font_r_s + dip(16);
    const int font_pad = dip(6);
    u.font_bar_h = 2 * (u.font_r_l + font_pad);
    u.swatch_h = std::max(dip(kUiHitMin), u.body_h + pad_y * 2);
    u.swatch_w =
        std::max(1, static_cast<int>(std::lround(u.swatch_h * static_cast<double>(kUiSwatchW) / kUiSwatchH)));
    int name_w = 1;
    for (const auto& slot : settings_.aura_slots) {
      const wchar_t* name = input_language_display_name(slot.lang_id, lang() != Lang::En);
      if (name) name_w = std::max(name_w, text_width(body_fmt, name));
    }
    u.lang_drop_w = name_w + pad_x * 2 + dip(28);
    u.back_btn_w = dip(8) + dip(16) + dip(4) + text_width(body_fmt, tr(lang(), StringId::kLangBack)) + dip(8);
    u.add_slot_w =
        dip(8) + dip(16) + dip(4) + text_width(body_fmt, tr(lang(), StringId::kAddColorSlot)) + dip(8);
    return u;
  }

  void draw_text(ID2D1RenderTarget* rt, IDWriteTextFormat* fmt, const wchar_t* text, const D2D1_RECT_F& box,
                 const D2D1_COLOR_F& color, AlignH align_h = AlignH::Left, AlignV align_v = AlignV::Top,
                 bool wrap = true) {
    if (!rt || !fmt || !text || !dwrite_) return;
    const float w = std::max(0.f, box.right - box.left);
    const float h = std::max(0.f, box.bottom - box.top);
    if (w <= 0.f || h <= 0.f) return;
    ComPtr<IDWriteTextLayout> layout;
    const UINT32 n = static_cast<UINT32>(wcslen(text));
    // Always use the destination box height so paragraph alignment stays inside the clip.
    if (FAILED(dwrite_->CreateTextLayout(text, n, fmt, w, h, layout.GetAddressOf())) || !layout) return;
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (align_h == AlignH::Center) layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    else layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    if (align_v == AlignV::Center) layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    else layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (!brush) return;
    rt->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
    rt->DrawTextLayout(D2D1::Point2F(box.left, box.top), layout.Get(), brush.Get());
    rt->PopAxisAlignedClip();
  }

  void fill_round(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(color, brush.GetAddressOf());
    rt->FillRoundedRectangle(RoundRect(r, radius), brush.Get());
  }

  int client_dip_w() const {
    RECT rc{}; GetClientRect(hwnd_, &rc);
    return MulDiv(rc.right, 96, dpi_);
  }
  int client_dip_h() const {
    RECT rc{}; GetClientRect(hwnd_, &rc);
    return MulDiv(rc.bottom, 96, dpi_);
  }

  void clamp_scroll() {
    client_height_ = client_dip_h() - tab_bar_h();
    scroll_max_ = std::max(0, content_height_ - client_height_);
    scroll_y_ = std::clamp(scroll_y_, 0, scroll_max_);
  }

  void update_tab_bar_h(const UiMetrics& um) {
    tab_bar_h_cached_ =
        on_picker_page() ? 0 : std::max(dip(kUiTabBarHeight), um.row_h);
  }

  int tab_bar_h() const {
    if (on_picker_page()) return 0;
    return tab_bar_h_cached_ > 0 ? tab_bar_h_cached_ : dip(kUiTabBarHeight);
  }

  bool on_picker_page() const {
    return page_ == Page::LangPicker || page_ == Page::FireflyBusyPicker;
  }

  bool busy_action_selectable(std::string_view action) const {
    return busy_action_supported(action, firefly_caps_, settings_.firefly_custom_vk);
  }

  void scroll_by(int delta) {
    if (scroll_max_ <= 0) return;
    scroll_y_ = std::clamp(scroll_y_ + delta, 0, scroll_max_);
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void tick_entrance() {
    const int step = std::max(1, 255 * static_cast<int>(kEntranceFrameMs) / std::max(static_cast<int>(kEntranceMs), 1));
    entrance_alpha_ = std::min(255, entrance_alpha_ + step);
    SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(entrance_alpha_), LWA_ALPHA);
    if (entrance_alpha_ >= 255) KillTimer(hwnd_, kEntranceTimerId);
  }

  void start_hover_reveal() {
    hover_reveal_t_ = 0.f;
    hover_reveal_start_ = std::chrono::steady_clock::now();
    if (prefers_reduced_motion()) {
      hover_reveal_t_ = 1.f;
      return;
    }
    SetTimer(hwnd_, kRevealTimerId, kEntranceFrameMs, nullptr);
  }

  void tick_reveal() {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - hover_reveal_start_)
                             .count();
    const float t = std::clamp(static_cast<float>(elapsed) / static_cast<float>(kRevealMs), 0.f, 1.f);
    hover_reveal_t_ = ease_out_cubic(t);
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (t >= 1.f) KillTimer(hwnd_, kRevealTimerId);
  }

  RECT active_tab_rect() const {
    if (active_tab_ == Tab::Firefly) return tab_firefly_;
    if (active_tab_ == Tab::General) return tab_general_;
    return tab_aura_;
  }

  static int tab_index(Tab tab) {
    if (tab == Tab::Firefly) return 1;
    if (tab == Tab::General) return 2;
    return 0;
  }

  RECT tab_rect(Tab tab) const {
    if (tab == Tab::Firefly) return tab_firefly_;
    if (tab == Tab::General) return tab_general_;
    return tab_aura_;
  }

  void activate_tab(Tab tab) {
    if (on_picker_page()) page_ = Page::Main;
    if (active_tab_ != tab) {
      start_tab_anim(active_tab_rect(), tab_rect(tab));
      active_tab_ = tab;
    }
    scroll_y_ = tab_scroll_[tab_index(tab)];
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void cycle_tab(bool forward) {
    static constexpr Tab kTabs[] = {Tab::Aura, Tab::Firefly, Tab::General};
    int idx = tab_index(active_tab_);
    idx = forward ? (idx + 1) % 3 : (idx + 2) % 3;
    activate_tab(kTabs[idx]);
  }

  void start_tab_anim(const RECT& from, const RECT& to) {
    const int pad = dip(kUiTabPadX);
    if (prefers_reduced_motion()) {
      tab_anim_t_ = 1.f;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    tab_from_left_ = static_cast<float>(from.left + pad);
    tab_from_right_ = static_cast<float>(from.right - pad);
    tab_to_left_ = static_cast<float>(to.left + pad);
    tab_to_right_ = static_cast<float>(to.right - pad);
    tab_anim_t_ = 0.f;
    tab_anim_start_ = std::chrono::steady_clock::now();
    SetTimer(hwnd_, kTabAnimTimerId, kTabAnimFrameMs, nullptr);
  }

  void tick_tab_anim() {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - tab_anim_start_)
                             .count();
    const float t = std::clamp(static_cast<float>(elapsed) / kTabAnimDurationMs, 0.f, 1.f);
    tab_anim_t_ = ease_out_cubic(t);
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (t >= 1.f) KillTimer(hwnd_, kTabAnimTimerId);
  }

  RECT selected_font_rect() const {
    if (settings_.ui_font_size == kFontSizeLarge) return font_large_;
    if (settings_.ui_font_size == kFontSizeMedium) return font_medium_;
    return font_small_;
  }

  void start_segment_anim(const RECT& from, const RECT& to) {
    if (prefers_reduced_motion()) {
      seg_anim_t_ = 1.f;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    seg_from_left_ = static_cast<float>(from.left);
    seg_from_right_ = static_cast<float>(from.right);
    seg_to_left_ = static_cast<float>(to.left);
    seg_to_right_ = static_cast<float>(to.right);
    seg_anim_t_ = 0.f;
    seg_anim_start_ = std::chrono::steady_clock::now();
    SetTimer(hwnd_, kSegmentTimerId, kSegmentFrameMs, nullptr);
  }

  void tick_segment() {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - seg_anim_start_)
                             .count();
    const float t = std::clamp(static_cast<float>(elapsed) / kSegmentAnimDurationMs, 0.f, 1.f);
    seg_anim_t_ = ease_out_cubic(t);
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (t >= 1.f) KillTimer(hwnd_, kSegmentTimerId);
  }

  void sync_toggle_v_from_settings() {
    const bool on[static_cast<int>(ToggleId::Count)] = {settings_.aura_enabled, settings_.firefly_enabled,
                                                        settings_.easy_quit};
    for (int i = 0; i < static_cast<int>(ToggleId::Count); ++i) {
      if (!toggle_anim_active_[i]) toggle_v_[i] = on[i] ? 1.f : 0.f;
    }
  }

  void start_toggle_anim(ToggleId id, bool to_on) {
    const int idx = static_cast<int>(id);
    const float target = to_on ? 1.f : 0.f;
    if (prefers_reduced_motion()) {
      toggle_v_[idx] = target;
      toggle_anim_active_[idx] = false;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    toggle_from_[idx] = toggle_v_[idx];
    toggle_to_[idx] = target;
    toggle_anim_active_[idx] = true;
    toggle_anim_start_[idx] = std::chrono::steady_clock::now();
    SetTimer(hwnd_, kToggleAnimTimerId, kToggleAnimFrameMs, nullptr);
  }

  void tick_toggle_anim() {
    const auto now = std::chrono::steady_clock::now();
    bool any_active = false;
    for (int i = 0; i < static_cast<int>(ToggleId::Count); ++i) {
      if (!toggle_anim_active_[i]) continue;
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - toggle_anim_start_[i]).count();
      const float raw = std::clamp(static_cast<float>(elapsed) / kToggleAnimDurationMs, 0.f, 1.f);
      toggle_v_[i] = lerp_f(toggle_from_[i], toggle_to_[i], ease_out_cubic(raw));
      if (raw < 1.f) {
        any_active = true;
      } else {
        toggle_anim_active_[i] = false;
        toggle_v_[i] = toggle_to_[i];
      }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (!any_active) KillTimer(hwnd_, kToggleAnimTimerId);
  }

  float toggle_v(ToggleId id) const { return toggle_v_[static_cast<int>(id)]; }

  void reset_slot_anim() {
    if (hwnd_) KillTimer(hwnd_, kSlotAnimTimerId);
    slot_anim_t_ = 1.f;
    slot_anim_pending_ = false;
    slot_anim_kind_ = SlotAnimKind::None;
    slot_anim_is_add_ = false;
    slot_anim_index_ = 0;
    slot_anim_row_h_ = 0;
    slot_anim_from_.clear();
    slot_anim_to_.clear();
    add_slot_anim_from_ = {};
    add_slot_anim_to_ = {};
    slot_exit_ = {};
  }

  void capture_slot_anim_from() {
    slot_anim_from_.clear();
    for (size_t i = 0; i < settings_.aura_slots.size(); ++i) {
      slot_anim_from_.push_back({slot_lang_[i], slot_swatch_[i], slot_remove_[i]});
    }
    add_slot_anim_from_ = add_slot_;
  }

  void setup_slot_anim_after_layout(int color_row, int row_gap) {
    slot_anim_row_h_ = color_row + row_gap;
    slot_anim_to_.clear();
    for (size_t i = 0; i < settings_.aura_slots.size(); ++i) {
      slot_anim_to_.push_back({slot_lang_[i], slot_swatch_[i], slot_remove_[i]});
    }
    add_slot_anim_to_ = add_slot_;

    if (slot_anim_kind_ == SlotAnimKind::Add) {
      slot_anim_is_add_ = true;
      const size_t idx = slot_anim_index_;
      if (idx < slot_anim_to_.size()) {
        if (slot_anim_from_.size() < idx) slot_anim_from_.resize(idx);
        slot_anim_from_.resize(slot_anim_to_.size());
        slot_anim_from_[idx] = collapse_slot_row(slot_anim_to_[idx]);
      }
    } else if (slot_anim_kind_ == SlotAnimKind::Remove) {
      slot_anim_is_add_ = false;
      const size_t idx = slot_anim_index_;
      std::vector<SlotRowRects> remapped;
      remapped.reserve(slot_anim_to_.size());
      for (size_t j = 0; j < slot_anim_to_.size(); ++j) {
        const size_t old_j = j >= idx ? j + 1 : j;
        if (old_j < slot_anim_from_.size())
          remapped.push_back(slot_anim_from_[old_j]);
        else if (j < slot_anim_to_.size())
          remapped.push_back(slot_anim_to_[j]);
      }
      slot_anim_from_ = std::move(remapped);
      if (slot_exit_.active) slot_exit_.to = collapse_slot_row(slot_exit_.from);
      if (rect_empty(add_slot_anim_from_) && !rect_empty(add_slot_anim_to_)) {
        add_slot_anim_from_ = collapse_rect_h(add_slot_anim_to_);
      }
    } else {
      slot_anim_is_add_ = false;
    }

    slot_anim_kind_ = SlotAnimKind::None;
    slot_anim_pending_ = false;
    start_slot_anim();
  }

  void start_slot_anim() {
    if (prefers_reduced_motion()) {
      reset_slot_anim();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    slot_anim_t_ = 0.f;
    slot_anim_start_ = std::chrono::steady_clock::now();
    SetTimer(hwnd_, kSlotAnimTimerId, kSlotAnimFrameMs, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void tick_slot_anim() {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - slot_anim_start_)
                             .count();
    const float t = std::clamp(static_cast<float>(elapsed) / kSlotAnimDurationMs, 0.f, 1.f);
    slot_anim_t_ = ease_out_cubic(t);
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (t >= 1.f) {
      KillTimer(hwnd_, kSlotAnimTimerId);
      slot_exit_.active = false;
      slot_anim_from_.clear();
      slot_anim_to_.clear();
      add_slot_anim_from_ = {};
      add_slot_anim_to_ = {};
    }
  }

  bool slot_anim_active() const {
    return slot_anim_t_ < 1.f && (!slot_anim_from_.empty() || slot_exit_.active);
  }

  float slot_trailing_shift() const {
    if (!slot_anim_active() || slot_anim_row_h_ <= 0) return 0.f;
    const float remain = 1.f - slot_anim_t_;
    if (slot_anim_is_add_) return -static_cast<float>(slot_anim_row_h_) * remain;
    if (slot_exit_.active) return static_cast<float>(slot_anim_row_h_) * remain;
    return 0.f;
  }

  SlotRowRects visual_slot_row(size_t i, float* alpha) const {
    *alpha = 1.f;
    if (i >= settings_.aura_slots.size()) return {};
    if (!slot_anim_active() || i >= slot_anim_to_.size() ||
        slot_anim_from_.size() != slot_anim_to_.size()) {
      return {slot_lang_[i], slot_swatch_[i], slot_remove_[i]};
    }
    const float t = slot_anim_t_;
    if (slot_anim_is_add_ && i == slot_anim_index_) *alpha = t;
    return lerp_slot_row(slot_anim_from_[i], slot_anim_to_[i], t);
  }

  RECT visual_add_slot() const {
    if (!slot_anim_active()) return add_slot_;
    if (rect_empty(add_slot_anim_from_)) return add_slot_;
    if (rect_empty(add_slot_anim_to_)) return add_slot_anim_from_;
    return lerp_rect(add_slot_anim_from_, add_slot_anim_to_, slot_anim_t_);
  }

  void paint_aura_color_slots(float settings_alpha, IDWriteTextFormat* body_fmt, int color_row, int row_gap) {
    if (slot_anim_pending_) setup_slot_anim_after_layout(color_row, row_gap);

    for (size_t i = 0; i < settings_.aura_slots.size(); ++i) {
      const auto& slot = settings_.aura_slots[i];
      float alpha = 1.f;
      const SlotRowRects row = visual_slot_row(i, &alpha);
      const float row_alpha = settings_alpha * alpha;
      const wchar_t* name = input_language_display_name(slot.lang_id, lang() != Lang::En);
      paint_lang_dropdown(row.lang, name, body_fmt,
                          hover_ == static_cast<Hit>(static_cast<int>(Hit::SlotLang0) + static_cast<int>(i)),
                          row_alpha);
      paint_swatch(row.swatch, slot.color,
                   hover_ == static_cast<Hit>(static_cast<int>(Hit::SlotSwatch0) + static_cast<int>(i)), row_alpha);
      if (rect_valid(row.remove)) {
        paint_remove_button(row.remove,
                            hover_ == static_cast<Hit>(static_cast<int>(Hit::SlotRemove0) + static_cast<int>(i)),
                            row_alpha);
      }
    }

    if (slot_exit_.active && slot_anim_t_ < 1.f) {
      const float ghost_alpha = settings_alpha * (1.f - slot_anim_t_);
      const SlotRowRects row = lerp_slot_row(slot_exit_.from, slot_exit_.to, slot_anim_t_);
      const wchar_t* name = input_language_display_name(slot_exit_.slot.lang_id, lang() != Lang::En);
      paint_lang_dropdown(row.lang, name, body_fmt, false, ghost_alpha);
      paint_swatch(row.swatch, slot_exit_.slot.color, false, ghost_alpha);
      if (rect_valid(row.remove)) paint_remove_button(row.remove, false, ghost_alpha);
    }

    if (static_cast<int>(settings_.aura_slots.size()) < kMaxAuraSlots) {
      const RECT add_rc = visual_add_slot();
      if (rect_valid(add_rc)) {
        float add_alpha = 1.f;
        if (slot_anim_active() && !slot_anim_is_add_ && !rect_empty(add_slot_anim_from_)) {
          add_alpha = slot_anim_t_;
        }
        paint_add_slot_button(add_rc, body_fmt, hover_ == Hit::AddSlot, add_alpha);
      }
    } else if (slot_anim_is_add_ && slot_anim_active() && !rect_empty(add_slot_anim_from_)) {
      paint_add_slot_button(add_slot_anim_from_, body_fmt, false, 1.f - slot_anim_t_);
    }
  }

  void flash_reset(bool colors) {
    if (colors)
      reset_colors_flash_ = true;
    else
      reset_width_flash_ = true;
    SetTimer(hwnd_, kFlashTimerId, kStatusFlashMs, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void paint() {
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);
    if (!ensure_target()) {
      EndPaint(hwnd_, &ps);
      return;
    }
    const int cw = client_dip_w();
    const int ch = client_dip_h();
    const int body = ui_font_point_size(settings_.ui_font_size);
    auto title_fmt = make_format(body + 2, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto body_fmt = make_format(body, DWRITE_FONT_WEIGHT_NORMAL);
    auto sub_fmt = make_format(std::max(body - 2, 10), DWRITE_FONT_WEIGHT_NORMAL);
    const UiMetrics um = make_metrics(title_fmt.Get(), body_fmt.Get(), sub_fmt.Get());
    update_tab_bar_h(um);
    layout(um, body_fmt.Get(), sub_fmt.Get());
    clamp_scroll();
    layout(um, body_fmt.Get(), sub_fmt.Get());
    clamp_scroll();

    rt_->BeginDraw();
    const float w = static_cast<float>(cw);
    const float h = static_cast<float>(ch);
    rt_->Clear(C(kUiBg));

    ComPtr<ID2D1LinearGradientBrush> wash;
    ComPtr<ID2D1GradientStopCollection> stops;
    const D2D1_GRADIENT_STOP gs[] = {
        {0.f, C(kDefaultColorJp, 28.f / 255.f)},
        {0.42f, D2D1::ColorF(248 / 255.f, 249 / 255.f, 252 / 255.f, 0.f)},
        {1.f, C(kDefaultColorEn, 28.f / 255.f)},
    };
    if (SUCCEEDED(rt_->CreateGradientStopCollection(gs, 3, stops.GetAddressOf()))) {
      rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(w, h)),
                                     stops.Get(), wash.GetAddressOf());
      if (wash) rt_->FillRectangle(D2D1::RectF(0, 0, w, h), wash.Get());
    }

    // Tab bar (hidden on language picker)
    const int tab_h = tab_bar_h();
    if (tab_h > 0) {
      const float tab_y = static_cast<float>(tab_h);
      ComPtr<ID2D1SolidColorBrush> sep;
      rt_->CreateSolidColorBrush(C(kUiSeparator), sep.GetAddressOf());
      if (sep) rt_->DrawLine(D2D1::Point2F(0, tab_y), D2D1::Point2F(w, tab_y), sep.Get(), 1.f);

      const wchar_t* tab_labels[] = {
        tr(lang(), StringId::kTabAura),
        tr(lang(), StringId::kTabFirefly),
        tr(lang(), StringId::kTabGeneral)
      };
      const bool tab_enabled[] = {settings_.aura_enabled, settings_.firefly_enabled, false};
      const RECT* tab_rects[] = { &tab_aura_, &tab_firefly_, &tab_general_ };
      const Tab tabs[] = { Tab::Aura, Tab::Firefly, Tab::General };
      const int tab_w = static_cast<int>(w) / 3;
      for (int i = 0; i < 3; ++i) {
        const_cast<RECT*>(tab_rects[i])->left = tab_w * i;
        const_cast<RECT*>(tab_rects[i])->right = (i == 2) ? static_cast<int>(w) : tab_w * (i + 1);
        const_cast<RECT*>(tab_rects[i])->top = 0;
        const_cast<RECT*>(tab_rects[i])->bottom = tab_h;
        const bool active = (active_tab_ == tabs[i]);
        paint_tab_cell(*tab_rects[i], tab_labels[i], body_fmt.Get(), active, tab_enabled[i], tabs[i],
                       um.body_h);
      }
      {
        const float ind_y = tab_y - static_cast<float>(dip(kUiTabIndicatorH));
        ComPtr<ID2D1SolidColorBrush> ind;
        rt_->CreateSolidColorBrush(C(kUiTabActive), ind.GetAddressOf());
        if (ind) {
          const RECT ar = active_tab_rect();
          const int pad = dip(kUiTabPadX);
          float il, ir;
          if (tab_anim_t_ >= 1.f) {
            il = static_cast<float>(ar.left + pad);
            ir = static_cast<float>(ar.right - pad);
          } else {
            auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
            il = lerp(tab_from_left_, tab_to_left_, tab_anim_t_);
            ir = lerp(tab_from_right_, tab_to_right_, tab_anim_t_);
          }
          rt_->FillRectangle(D2D1::RectF(il, ind_y, ir, tab_y), ind.Get());
        }
      }
    } else {
      tab_aura_ = tab_firefly_ = tab_general_ = RECT{};
    }

    rt_->PushAxisAlignedClip(D2D1::RectF(0, static_cast<float>(tab_h), w, h), D2D1_ANTIALIAS_MODE_ALIASED);
    rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, static_cast<float>(tab_h) - static_cast<float>(scroll_y_)));

    if (page_ == Page::LangPicker) {
      paint_back_button(lang_back_, body_fmt.Get(), hover_ == Hit::LangBack);
      draw_text(rt_.Get(), title_fmt.Get(), tr(lang(), StringId::kLangSection), r2f(lang_picker_title_), C(kUiText),
                AlignH::Left, AlignV::Center);
      size_t n = 0;
      const auto* cat = ui_language_catalog(n);
      for (size_t i = 0; i < n && i < lang_pick_rows_.size(); ++i) {
        const bool on = settings_.language == cat[i].id;
        paint_radio(lang_pick_rows_[i], on, tr(lang(), string_id_for_ui_lang(cat[i].id)), body_fmt.Get());
      }
    } else if (page_ == Page::FireflyBusyPicker) {
      paint_back_button(ff_busy_back_, body_fmt.Get(), hover_ == Hit::FireflyBusyBack);
      draw_text(rt_.Get(), title_fmt.Get(), tr(lang(), StringId::kFireflyBusySection), r2f(ff_busy_picker_title_),
                C(kUiText), AlignH::Left, AlignV::Center);
      const auto& catalog = busy_action_catalog();
      for (size_t i = 0; i < catalog.size() && i < ff_busy_pick_rows_.size(); ++i) {
        const bool on = settings_.firefly_busy_action == catalog[i];
        const bool supported = busy_action_selectable(catalog[i]);
        const float alpha = supported ? 1.f : 0.45f;
        paint_radio(ff_busy_pick_rows_[i], on, tr(lang(), string_id_for_busy_action(std::string(catalog[i]))),
                    body_fmt.Get(), alpha);
      }
      if (settings_.firefly_busy_action == kFireflyBusyCustomKey && ff_custom_key_picker_.bottom > ff_custom_key_picker_.top) {
        const float alpha = firefly_key_capture_ ? 1.f : 0.85f;
        fill_round(rt_.Get(), r2f(ff_custom_key_picker_), dip(10),
                   hover_ == Hit::FireflyCustomKeyCapture ? C(kUiFillHover) : C(kUiFill));
        wchar_t key_line[128];
        if (firefly_key_capture_) {
          swprintf_s(key_line, L"%s", tr(lang(), StringId::kFireflyCustomKeyPrompt));
        } else if (settings_.firefly_custom_vk > 0) {
          swprintf_s(key_line, L"%s: %s", tr(lang(), StringId::kFireflyCustomKeyPrompt),
                     vk_label(settings_.firefly_custom_vk).c_str());
        } else {
          swprintf_s(key_line, L"%s", tr(lang(), StringId::kFireflyCustomKeyPrompt));
        }
        draw_text(rt_.Get(), body_fmt.Get(), key_line, r2f(ff_custom_key_picker_), C(kUiText, alpha), AlignH::Left,
                  AlignV::Center, false);
      }
    } else if (active_tab_ == Tab::Aura) {
    const float settings_alpha = settings_.aura_enabled ? 1.f : 0.45f;

    draw_text(rt_.Get(), title_fmt.Get(), tr(lang(), StringId::kAuraTitle), r2f(aura_title_), C(kUiText));
    draw_text(rt_.Get(), sub_fmt.Get(), tr(lang(), StringId::kAuraSub), r2f(aura_sub_), C(kUiTextSecondary));
    paint_toggle_row(aura_toggle_, toggle_v(ToggleId::Aura), tr(lang(), StringId::kAuraEnable), body_fmt.Get());

    draw_text(rt_.Get(), title_fmt.Get(), tr(lang(), StringId::kColorSection), r2f(sec_color_title_),
              C(kUiText, settings_alpha));
    draw_text(rt_.Get(), sub_fmt.Get(), tr(lang(), StringId::kColorSub), r2f(sec_color_sub_),
              C(kUiTextSecondary, settings_alpha));
    paint_aura_color_slots(settings_alpha, body_fmt.Get(), std::max(um.row_h, um.swatch_h), dip(kUiRowGap));

    const float trail_shift = slot_trailing_shift();
    const float content_ty = static_cast<float>(tab_h) - static_cast<float>(scroll_y_);
    if (trail_shift != 0.f) {
      rt_->SetTransform(D2D1::Matrix3x2F::Translation(0.f, content_ty + trail_shift));
    }
    draw_text(rt_.Get(), body_fmt.Get(),
              reset_colors_flash_ ? tr(lang(), StringId::kColorResetDone) : tr(lang(), StringId::kColorReset),
              r2f(reset_colors_),
              hover_ == Hit::ResetColors ? C(kDefaultColorEn, settings_alpha)
                                         : C(kUiTextSecondary, settings_alpha),
              AlignH::Left, AlignV::Center);
    paint_rule(rule1_);

    draw_text(rt_.Get(), title_fmt.Get(), tr(lang(), StringId::kWidthSection), r2f(sec_width_title_),
              C(kUiText, settings_alpha));
    draw_text(rt_.Get(), sub_fmt.Get(), tr(lang(), StringId::kWidthSub), r2f(sec_width_sub_),
              C(kUiTextSecondary, settings_alpha));
    paint_slider();
    wchar_t pxbuf[32];
    if (width_editing_) {
      swprintf_s(pxbuf, L"%s px", width_edit_buf_.empty() ? L"" : width_edit_buf_.c_str());
    } else {
      swprintf_s(pxbuf, L"%d px", settings_.gradient_width);
    }
    fill_round(rt_.Get(), r2f(width_value_), dip(8), width_editing_ ? C(kUiFillHover) : C(kUiFill));
    draw_text(rt_.Get(), body_fmt.Get(), pxbuf, r2f(width_value_), C(kUiText, settings_alpha), AlignH::Center,
              AlignV::Center);
    draw_text(rt_.Get(), body_fmt.Get(),
              reset_width_flash_ ? tr(lang(), StringId::kWidthResetDone) : tr(lang(), StringId::kWidthReset),
              r2f(reset_width_),
              hover_ == Hit::ResetWidth ? C(kDefaultColorEn, settings_alpha) : C(kUiTextSecondary, settings_alpha),
              AlignH::Left, AlignV::Center);
    paint_rule(rule2_);

    draw_text(rt_.Get(), title_fmt.Get(), tr(lang(), StringId::kDisplaySection), r2f(sec_disp_title_),
              C(kUiText, settings_alpha));
    paint_radio(mode_always_, settings_.display_mode == kDisplayModeAlways, tr(lang(), StringId::kDisplayAlways),
                body_fmt.Get(), settings_alpha);
    paint_radio(mode_focus_, settings_.display_mode == kDisplayModeOnFocus, tr(lang(), StringId::kDisplayFocus),
                body_fmt.Get(), settings_alpha);
    if (settings_.display_mode == kDisplayModeOnFocus && hover_box_.bottom > hover_box_.top) {
      paint_check(hover_box_, settings_.show_on_hover, tr(lang(), StringId::kDisplayHover), body_fmt.Get());
    }
    if (trail_shift != 0.f) {
      rt_->SetTransform(D2D1::Matrix3x2F::Translation(0.f, content_ty));
    }

    } else if (active_tab_ == Tab::Firefly) {
      const float caps_alpha = settings_.firefly_enabled ? 1.f : 0.45f;

      draw_text(rt_.Get(), title_fmt.Get(), tr(lang(), StringId::kFireflyTitle), r2f(ff_title_), C(kUiText));
      draw_text(rt_.Get(), sub_fmt.Get(), tr(lang(), StringId::kFireflySub), r2f(ff_sub_), C(kUiTextSecondary));
      paint_toggle_row(ff_toggle_, toggle_v(ToggleId::Firefly), tr(lang(), StringId::kFireflyEnable), body_fmt.Get());

      draw_text(rt_.Get(), sub_fmt.Get(), tr(lang(), StringId::kFireflyBusySection), r2f(ff_busy_title_),
                C(kUiTextSecondary, caps_alpha));
      fill_round(rt_.Get(), r2f(ff_busy_change_), dip(10),
                 hover_ == Hit::FireflyBusyChange ? C(kUiFillHover) : C(kUiFill));
      {
        wchar_t busy_btn[128];
        swprintf_s(busy_btn, L"%s: %s", tr(lang(), StringId::kFireflyBusyChange),
                   tr(lang(), string_id_for_busy_action(settings_.firefly_busy_action)));
        draw_text(rt_.Get(), body_fmt.Get(), busy_btn, r2f(ff_busy_change_), C(kUiText, caps_alpha), AlignH::Center,
                  AlignV::Center, false);
      }
      if (settings_.firefly_enabled && settings_.firefly_busy_action == kFireflyBusyKeepAwake &&
          ff_keep_display_.bottom > ff_keep_display_.top) {
        paint_check(ff_keep_display_, settings_.firefly_keep_display_on, tr(lang(), StringId::kFireflyKeepDisplayOn),
                    body_fmt.Get(), caps_alpha);
      }
      if (settings_.firefly_enabled && settings_.firefly_busy_action == kFireflyBusyCustomKey &&
          ff_custom_key_.bottom > ff_custom_key_.top) {
        wchar_t key_line[128];
        if (settings_.firefly_custom_vk > 0) {
          swprintf_s(key_line, L"%s: %s", tr(lang(), StringId::kFireflyCustomKeyPrompt),
                     vk_label(settings_.firefly_custom_vk).c_str());
        } else {
          swprintf_s(key_line, L"%s", tr(lang(), StringId::kFireflyCustomKeyPrompt));
        }
        draw_text(rt_.Get(), body_fmt.Get(), key_line, r2f(ff_custom_key_), C(kUiTextSecondary, caps_alpha), AlignH::Left,
                  AlignV::Center);
      }

      draw_text(rt_.Get(), sub_fmt.Get(), tr(lang(), StringId::kFireflyCapsSection), r2f(ff_caps_title_),
                C(kUiTextSecondary, caps_alpha));
      paint_radio(ff_caps_preserve_, settings_.firefly_caps_mode == kFireflyCapsPreserve,
                  tr(lang(), StringId::kFireflyCapsPreserve), body_fmt.Get(), caps_alpha);
      paint_radio(ff_caps_upper_, settings_.firefly_caps_mode == kFireflyCapsUppercase,
                  tr(lang(), StringId::kFireflyCapsUppercase), body_fmt.Get(), caps_alpha);
      paint_radio(ff_caps_lower_, settings_.firefly_caps_mode == kFireflyCapsLowercase,
                  tr(lang(), StringId::kFireflyCapsLowercase), body_fmt.Get(), caps_alpha);

      if (settings_.firefly_enabled) {
        const D2D1_COLOR_F lamp_on = D2D1::ColorF(0x16 / 255.f, 0xCC / 255.f, 0x7B / 255.f, 1.f);
        const D2D1_COLOR_F lamp_busy = D2D1::ColorF(0xE6 / 255.f, 0x69 / 255.f, 0x0C / 255.f, 1.f);
        paint_status_lamp(ff_status_, true, firefly_active_ ? lamp_on : lamp_busy,
                          firefly_active_ ? tr(lang(), StringId::kFireflyStateBusy)
                                          : tr(lang(), StringId::kFireflyStateAvailable),
                          body_fmt.Get());
        paint_status_lamp(ff_caps_ok_, firefly_caps_ok_, lamp_on, tr(lang(), StringId::kFireflyCapsOk),
                          sub_fmt.Get());
        paint_status_lamp(ff_led_ok_, firefly_led_ok_, lamp_on, tr(lang(), StringId::kFireflyLedOk), sub_fmt.Get());
        if (busy_action_requires_dnd(settings_.firefly_busy_action)) {
          paint_status_lamp(ff_dnd_ok_, firefly_dnd_ok_, lamp_on, tr(lang(), StringId::kFireflyDndOk), sub_fmt.Get());
        }
        if (busy_action_requires_keep_awake(settings_.firefly_busy_action)) {
          paint_status_lamp(ff_keep_awake_ok_, firefly_keep_awake_ok_, lamp_on, tr(lang(), StringId::kFireflyKeepAwakeOk),
                            sub_fmt.Get());
        }
        if (busy_action_requires_voice_input(settings_.firefly_busy_action)) {
          paint_status_lamp(ff_voice_ok_, firefly_voice_ok_, lamp_on, tr(lang(), StringId::kFireflyVoiceOk),
                            sub_fmt.Get());
        }
        if (busy_action_requires_mic_mute(settings_.firefly_busy_action)) {
          paint_status_lamp(ff_mic_ok_, firefly_mic_ok_, lamp_on, tr(lang(), StringId::kFireflyMicOk), sub_fmt.Get());
        }
        if (busy_action_requires_speaker_mute(settings_.firefly_busy_action)) {
          paint_status_lamp(ff_speaker_ok_, firefly_speaker_ok_, lamp_on, tr(lang(), StringId::kFireflySpeakerOk),
                            sub_fmt.Get());
        }
        if (busy_action_requires_custom_key(settings_.firefly_busy_action)) {
          paint_status_lamp(ff_custom_key_ok_, firefly_custom_key_ok_, lamp_on, tr(lang(), StringId::kFireflyCustomKeyPrompt),
                            sub_fmt.Get());
        }
        const bool action_ok = busy_action_selectable(settings_.firefly_busy_action);
        if (!firefly_caps_ok_ || !firefly_led_ok_ || !action_ok ||
            (busy_action_requires_dnd(settings_.firefly_busy_action) && !firefly_dnd_ok_) ||
            (busy_action_requires_keep_awake(settings_.firefly_busy_action) && !firefly_keep_awake_ok_) ||
            (busy_action_requires_voice_input(settings_.firefly_busy_action) && !firefly_voice_ok_) ||
            (busy_action_requires_mic_mute(settings_.firefly_busy_action) && !firefly_mic_ok_) ||
            (busy_action_requires_speaker_mute(settings_.firefly_busy_action) && !firefly_speaker_ok_) ||
            (busy_action_requires_custom_key(settings_.firefly_busy_action) &&
             (!firefly_custom_key_ok_ || settings_.firefly_custom_vk <= 0))) {
          RECT uns = ff_status_;
          uns.top = ff_mic_ok_.bottom > ff_status_.bottom ? ff_mic_ok_.bottom : ff_dnd_ok_.bottom;
          uns.top += dip(kUiRowGap);
          uns.bottom = uns.top + um.sub_h;
          draw_text(rt_.Get(), sub_fmt.Get(), tr(lang(), StringId::kFireflyUnsupported), r2f(uns),
                    C(kUiTextSecondary), AlignH::Left, AlignV::Center);
        }
      }

    } else {
    // General tab — font size, language, about, quit
    // Use existing font/about/quit layout positions (computed by layout())
    draw_text(rt_.Get(), title_fmt.Get(), tr(lang(), StringId::kFontSection), r2f(sec_font_title_), C(kUiText));
    draw_text(rt_.Get(), sub_fmt.Get(), tr(lang(), StringId::kFontSub), r2f(sec_font_sub_), C(kUiTextSecondary));
    paint_segments();
    paint_rule(rule3_);

    // Language section
    {
      const int m = dip(kUiMargin);
      const int inner = content_width();
      const int row = um.row_h;
      int gy = rule3_.top + dip(kUiSectionGap);
      RECT lang_title = box(m, gy, inner, um.title_h);
      gy += um.title_h + dip(kUiRowGap);
      lang_change_ = box(m, gy, inner, row);
      gy += row + dip(kUiSectionGap);
      lang_rule_ = box(m, gy, inner, 1);
      gy += dip(kUiSectionGap);

      draw_text(rt_.Get(), title_fmt.Get(), tr(lang(), StringId::kLangSection), r2f(lang_title), C(kUiText));
      fill_round(rt_.Get(), r2f(lang_change_), dip(10),
                 hover_ == Hit::LangChange ? C(kUiFillHover) : C(kUiFill));
      wchar_t lang_btn[128];
      swprintf_s(lang_btn, L"%s: %s", tr(lang(), StringId::kLangChange),
                 tr(lang(), string_id_for_ui_lang(settings_.language)));
      draw_text(rt_.Get(), body_fmt.Get(), lang_btn, r2f(lang_change_), C(kUiText), AlignH::Center, AlignV::Center,
                false);
      paint_rule(lang_rule_);
      easy_quit_ = box(m, gy, inner, row);
      paint_toggle_row(easy_quit_, toggle_v(ToggleId::EasyQuit), tr(lang(), StringId::kEasyQuit), body_fmt.Get());
    }

    paint_rule(rule4_);

    fill_round(rt_.Get(), r2f(about_), dip(12), hover_ == Hit::About ? C(kUiFillHover) : C(kUiFill));
    draw_text(rt_.Get(), body_fmt.Get(), tr(lang(), StringId::kAbout), r2f(about_), C(kUiText), AlignH::Center,
              AlignV::Center, false);
    fill_round(rt_.Get(), r2f(quit_), dip(12),
               hover_ == Hit::Quit ? D2D1::ColorF(200 / 255.f, 50 / 255.f, 50 / 255.f, 38 / 255.f)
                                   : C(kUiDangerFill));
    draw_text(rt_.Get(), body_fmt.Get(), tr(lang(), StringId::kQuit), r2f(quit_), C(kUiDanger), AlignH::Center,
              AlignV::Center, false);
    } // end of General tab

    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->PopAxisAlignedClip();

    if (scroll_max_ > 0) {
      const int sb_w = dip(kUiScrollBarWidth);
      const int sb_mr = dip(kUiScrollBarMarginRight);
      const int sb_my = dip(kUiScrollBarMarginY);
      scroll_bar_ = box(cw - sb_mr - sb_w, tab_h + sb_my, sb_w, ch - tab_h - sb_my * 2);
      ComPtr<ID2D1SolidColorBrush> track;
      rt_->CreateSolidColorBrush(C(kUiFill), track.GetAddressOf());
      rt_->FillRoundedRectangle(RoundRect(r2f(scroll_bar_), 4.f), track.Get());
      const int track_h = scroll_bar_.bottom - scroll_bar_.top;
      const int thumb_h = std::max(dip(24), track_h * client_height_ / std::max(content_height_, 1));
      const int thumb_y = scroll_bar_.top + (track_h - thumb_h) * scroll_y_ / std::max(scroll_max_, 1);
      ComPtr<ID2D1SolidColorBrush> thumb;
      rt_->CreateSolidColorBrush(C(kUiTextSecondary, 0.55f), thumb.GetAddressOf());
      rt_->FillRoundedRectangle(
          D2D1::RoundedRect(D2D1::RectF(static_cast<float>(scroll_bar_.left + 2), static_cast<float>(thumb_y),
                                        static_cast<float>(scroll_bar_.right - 2),
                                        static_cast<float>(thumb_y + thumb_h)),
                            4.f, 4.f),
          thumb.Get());
    }

    const HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) release_target();
    EndPaint(hwnd_, &ps);
  }

  // Icons match img/icon_back.svg, img/icon_trash.svg, img/icon_add.svg (24x24 viewBox).
  void draw_icon_back(float cx, float cy, float size, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (!brush) return;
    ComPtr<ID2D1PathGeometry> geo;
    ID2D1Factory* fac = nullptr;
    rt_->GetFactory(&fac);
    if (!fac || FAILED(fac->CreatePathGeometry(geo.GetAddressOf())) || !geo) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(sink.GetAddressOf())) || !sink) return;
    const float s = size / 24.f;
    auto P = [&](float x, float y) { return D2D1::Point2F(cx + (x - 12.f) * s, cy + (y - 12.f) * s); };
    sink->BeginFigure(P(15.f, 5.f), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(P(8.f, 12.f));
    sink->AddLine(P(15.f, 19.f));
    sink->AddLine(P(13.2f, 19.f));
    sink->AddLine(P(6.2f, 12.f));
    sink->AddLine(P(13.2f, 5.f));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    rt_->FillGeometry(geo.Get(), brush.Get());
  }

  void draw_icon_trash(float cx, float cy, float size, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (!brush) return;
    const float s = size / 24.f;
    auto P = [&](float x, float y) { return D2D1::Point2F(cx + (x - 12.f) * s, cy + (y - 12.f) * s); };
    const float stroke = std::max(1.25f, size * 0.075f);

    // Lid
    rt_->DrawLine(P(4.5f, 7.f), P(19.5f, 7.f), brush.Get(), stroke);
    // Handle
    rt_->DrawLine(P(9.f, 7.f), P(9.f, 5.f), brush.Get(), stroke);
    rt_->DrawLine(P(9.f, 5.f), P(15.f, 5.f), brush.Get(), stroke);
    rt_->DrawLine(P(15.f, 5.f), P(15.f, 7.f), brush.Get(), stroke);
    // Can body
    ComPtr<ID2D1PathGeometry> body;
    ID2D1Factory* fac = nullptr;
    rt_->GetFactory(&fac);
    if (fac && SUCCEEDED(fac->CreatePathGeometry(body.GetAddressOf())) && body) {
      ComPtr<ID2D1GeometrySink> sink;
      if (SUCCEEDED(body->Open(sink.GetAddressOf())) && sink) {
        sink->BeginFigure(P(7.f, 7.f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(P(8.2f, 19.5f));
        sink->AddBezier(D2D1::BezierSegment(P(8.35f, 20.6f), P(9.2f, 21.f), P(10.f, 21.f)));
        sink->AddLine(P(14.f, 21.f));
        sink->AddBezier(D2D1::BezierSegment(P(14.8f, 21.f), P(15.65f, 20.6f), P(15.8f, 19.5f)));
        sink->AddLine(P(17.f, 7.f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        rt_->DrawGeometry(body.Get(), brush.Get(), stroke);
      }
    }
    // Inner slots
    rt_->DrawLine(P(10.f, 10.5f), P(10.f, 17.5f), brush.Get(), stroke);
    rt_->DrawLine(P(12.f, 10.5f), P(12.f, 17.5f), brush.Get(), stroke);
    rt_->DrawLine(P(14.f, 10.5f), P(14.f, 17.5f), brush.Get(), stroke);
  }

  void draw_icon_add(float cx, float cy, float size, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (!brush) return;
    const float s = size / 24.f;
    auto P = [&](float x, float y) { return D2D1::Point2F(cx + (x - 12.f) * s, cy + (y - 12.f) * s); };
    const float stroke = std::max(1.5f, size * 0.085f);
    rt_->DrawLine(P(12.f, 5.f), P(12.f, 19.f), brush.Get(), stroke);
    rt_->DrawLine(P(5.f, 12.f), P(19.f, 12.f), brush.Get(), stroke);
  }

  // Icons match img/icon_tab_aura.svg, img/icon_tab_firefly.svg, img/icon_tab_general.svg (24x24 viewBox).
  void draw_icon_stroke(float stroke, const D2D1_COLOR_F& color,
                        const std::function<void(ID2D1GeometrySink*)>& build) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (!brush) return;
    ID2D1Factory* fac = nullptr;
    rt_->GetFactory(&fac);
    if (!fac) return;
    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(fac->CreatePathGeometry(geo.GetAddressOf())) || !geo) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(sink.GetAddressOf())) || !sink) return;
    build(sink.Get());
    sink->Close();
    rt_->DrawGeometry(geo.Get(), brush.Get(), stroke);
  }

  void draw_icon_tab_aura(float cx, float cy, float size, const D2D1_COLOR_F& color) {
    const float s = size / 24.f;
    auto P = [&](float x, float y) { return D2D1::Point2F(cx + (x - 12.f) * s, cy + (y - 12.f) * s); };
    const float stroke = std::max(1.f, size * (1.5f / 24.f));
    draw_icon_stroke(stroke, color, [&](ID2D1GeometrySink* sink) {
      sink->BeginFigure(P(3.3f, 20.f), D2D1_FIGURE_BEGIN_HOLLOW);
      sink->AddLine(P(3.3f, 9.f));
      sink->AddBezier(D2D1::BezierSegment(P(3.3f, 6.2f), P(5.5f, 4.f), P(8.3f, 4.f)));
      sink->AddLine(P(19.3f, 4.f));
      sink->EndFigure(D2D1_FIGURE_END_OPEN);
      sink->BeginFigure(P(7.f, 20.f), D2D1_FIGURE_BEGIN_HOLLOW);
      sink->AddLine(P(7.f, 11.f));
      sink->AddBezier(D2D1::BezierSegment(P(7.f, 8.7f), P(8.7f, 8.f), P(10.f, 8.f)));
      sink->AddLine(P(17.f, 8.f));
      sink->EndFigure(D2D1_FIGURE_END_OPEN);
    });
  }

  void draw_icon_tab_firefly(float cx, float cy, float size, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (!brush) return;
    // img/icon_tab_firefly.svg uses viewBox 0 0 24 20 (center 12, 10).
    const float s = size / 24.f;
    auto P = [&](float x, float y) { return D2D1::Point2F(cx + (x - 12.f) * s, cy + (y - 10.f) * s); };
    const float stroke = std::max(1.25f, size * (1.5f / 24.f));
    draw_icon_stroke(stroke, color, [&](ID2D1GeometrySink* sink) {
      sink->BeginFigure(P(10.f, 11.f), D2D1_FIGURE_BEGIN_HOLLOW);
      sink->AddBezier(D2D1::BezierSegment(P(4.5f, 13.f), P(4.5f, 19.f), P(10.f, 17.f)));
      sink->EndFigure(D2D1_FIGURE_END_OPEN);
      sink->BeginFigure(P(14.f, 11.f), D2D1_FIGURE_BEGIN_HOLLOW);
      sink->AddBezier(D2D1::BezierSegment(P(19.5f, 13.f), P(19.5f, 19.f), P(14.f, 17.f)));
      sink->EndFigure(D2D1_FIGURE_END_OPEN);
    });
    rt_->DrawLine(P(12.f, 8.5f), P(12.f, 17.5f), brush.Get(), stroke);
    rt_->DrawLine(P(12.f, 4.5f), P(12.f, 3.f), brush.Get(), stroke);
    const float head_r = std::max(1.f, 1.75f * s);
    rt_->DrawEllipse(D2D1::Ellipse(P(12.f, 6.5f), head_r, head_r), brush.Get(), stroke);
  }

  void draw_icon_tab_general(float cx, float cy, float size, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (!brush) return;
    const float s = size / 24.f;
    auto P = [&](float x, float y) { return D2D1::Point2F(cx + (x - 12.f) * s, cy + (y - 12.f) * s); };
    const float stroke = std::max(1.25f, size * (1.5f / 24.f));
    const float hub_r = std::max(1.f, 3.f * s);
    rt_->DrawEllipse(D2D1::Ellipse(P(12.f, 12.f), hub_r, hub_r), brush.Get(), stroke);
    rt_->DrawLine(P(12.f, 4.f), P(12.f, 8.5f), brush.Get(), stroke);
    rt_->DrawLine(P(12.f, 15.5f), P(12.f, 20.f), brush.Get(), stroke);
    rt_->DrawLine(P(4.5f, 12.f), P(7.5f, 12.f), brush.Get(), stroke);
    rt_->DrawLine(P(16.5f, 12.f), P(19.5f, 12.f), brush.Get(), stroke);
    rt_->DrawLine(P(6.8f, 6.8f), P(8.9f, 8.9f), brush.Get(), stroke);
    rt_->DrawLine(P(15.1f, 15.1f), P(17.2f, 17.2f), brush.Get(), stroke);
    rt_->DrawLine(P(6.8f, 17.2f), P(8.9f, 15.1f), brush.Get(), stroke);
    rt_->DrawLine(P(15.1f, 8.9f), P(17.2f, 6.8f), brush.Get(), stroke);
  }

  ID2D1StrokeStyle* tab_icon_round_stroke() {
    if (tab_icon_round_stroke_) return tab_icon_round_stroke_.Get();
    ID2D1Factory* fac = nullptr;
    rt_->GetFactory(&fac);
    if (!fac) return nullptr;
    const D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    fac->CreateStrokeStyle(props, nullptr, 0, tab_icon_round_stroke_.GetAddressOf());
    return tab_icon_round_stroke_.Get();
  }

  void paint_tab_stroke_glow(float base_stroke,
                             const std::function<void(float stroke, ID2D1SolidColorBrush*)>& draw) {
    const struct {
      float width_mul;
      float alpha;
    } layers[] = {{9.f, 0.035f}, {5.5f, 0.065f}, {3.f, 0.11f}};
    for (const auto& layer : layers) {
      ComPtr<ID2D1SolidColorBrush> glow;
      rt_->CreateSolidColorBrush(C(kDefaultAuraSlotColors[0], layer.alpha), glow.GetAddressOf());
      if (!glow) continue;
      draw(base_stroke * layer.width_mul, glow.Get());
    }
  }

  void paint_tab_icon_glow(Tab tab, float cx, float cy, float size) {
    ID2D1StrokeStyle* stroke_style = tab_icon_round_stroke();
    ID2D1Factory* fac = nullptr;
    rt_->GetFactory(&fac);
    if (!fac) return;

    switch (tab) {
      case Tab::Aura: {
        const float s = size / 24.f;
        auto P = [&](float x, float y) { return D2D1::Point2F(cx + (x - 12.f) * s, cy + (y - 12.f) * s); };
        const float base_stroke = std::max(1.f, size * (1.5f / 24.f));
        ComPtr<ID2D1PathGeometry> geo;
        if (FAILED(fac->CreatePathGeometry(geo.GetAddressOf())) || !geo) break;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geo->Open(sink.GetAddressOf())) || !sink) break;
        sink->BeginFigure(P(3.3f, 20.f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(P(3.3f, 9.f));
        sink->AddBezier(D2D1::BezierSegment(P(3.3f, 6.2f), P(5.5f, 4.f), P(8.3f, 4.f)));
        sink->AddLine(P(19.3f, 4.f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->BeginFigure(P(7.f, 20.f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(P(7.f, 11.f));
        sink->AddBezier(D2D1::BezierSegment(P(7.f, 8.7f), P(8.7f, 8.f), P(10.f, 8.f)));
        sink->AddLine(P(17.f, 8.f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        paint_tab_stroke_glow(base_stroke, [&](float stroke, ID2D1SolidColorBrush* brush) {
          rt_->DrawGeometry(geo.Get(), brush, stroke, stroke_style);
        });
        break;
      }
      case Tab::Firefly: {
        const float s = size / 24.f;
        auto P = [&](float x, float y) { return D2D1::Point2F(cx + (x - 12.f) * s, cy + (y - 10.f) * s); };
        const float base_stroke = std::max(1.25f, size * (1.5f / 24.f));
        ComPtr<ID2D1PathGeometry> wings;
        if (SUCCEEDED(fac->CreatePathGeometry(wings.GetAddressOf())) && wings) {
          ComPtr<ID2D1GeometrySink> sink;
          if (SUCCEEDED(wings->Open(sink.GetAddressOf())) && sink) {
            sink->BeginFigure(P(10.f, 11.f), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddBezier(D2D1::BezierSegment(P(4.5f, 13.f), P(4.5f, 19.f), P(10.f, 17.f)));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->BeginFigure(P(14.f, 11.f), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddBezier(D2D1::BezierSegment(P(19.5f, 13.f), P(19.5f, 19.f), P(14.f, 17.f)));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            paint_tab_stroke_glow(base_stroke, [&](float stroke, ID2D1SolidColorBrush* brush) {
              rt_->DrawGeometry(wings.Get(), brush, stroke, stroke_style);
            });
          }
        }
        const D2D1_POINT_2F body_top = P(12.f, 8.5f);
        const D2D1_POINT_2F body_bottom = P(12.f, 17.5f);
        const D2D1_POINT_2F antenna_top = P(12.f, 4.5f);
        const D2D1_POINT_2F antenna_bottom = P(12.f, 3.f);
        paint_tab_stroke_glow(base_stroke, [&](float stroke, ID2D1SolidColorBrush* brush) {
          rt_->DrawLine(body_top, body_bottom, brush, stroke, stroke_style);
          rt_->DrawLine(antenna_top, antenna_bottom, brush, stroke, stroke_style);
        });
        const float head_r = std::max(1.f, 1.75f * s);
        const D2D1_ELLIPSE head = D2D1::Ellipse(P(12.f, 6.5f), head_r, head_r);
        paint_tab_stroke_glow(base_stroke, [&](float stroke, ID2D1SolidColorBrush* brush) {
          rt_->DrawEllipse(head, brush, stroke, stroke_style);
        });
        break;
      }
      case Tab::General: {
        const float s = size / 24.f;
        auto P = [&](float x, float y) { return D2D1::Point2F(cx + (x - 12.f) * s, cy + (y - 12.f) * s); };
        const float base_stroke = std::max(1.25f, size * (1.5f / 24.f));
        const float hub_r = std::max(1.f, 3.f * s);
        const D2D1_ELLIPSE hub = D2D1::Ellipse(P(12.f, 12.f), hub_r, hub_r);
        paint_tab_stroke_glow(base_stroke, [&](float stroke, ID2D1SolidColorBrush* brush) {
          rt_->DrawEllipse(hub, brush, stroke, stroke_style);
        });
        const D2D1_POINT_2F spokes[] = {
            P(12.f, 4.f),   P(12.f, 8.5f),   P(12.f, 15.5f), P(12.f, 20.f),  P(4.5f, 12.f),
            P(7.5f, 12.f),  P(16.5f, 12.f),  P(19.5f, 12.f), P(6.8f, 6.8f),  P(8.9f, 8.9f),
            P(15.1f, 15.1f), P(17.2f, 17.2f), P(6.8f, 17.2f), P(8.9f, 15.1f), P(15.1f, 8.9f),
            P(17.2f, 6.8f),
        };
        paint_tab_stroke_glow(base_stroke, [&](float stroke, ID2D1SolidColorBrush* brush) {
          for (size_t i = 0; i + 1 < std::size(spokes); i += 2) {
            rt_->DrawLine(spokes[i], spokes[i + 1], brush, stroke, stroke_style);
          }
        });
        break;
      }
    }
  }

  void paint_tab_cell(const RECT& rc, const wchar_t* label, IDWriteTextFormat* fmt, bool active, bool feature_enabled,
                      Tab tab, int body_h) {
    const D2D1_COLOR_F text_color = active ? C(kUiTabActive) : C(kUiTextSecondary);
    const float icon_size = static_cast<float>(std::max(1, body_h));
    const float gap = static_cast<float>(dip(6));
    const float label_w = static_cast<float>(text_width(fmt, label));
    const float total_w = icon_size + gap + label_w;
    const float start_x = (static_cast<float>(rc.left + rc.right) - total_w) * 0.5f;
    const float row_center_y = (static_cast<float>(rc.top) + static_cast<float>(rc.bottom)) * 0.5f;
    const float icon_left = start_x;
    const float icon_top = row_center_y - icon_size * 0.5f;
    const float icon_cx = icon_left + icon_size * 0.5f;

    const D2D1_COLOR_F icon_color = feature_enabled ? C(kDefaultAuraSlotColors[0]) : text_color;
    if (feature_enabled) paint_tab_icon_glow(tab, icon_cx, row_center_y, icon_size);
    switch (tab) {
      case Tab::Aura:
        draw_icon_tab_aura(icon_cx, row_center_y, icon_size, icon_color);
        break;
      case Tab::Firefly:
        draw_icon_tab_firefly(icon_cx, row_center_y, icon_size, icon_color);
        break;
      case Tab::General:
        draw_icon_tab_general(icon_cx, row_center_y, icon_size, icon_color);
        break;
    }

    D2D1_RECT_F text{};
    text.left = icon_left + icon_size + gap;
    text.right = std::min(static_cast<float>(rc.right), start_x + total_w + static_cast<float>(dip(4)));
    text.top = icon_top;
    text.bottom = icon_top + icon_size;
    draw_text(rt_.Get(), fmt, label, text, text_color, AlignH::Left, AlignV::Center, false);
  }

  void paint_back_button(const RECT& rc, IDWriteTextFormat* fmt, bool hover) {
    fill_round(rt_.Get(), r2f(rc), dip(10), hover ? C(kUiFillHover) : C(kUiFill));
    const float cy = (rc.top + rc.bottom) * 0.5f;
    const float icon = static_cast<float>(dip(16));
    const float pad = static_cast<float>(dip(8));
    const float icon_cx = static_cast<float>(rc.left) + pad + icon * 0.5f;
    draw_icon_back(icon_cx, cy, icon, C(kUiText));
    D2D1_RECT_F text = r2f(rc);
    text.left = icon_cx + icon * 0.5f + static_cast<float>(dip(4));
    text.right -= pad;
    draw_text(rt_.Get(), fmt, tr(lang(), StringId::kLangBack), text, C(kUiText), AlignH::Left, AlignV::Center);
  }

  void paint_remove_button(const RECT& rc, bool hover, float alpha = 1.f) {
    const float rad = static_cast<float>(std::min(rc.bottom - rc.top, rc.right - rc.left)) * 0.28f;
    if (hover) fill_round(rt_.Get(), r2f(rc), rad, C(kUiDangerFill, alpha));
    const float cx = (rc.left + rc.right) * 0.5f;
    const float cy = (rc.top + rc.bottom) * 0.5f;
    const float icon = static_cast<float>(std::min(rc.right - rc.left, rc.bottom - rc.top) - dip(8));
    draw_icon_trash(cx, cy, std::max(14.f, icon), hover ? C(kUiDanger, alpha) : C(kUiTextSecondary, alpha));
  }

  void paint_add_slot_button(const RECT& rc, IDWriteTextFormat* fmt, bool hover, float alpha = 1.f) {
    fill_round(rt_.Get(), r2f(rc), dip(10), hover ? C(kUiFillHover, alpha) : C(kUiFill, alpha));
    const float cy = (rc.top + rc.bottom) * 0.5f;
    const float icon = static_cast<float>(dip(16));
    const float pad = static_cast<float>(dip(8));
    const float icon_cx = static_cast<float>(rc.left) + pad + icon * 0.5f;
    draw_icon_add(icon_cx, cy, icon, C(kUiText, alpha));
    D2D1_RECT_F text = r2f(rc);
    text.left = icon_cx + icon * 0.5f + static_cast<float>(dip(4));
    text.right -= pad;
    draw_text(rt_.Get(), fmt, tr(lang(), StringId::kAddColorSlot), text, C(kUiText, alpha), AlignH::Left,
              AlignV::Center);
  }

  void paint_toggle_row(const RECT& rc, float v, const wchar_t* label, IDWriteTextFormat* fmt) {
    const int tw = dip(kUiToggleW);
    const int th = dip(kUiToggleH);
    const int knob = dip(kUiToggleKnob);
    const int row = rc.bottom - rc.top;
    const int ty = rc.top + (row - th) / 2;
    const float track = static_cast<float>(rc.right - dip(4) - tw);
    const D2D1_RECT_F trc =
        D2D1::RectF(track, static_cast<float>(ty), track + static_cast<float>(tw), static_cast<float>(ty + th));
    const float t = std::clamp(v, 0.f, 1.f);
    fill_round(rt_.Get(), trc, static_cast<float>(th) * 0.5f, lerp_color(C(kUiFill), C(kDefaultColorEn), t));
    const float kx = trc.left + 3.f + (tw - knob - 6) * t;
    const float ky = trc.top + (th - knob) * 0.5f;
    ComPtr<ID2D1SolidColorBrush> knob_br;
    rt_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), knob_br.GetAddressOf());
    if (knob_br) {
      rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx + knob * 0.5f, ky + knob * 0.5f), knob * 0.5f, knob * 0.5f),
                       knob_br.Get());
    }
    D2D1_RECT_F label_rc = r2f(rc);
    label_rc.right = trc.left - static_cast<float>(dip(kUiRowGap));
    draw_text(rt_.Get(), fmt, label, label_rc, C(kUiText), AlignH::Left, AlignV::Center);
  }

  void paint_status_lamp(const RECT& rc, bool on, const D2D1_COLOR_F& on_color, const wchar_t* label,
                         IDWriteTextFormat* fmt) {
    const float cy = (rc.top + rc.bottom) * 0.5f;
    const float radius = static_cast<float>(std::max(4, dip(5)));
    const float cx = static_cast<float>(rc.left) + radius;
    ComPtr<ID2D1SolidColorBrush> brush;
    const D2D1_COLOR_F fill = on ? on_color : C(kUiFill);
    rt_->CreateSolidColorBrush(fill, brush.GetAddressOf());
    if (brush) {
      rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush.Get());
      if (on) {
        ComPtr<ID2D1SolidColorBrush> rim;
        rt_->CreateSolidColorBrush(D2D1::ColorF(fill.r, fill.g, fill.b, 0.35f), rim.GetAddressOf());
        if (rim) {
          rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius + 1.5f, radius + 1.5f), rim.Get(), 2.f);
        }
      }
    }
    D2D1_RECT_F text = r2f(rc);
    text.left = cx + radius + static_cast<float>(dip(8));
    draw_text(rt_.Get(), fmt, label, text, C(kUiTextSecondary), AlignH::Left, AlignV::Center);
  }

  void paint_lang_dropdown(const RECT& rc, const wchar_t* label, IDWriteTextFormat* fmt, bool hover,
                           float alpha = 1.f) {
    fill_round(rt_.Get(), r2f(rc), dip(10), hover ? C(kUiFillHover, alpha) : C(kUiFill, alpha));
    D2D1_RECT_F text = r2f(rc);
    text.left += static_cast<float>(dip(kUiButtonPadX));
    text.right -= static_cast<float>(dip(28));
    draw_text(rt_.Get(), fmt, label, text, C(kUiText, alpha), AlignH::Left, AlignV::Center);

    ComPtr<ID2D1SolidColorBrush> chev;
    rt_->CreateSolidColorBrush(C(kUiTextSecondary, alpha), chev.GetAddressOf());
    if (!chev) return;
    const float cx = static_cast<float>(rc.right - dip(14));
    const float cy = (rc.top + rc.bottom) * 0.5f;
    const float s = static_cast<float>(dip(5));
    ComPtr<ID2D1PathGeometry> geo;
    ID2D1Factory* fac = nullptr;
    rt_->GetFactory(&fac);
    if (!fac || FAILED(fac->CreatePathGeometry(geo.GetAddressOf())) || !geo) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(sink.GetAddressOf())) || !sink) return;
    sink->BeginFigure(D2D1::Point2F(cx - s, cy - s * 0.35f), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(cx, cy + s * 0.55f));
    sink->AddLine(D2D1::Point2F(cx + s, cy - s * 0.35f));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    rt_->FillGeometry(geo.Get(), chev.Get());
  }

  void paint_swatch(const RECT& rc, const Rgba& color, bool hover, float alpha = 1.f) {
    const float rad = (rc.bottom - rc.top) * 0.5f;
    fill_round(rt_.Get(), r2f(rc), rad, C(color, alpha));
    if (hover) {
      ComPtr<ID2D1SolidColorBrush> edge;
      rt_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.45f * alpha), edge.GetAddressOf());
      rt_->DrawRoundedRectangle(RoundRect(r2f(rc), rad), edge.Get(), 1.5f);
    }
    ComPtr<ID2D1SolidColorBrush> chev;
    rt_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f * alpha), chev.GetAddressOf());
    const float cx = static_cast<float>(rc.right - dip(16));
    const float cy = (rc.top + rc.bottom) * 0.5f;
    ComPtr<ID2D1PathGeometry> geo;
    ID2D1Factory* fac = nullptr;
    rt_->GetFactory(&fac);
    fac->CreatePathGeometry(geo.GetAddressOf());
    ComPtr<ID2D1GeometrySink> sink;
    geo->Open(sink.GetAddressOf());
    sink->BeginFigure(D2D1::Point2F(cx - 3, cy - 5), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(cx + 4, cy));
    sink->AddLine(D2D1::Point2F(cx - 3, cy + 5));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    rt_->FillGeometry(geo.Get(), chev.Get());
  }

  void paint_rule(const RECT& rc) {
    ComPtr<ID2D1SolidColorBrush> b;
    rt_->CreateSolidColorBrush(C(kUiSeparator), b.GetAddressOf());
    rt_->FillRectangle(r2f(rc), b.Get());
  }

  void paint_slider() {
    const auto track = r2f(width_track_);
    const float mid_y = (track.top + track.bottom) * 0.5f;
    ComPtr<ID2D1SolidColorBrush> back;
    rt_->CreateSolidColorBrush(C(kUiFill), back.GetAddressOf());
    rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(track.left, mid_y - 2, track.right, mid_y + 2), 2, 2),
                              back.Get());
    const float t = (settings_.gradient_width - kGradientWidthMin) /
                    static_cast<float>(kGradientWidthMax - kGradientWidthMin);
    const float x = track.left + t * (track.right - track.left);
    ComPtr<ID2D1SolidColorBrush> acc;
    rt_->CreateSolidColorBrush(C(kDefaultColorEn), acc.GetAddressOf());
    rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(track.left, mid_y - 2, x, mid_y + 2), 2, 2), acc.Get());
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, mid_y), 7.f, 7.f), acc.Get());
  }

  void paint_radio(const RECT& rc, bool on, const wchar_t* label, IDWriteTextFormat* fmt, float alpha = 1.f) {
    if (rc.bottom <= rc.top) return;
    const float r = static_cast<float>(std::max(1, dip(7)));
    const float cy = (rc.top + rc.bottom) * 0.5f;
    const float cx = static_cast<float>(rc.left) + r + static_cast<float>(dip(4));
    const D2D1_COLOR_F accent = on ? C(kDefaultColorEn, alpha) : C(kUiTextSecondary, alpha);
    ComPtr<ID2D1SolidColorBrush> ring;
    rt_->CreateSolidColorBrush(accent, ring.GetAddressOf());
    rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), ring.Get(), 1.5f);
    if (on) rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 4.f / 7.f, r * 4.f / 7.f), ring.Get());
    D2D1_RECT_F text = r2f(rc);
    text.left = cx + r + static_cast<float>(dip(kUiRowGap));
    draw_text(rt_.Get(), fmt, label, text, C(kUiText, alpha), AlignH::Left, AlignV::Center);
  }

  void paint_check(const RECT& rc, bool on, const wchar_t* label, IDWriteTextFormat* fmt, float alpha = 1.f) {
    const float s = static_cast<float>(std::max(1, dip(14)));
    const float x = static_cast<float>(rc.left + dip(4));
    const float y = (rc.top + rc.bottom) * 0.5f - s * 0.5f;
    const D2D1_RECT_F box = D2D1::RectF(x, y, x + s, y + s);
    const D2D1_COLOR_F fill_color = on ? C(kDefaultColorEn) : C(kUiFill);
    fill_round(rt_.Get(), box, 3.f, fill_color);

    ComPtr<ID2D1SolidColorBrush> stroke;
    rt_->CreateSolidColorBrush(on ? C(kDefaultColorEn) : C(kUiTextSecondary, 0.72f), stroke.GetAddressOf());
    rt_->DrawRoundedRectangle(RoundRect(box, 3.f), stroke.Get(), 1.2f);

    if (on) {
      ComPtr<ID2D1SolidColorBrush> mark;
      rt_->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 1.f), mark.GetAddressOf());
      const D2D1_POINT_2F p1 = D2D1::Point2F(x + s * 0.24f, y + s * 0.54f);
      const D2D1_POINT_2F p2 = D2D1::Point2F(x + s * 0.43f, y + s * 0.72f);
      const D2D1_POINT_2F p3 = D2D1::Point2F(x + s * 0.78f, y + s * 0.32f);
      const float mark_thickness = std::max(1.75f, static_cast<float>(dip(2)));
      rt_->DrawLine(p1, p2, mark.Get(), mark_thickness);
      rt_->DrawLine(p2, p3, mark.Get(), mark_thickness);
    }

    D2D1_RECT_F text = r2f(rc);
    text.left = x + s + static_cast<float>(dip(kUiRowGap));
    draw_text(rt_.Get(), fmt, label, text, C(kUiText, alpha), AlignH::Left, AlignV::Center);
  }

  void paint_segments() {
    const wchar_t* labels[3] = {tr(lang(), StringId::kFontSmall), tr(lang(), StringId::kFontMedium),
                                tr(lang(), StringId::kFontLarge)};
    const char* keys[3] = {kFontSizeSmall, kFontSizeMedium, kFontSizeLarge};
    RECT parts[3] = {font_small_, font_medium_, font_large_};

    struct Circ {
      float cx = 0.f;
      float cy = 0.f;
      float r = 0.f;
    };
    auto as_circle = [](const RECT& rc) {
      Circ c;
      c.cx = (rc.left + rc.right) * 0.5f;
      c.cy = (rc.top + rc.bottom) * 0.5f;
      c.r = std::min(rc.right - rc.left, rc.bottom - rc.top) * 0.5f;
      return c;
    };
    const Circ c0 = as_circle(parts[0]);
    const Circ c1 = as_circle(parts[1]);
    const Circ c2 = as_circle(parts[2]);
    (void)c1;

    // Outer tapered capsule: narrow left, wide right, rounded ends.
    {
      const float pad = static_cast<float>(dip(6));
      const float rl = c0.r + pad;
      const float rr = c2.r + pad;
      const float cy = c0.cy;
      ComPtr<ID2D1PathGeometry> geo;
      ID2D1Factory* fac = nullptr;
      rt_->GetFactory(&fac);
      if (fac && SUCCEEDED(fac->CreatePathGeometry(geo.GetAddressOf())) && geo) {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(geo->Open(sink.GetAddressOf())) && sink) {
          sink->BeginFigure(D2D1::Point2F(c0.cx, cy - rl), D2D1_FIGURE_BEGIN_FILLED);
          sink->AddLine(D2D1::Point2F(c2.cx, cy - rr));
          sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(c2.cx, cy + rr), D2D1::SizeF(rr, rr), 0.f,
                                        D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
          sink->AddLine(D2D1::Point2F(c0.cx, cy + rl));
          sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(c0.cx, cy - rl), D2D1::SizeF(rl, rl), 0.f,
                                        D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
          sink->EndFigure(D2D1_FIGURE_END_CLOSED);
          sink->Close();
          ComPtr<ID2D1SolidColorBrush> fill;
          rt_->CreateSolidColorBrush(C(kUiFill), fill.GetAddressOf());
          if (fill) rt_->FillGeometry(geo.Get(), fill.Get());
        }
      }
    }

    int sel = -1;
    for (int i = 0; i < 3; ++i) {
      if (settings_.ui_font_size == keys[i]) sel = i;
    }
    if (sel >= 0) {
      Circ hi = as_circle(parts[sel]);
      if (seg_anim_t_ < 1.f) {
        auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
        const float from_cx = (seg_from_left_ + seg_from_right_) * 0.5f;
        const float from_r = std::max(1.f, (seg_from_right_ - seg_from_left_) * 0.5f);
        hi.cx = lerp(from_cx, hi.cx, seg_anim_t_);
        hi.r = lerp(from_r, hi.r, seg_anim_t_);
      }
      ComPtr<ID2D1SolidColorBrush> sel_br;
      rt_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.94f), sel_br.GetAddressOf());
      if (sel_br) rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(hi.cx, hi.cy), hi.r, hi.r), sel_br.Get());
    }

    for (int i = 0; i < 3; ++i) {
      auto fmt = make_format(ui_font_point_size(keys[i]), DWRITE_FONT_WEIGHT_BOLD);
      draw_text(rt_.Get(), fmt.Get(), labels[i], r2f(parts[i]), C(kUiText), AlignH::Center, AlignV::Center, false);
    }
  }

  static D2D1_RECT_F r2f(const RECT& r) {
    return D2D1::RectF(static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
                       static_cast<float>(r.bottom));
  }

  RECT box(int x, int y, int w, int h) const { return RECT{x, y, x + w, y + h}; }

  void layout(const UiMetrics& um, IDWriteTextFormat* body_fmt, IDWriteTextFormat* sub_fmt) {
    const int m = dip(kUiMargin);
    const int ind = dip(kUiIndent);
    const int gap = dip(kUiRowGap);
    const int sec = dip(kUiSectionGap);
    const int row = um.row_h;
    const int inner = content_width();
    const int radio_gap = dip(kUiSpace1);
    const int x0 = m;
    const int x1 = m + ind;
    const int x2 = m + ind * 2;
    const int w0 = inner;
    const int w1 = std::max(1, inner - ind);
    const int w2 = std::max(1, inner - ind * 2);

    slot_lang_.assign(kMaxAuraSlots, RECT{});
    slot_swatch_.assign(kMaxAuraSlots, RECT{});
    slot_remove_.assign(kMaxAuraSlots, RECT{});
    lang_pick_rows_.clear();

    auto wrapped = [&](IDWriteTextFormat* fmt, const wchar_t* text, int max_w) {
      return text_height_wrapped(fmt, text, max_w);
    };

    if (page_ == Page::LangPicker) {
      int y = m;
      const int header_h = std::max(row, um.title_h);
      const int back_w = std::min(w0, um.back_btn_w);
      lang_back_ = box(x0, y, back_w, header_h);
      const int title_x = lang_back_.right + gap;
      lang_picker_title_ = box(title_x, y, std::max(0, x0 + w0 - title_x), header_h);
      y += header_h + sec;
      size_t n = 0;
      const auto* cat = ui_language_catalog(n);
      for (size_t i = 0; i < n; ++i) {
        const int rh =
            std::max(row, wrapped(body_fmt, tr(lang(), string_id_for_ui_lang(cat[i].id)), w1) + dip(kUiButtonPadY));
        lang_pick_rows_.push_back(box(x1, y, w1, rh));
        y += rh + radio_gap;
      }
      y += m;
      content_height_ = y;
      return;
    }

    if (page_ == Page::FireflyBusyPicker) {
      int y = m;
      const int header_h = std::max(row, um.title_h);
      const int back_w = std::min(w0, um.back_btn_w);
      ff_busy_back_ = box(x0, y, back_w, header_h);
      const int title_x = ff_busy_back_.right + gap;
      ff_busy_picker_title_ = box(title_x, y, std::max(0, x0 + w0 - title_x), header_h);
      y += header_h + sec;
      ff_busy_pick_rows_.clear();
      const auto& catalog = busy_action_catalog();
      for (size_t i = 0; i < catalog.size(); ++i) {
        const int rh = std::max(
            row, wrapped(body_fmt, tr(lang(), string_id_for_busy_action(std::string(catalog[i]))), w1) + dip(kUiButtonPadY));
        ff_busy_pick_rows_.push_back(box(x1, y, w1, rh));
        y += rh + radio_gap;
      }
      ff_custom_key_picker_ = box(0, 0, 0, 0);
      if (settings_.firefly_busy_action == kFireflyBusyCustomKey) {
        const int rh = std::max(row, wrapped(body_fmt, tr(lang(), StringId::kFireflyCustomKeyPrompt), w1) + dip(8));
        ff_custom_key_picker_ = box(x1, y, w1, rh);
        y += rh + radio_gap;
      }
      y += m;
      content_height_ = y;
      return;
    }

    if (active_tab_ == Tab::Aura) {
      int y = m;
      aura_title_ = box(x0, y, w0, um.title_h);
      y += um.title_h + gap;
      const int aura_sub_h = wrapped(sub_fmt, tr(lang(), StringId::kAuraSub), w0);
      aura_sub_ = box(x0, y, w0, aura_sub_h);
      y += aura_sub_h + gap;
      aura_toggle_ = box(x0, y, w0, row);
      y += row + sec;

      sec_color_title_ = box(x0, y, w0, um.title_h);
      y += um.title_h + gap;
      const int color_sub_h = wrapped(sub_fmt, tr(lang(), StringId::kColorSub), w1);
      sec_color_sub_ = box(x1, y, w1, color_sub_h);
      y += color_sub_h + gap;
      const int color_row = std::max(row, um.swatch_h);
      const int sw_w = std::min(um.swatch_w, w1 * 30 / 100);
      const int rem_h = um.swatch_h;
      const int rem_w = std::max(dip(22), rem_h * 3 / 4);
      for (size_t i = 0; i < settings_.aura_slots.size(); ++i) {
        const bool can_remove = settings_.aura_slots.size() > static_cast<size_t>(kMinAuraSlots);
        const int rem_gap = dip(4);
        const int rem_extra = can_remove ? rem_w + rem_gap : 0;
        const int right_block = sw_w + rem_extra;
        const int lang_w = std::min(um.lang_drop_w, std::max(1, w1 - right_block - gap));
        const int flex = std::max(gap, w1 - lang_w - right_block);
        slot_lang_[i] = box(x1, y, lang_w, color_row);
        slot_swatch_[i] = box(x1 + lang_w + flex, y + (color_row - um.swatch_h) / 2, sw_w, um.swatch_h);
        if (can_remove) {
          const int rem_y = y + (color_row - rem_h) / 2;
          slot_remove_[i] = box(x1 + lang_w + flex + sw_w + rem_gap, rem_y, rem_w, rem_h);
        } else {
          slot_remove_[i] = box(0, 0, 0, 0);
        }
        y += color_row + gap;
      }
      if (static_cast<int>(settings_.aura_slots.size()) < kMaxAuraSlots) {
        const int add_w = std::min(w1, um.add_slot_w);
        add_slot_ = box(x1 + (w1 - add_w) / 2, y, add_w, row);
        y += row + gap;
      } else {
        add_slot_ = box(0, 0, 0, 0);
      }
      {
        const int rh = std::max(row, wrapped(body_fmt, tr(lang(), StringId::kColorReset), w1));
        reset_colors_ = box(x1, y, w1, rh);
        y += rh + sec;
      }
      rule1_ = box(x0, y, w0, 1);
      y += sec;

      sec_width_title_ = box(x0, y, w0, um.title_h);
      y += um.title_h + gap;
      const int width_sub_h = wrapped(sub_fmt, tr(lang(), StringId::kWidthSub), w1);
      sec_width_sub_ = box(x1, y, w1, width_sub_h);
      y += width_sub_h + gap;
      const int value_w = std::max(dip(48), w1 * 20 / 100);
      width_value_ = box(x1 + w1 - value_w, y, value_w, row);
      width_track_ = box(x1, y, std::max(1, w1 - value_w - gap), row);
      y += row + gap;
      {
        const int rh = std::max(row, wrapped(body_fmt, tr(lang(), StringId::kWidthReset), w1));
        reset_width_ = box(x1, y, w1, rh);
        y += rh + sec;
      }
      rule2_ = box(x0, y, w0, 1);
      y += sec;

      sec_disp_title_ = box(x0, y, w0, um.title_h);
      y += um.title_h + gap;
      {
        const int rh = std::max(row, wrapped(body_fmt, tr(lang(), StringId::kDisplayAlways), w1) + dip(4));
        mode_always_ = box(x1, y, w1, rh);
        y += rh + radio_gap;
      }
      {
        const int rh = std::max(row, wrapped(body_fmt, tr(lang(), StringId::kDisplayFocus), w1) + dip(4));
        mode_focus_ = box(x1, y, w1, rh);
        y += rh;
      }
      if (settings_.display_mode == kDisplayModeOnFocus) {
        const int full_h = std::max(row, wrapped(body_fmt, tr(lang(), StringId::kDisplayHover), w2) + dip(4));
        const int hover_h = std::max(1, static_cast<int>(std::lround(full_h * hover_reveal_t_)));
        hover_box_ = box(x2, y, w2, hover_h);
        y += hover_h;
      } else {
        hover_box_ = box(x2, y, 0, 0);
      }
      y += radio_gap + m;
      content_height_ = y;
    } else if (active_tab_ == Tab::Firefly) {
      int y = m;
      ff_title_ = box(x0, y, w0, um.title_h);
      y += um.title_h + gap;
      const int ff_sub_h = wrapped(sub_fmt, tr(lang(), StringId::kFireflySub), w0);
      ff_sub_ = box(x0, y, w0, ff_sub_h);
      y += ff_sub_h + gap;
      ff_toggle_ = box(x0, y, w0, row);
      y += row + sec;
      {
        const int rh = wrapped(sub_fmt, tr(lang(), StringId::kFireflyBusySection), w1);
        ff_busy_title_ = box(x1, y, w1, rh);
        y += rh + gap;
      }
      ff_busy_change_ = box(x1, y, w1, row);
      y += row + gap;
      if (settings_.firefly_busy_action == kFireflyBusyKeepAwake) {
        const int rh = std::max(row, wrapped(body_fmt, tr(lang(), StringId::kFireflyKeepDisplayOn), w1) + dip(4));
        ff_keep_display_ = box(x1, y, w1, rh);
        y += rh + gap;
      } else {
        ff_keep_display_ = box(x1, y, 0, 0);
      }
      if (settings_.firefly_busy_action == kFireflyBusyCustomKey) {
        const int rh = std::max(row, wrapped(body_fmt, tr(lang(), StringId::kFireflyCustomKeyPrompt), w1) + dip(4));
        ff_custom_key_ = box(x1, y, w1, rh);
        y += rh + gap;
      } else {
        ff_custom_key_ = box(x1, y, 0, 0);
      }
      y += sec - gap;
      {
        const int rh = wrapped(sub_fmt, tr(lang(), StringId::kFireflyCapsSection), w1);
        ff_caps_title_ = box(x1, y, w1, rh);
        y += rh + gap;
      }
      auto caps_row = [&](StringId id, RECT& out) {
        const int rh = std::max(row, wrapped(body_fmt, tr(lang(), id), w1) + dip(4));
        out = box(x1, y, w1, rh);
        y += rh + radio_gap;
      };
      caps_row(StringId::kFireflyCapsPreserve, ff_caps_preserve_);
      caps_row(StringId::kFireflyCapsUppercase, ff_caps_upper_);
      caps_row(StringId::kFireflyCapsLowercase, ff_caps_lower_);
      y += sec - radio_gap;
      ff_status_ = box(x1, y, w1, row);
      y += row + gap;
      ff_caps_ok_ = box(x1, y, w1, std::max(um.sub_h, wrapped(sub_fmt, tr(lang(), StringId::kFireflyCapsOk), w1)));
      y += (ff_caps_ok_.bottom - ff_caps_ok_.top) + dip(kUiSpace1);
      ff_led_ok_ = box(x1, y, w1, std::max(um.sub_h, wrapped(sub_fmt, tr(lang(), StringId::kFireflyLedOk), w1)));
      y += (ff_led_ok_.bottom - ff_led_ok_.top) + dip(kUiSpace1);
      ff_dnd_ok_ = box(x1, y, w1, std::max(um.sub_h, wrapped(sub_fmt, tr(lang(), StringId::kFireflyDndOk), w1)));
      y += (ff_dnd_ok_.bottom - ff_dnd_ok_.top) + dip(kUiSpace1);
      ff_keep_awake_ok_ =
          box(x1, y, w1, std::max(um.sub_h, wrapped(sub_fmt, tr(lang(), StringId::kFireflyKeepAwakeOk), w1)));
      y += (ff_keep_awake_ok_.bottom - ff_keep_awake_ok_.top) + dip(kUiSpace1);
      ff_voice_ok_ = box(x1, y, w1, std::max(um.sub_h, wrapped(sub_fmt, tr(lang(), StringId::kFireflyVoiceOk), w1)));
      y += (ff_voice_ok_.bottom - ff_voice_ok_.top) + dip(kUiSpace1);
      ff_mic_ok_ = box(x1, y, w1, std::max(um.sub_h, wrapped(sub_fmt, tr(lang(), StringId::kFireflyMicOk), w1)));
      y += (ff_mic_ok_.bottom - ff_mic_ok_.top) + dip(kUiSpace1);
      ff_speaker_ok_ =
          box(x1, y, w1, std::max(um.sub_h, wrapped(sub_fmt, tr(lang(), StringId::kFireflySpeakerOk), w1)));
      y += (ff_speaker_ok_.bottom - ff_speaker_ok_.top) + dip(kUiSpace1);
      ff_custom_key_ok_ =
          box(x1, y, w1, std::max(um.sub_h, wrapped(sub_fmt, tr(lang(), StringId::kFireflyCustomKeyPrompt), w1)));
      y += (ff_custom_key_ok_.bottom - ff_custom_key_ok_.top) + m;
      content_height_ = y;
    } else {
      int y = m;
      sec_font_title_ = box(x0, y, w0, um.title_h);
      y += um.title_h + gap;
      const int font_sub_h = wrapped(sub_fmt, tr(lang(), StringId::kFontSub), w1);
      sec_font_sub_ = box(x1, y, w1, font_sub_h);
      y += font_sub_h + gap;
      font_bar_ = box(x0, y, w0, um.font_bar_h);
      const int cy = y + um.font_bar_h / 2;
      const int pad = dip(6);
      const int cx0 = x0 + pad + um.font_r_s;
      const int cx2 = x0 + w0 - pad - um.font_r_l;
      const int cx1 = (cx0 + cx2) / 2;
      font_small_ = box(cx0 - um.font_r_s, cy - um.font_r_s, um.font_r_s * 2, um.font_r_s * 2);
      font_medium_ = box(cx1 - um.font_r_m, cy - um.font_r_m, um.font_r_m * 2, um.font_r_m * 2);
      font_large_ = box(cx2 - um.font_r_l, cy - um.font_r_l, um.font_r_l * 2, um.font_r_l * 2);
      y += um.font_bar_h + sec;
      rule3_ = box(x0, y, w0, 1);
      y += sec;
      // Language section (title + change button); about/quit follow.
      // Positions must match paint() for the language block.
      {
        // Reserve: title + gap + row + sec (lang_rule) — paint recomputes lang_change_ from rule3_.
        y += um.title_h + gap + row + sec;
      }
      lang_rule_ = box(x0, y, w0, 1);
      y += sec;
      easy_quit_ = box(x0, y, w0, row);
      y += row + sec;
      rule4_ = box(x0, y, w0, 1);
      y += sec;
      about_ = box(x0, y, w0, row);
      y += row + gap;
      quit_ = box(x0, y, w0, row);
      y += row + m;
      content_height_ = y;
    }
  }

  bool contains(const RECT& r, int x, int y) const {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
  }

  bool contains_circle(const RECT& r, int x, int y) const {
    const float cx = (r.left + r.right) * 0.5f;
    const float cy = (r.top + r.bottom) * 0.5f;
    const float rad = std::min(r.right - r.left, r.bottom - r.top) * 0.5f;
    const float dx = static_cast<float>(x) - cx;
    const float dy = static_cast<float>(y) - cy;
    return dx * dx + dy * dy <= rad * rad;
  }

  Hit hit_test(int x, int y) const {
    if (!on_picker_page()) {
      if (contains(tab_aura_, x, y)) return Hit::TabAura;
      if (contains(tab_firefly_, x, y)) return Hit::TabFirefly;
      if (contains(tab_general_, x, y)) return Hit::TabGeneral;
    }

    const int tab_h = tab_bar_h();
    if (!on_picker_page() && y < tab_h) return Hit::None;
    const int cy = y - tab_h + scroll_y_;

    if (scroll_max_ > 0 && contains(scroll_bar_, x, y)) return Hit::ScrollBar;

    if (page_ == Page::LangPicker) {
      if (contains(lang_back_, x, cy)) return Hit::LangBack;
      for (size_t i = 0; i < lang_pick_rows_.size(); ++i) {
        if (contains(lang_pick_rows_[i], x, cy))
          return static_cast<Hit>(static_cast<int>(Hit::LangPick0) + static_cast<int>(i));
      }
      return Hit::None;
    }
    if (page_ == Page::FireflyBusyPicker) {
      if (contains(ff_busy_back_, x, cy)) return Hit::FireflyBusyBack;
      if (ff_custom_key_picker_.bottom > ff_custom_key_picker_.top && contains(ff_custom_key_picker_, x, cy)) {
        return Hit::FireflyCustomKeyCapture;
      }
      const auto& catalog = busy_action_catalog();
      for (size_t i = 0; i < catalog.size() && i < ff_busy_pick_rows_.size(); ++i) {
        if (!busy_action_selectable(catalog[i])) continue;
        if (contains(ff_busy_pick_rows_[i], x, cy))
          return static_cast<Hit>(static_cast<int>(Hit::BusyPick0) + static_cast<int>(i));
      }
      return Hit::None;
    }

    if (active_tab_ == Tab::Aura) {
      if (slot_anim_active()) {
        for (size_t i = 0; i < settings_.aura_slots.size(); ++i) {
          float alpha = 1.f;
          const SlotRowRects row = visual_slot_row(i, &alpha);
          if (alpha > 0.05f) {
            if (contains(row.lang, x, cy))
              return static_cast<Hit>(static_cast<int>(Hit::SlotLang0) + static_cast<int>(i));
            if (contains(row.swatch, x, cy))
              return static_cast<Hit>(static_cast<int>(Hit::SlotSwatch0) + static_cast<int>(i));
            if (rect_valid(row.remove) && contains(row.remove, x, cy))
              return static_cast<Hit>(static_cast<int>(Hit::SlotRemove0) + static_cast<int>(i));
          }
        }
        if (slot_exit_.active) return Hit::None;
        const RECT add_rc = visual_add_slot();
        if (rect_valid(add_rc) && contains(add_rc, x, cy)) return Hit::AddSlot;
      } else {
        for (size_t i = 0; i < settings_.aura_slots.size(); ++i) {
          if (contains(slot_lang_[i], x, cy))
            return static_cast<Hit>(static_cast<int>(Hit::SlotLang0) + static_cast<int>(i));
          if (contains(slot_swatch_[i], x, cy))
            return static_cast<Hit>(static_cast<int>(Hit::SlotSwatch0) + static_cast<int>(i));
          if (slot_remove_[i].right > slot_remove_[i].left && contains(slot_remove_[i], x, cy))
            return static_cast<Hit>(static_cast<int>(Hit::SlotRemove0) + static_cast<int>(i));
        }
        if (add_slot_.bottom > add_slot_.top && contains(add_slot_, x, cy)) return Hit::AddSlot;
      }
      if (contains(reset_colors_, x, cy)) return Hit::ResetColors;
      if (contains(width_track_, x, cy)) return Hit::WidthTrack;
      if (contains(width_value_, x, cy)) return Hit::WidthValue;
      if (contains(reset_width_, x, cy)) return Hit::ResetWidth;
      if (contains(mode_always_, x, cy)) return Hit::ModeAlways;
      if (contains(mode_focus_, x, cy)) return Hit::ModeFocus;
      if (contains(aura_toggle_, x, cy)) return Hit::AuraToggle;
      if (settings_.display_mode == kDisplayModeOnFocus && hover_box_.bottom > hover_box_.top &&
          contains(hover_box_, x, cy))
        return Hit::Hover;
    } else if (active_tab_ == Tab::Firefly) {
      if (contains(ff_toggle_, x, cy)) return Hit::FireflyToggle;
      if (settings_.firefly_enabled) {
        if (contains(ff_busy_change_, x, cy)) return Hit::FireflyBusyChange;
        if (settings_.firefly_busy_action == kFireflyBusyKeepAwake && ff_keep_display_.bottom > ff_keep_display_.top &&
            contains(ff_keep_display_, x, cy))
          return Hit::FireflyKeepDisplay;
        if (contains(ff_caps_preserve_, x, cy)) return Hit::FireflyCapsPreserve;
        if (contains(ff_caps_upper_, x, cy)) return Hit::FireflyCapsUppercase;
        if (contains(ff_caps_lower_, x, cy)) return Hit::FireflyCapsLowercase;
      }
    } else {
      if (contains_circle(font_small_, x, cy)) return Hit::FontSmall;
      if (contains_circle(font_medium_, x, cy)) return Hit::FontMedium;
      if (contains_circle(font_large_, x, cy)) return Hit::FontLarge;
      if (contains(lang_change_, x, cy)) return Hit::LangChange;
      if (contains(easy_quit_, x, cy)) return Hit::EasyQuitToggle;
      if (contains(about_, x, cy)) return Hit::About;
      if (contains(quit_, x, cy)) return Hit::Quit;
    }
    return Hit::None;
  }

  void pick_slot_language(size_t index) {
    if (index >= settings_.aura_slots.size() || index >= slot_lang_.size()) return;
    std::vector<std::string> used;
    for (size_t i = 0; i < settings_.aura_slots.size(); ++i) {
      if (i != index) used.push_back(settings_.aura_slots[i].lang_id);
    }
    const std::string& current = settings_.aura_slots[index].lang_id;
    const auto choices = aura_slot_language_choices(used, current);
    if (choices.empty()) return;

    HMENU menu = CreatePopupMenu();
    UINT selected_id = 0;
    for (size_t i = 0; i < choices.size(); ++i) {
      const bool selected = choices[i] == current;
      const UINT id = static_cast<UINT>(i + 1);
      UINT flags = MF_STRING | (selected ? MF_CHECKED : MF_UNCHECKED);
      AppendMenuW(menu, flags, id, input_language_display_name(choices[i], lang() != Lang::En));
      if (selected) selected_id = id;
    }
    if (selected_id != 0) {
      CheckMenuRadioItem(menu, 1, static_cast<UINT>(choices.size()), selected_id, MF_BYCOMMAND);
      SetMenuDefaultItem(menu, selected_id, FALSE);
    }
    // Anchor under the language dropdown (client DIP → screen pixels).
    const RECT& rc = slot_lang_[index];
    POINT pt{MulDiv(rc.left, dpi_, 96),
             MulDiv(tab_bar_h() - scroll_y_ + rc.bottom, dpi_, 96)};
    ClientToScreen(hwnd_, &pt);
    const int cmd =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (cmd <= 0) return;
    const size_t choice = static_cast<size_t>(cmd - 1);
    if (choice >= choices.size()) return;
    if (settings_.aura_slots[index].lang_id == choices[choice]) return;
    settings_.aura_slots[index].lang_id = choices[choice];
    emit();
  }

  void emit() {
    settings_ = normalize_settings(settings_);
    if (callback_) callback_(settings_);
    enforce_min_client_width();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  bool pick_color(Rgba& color) { return win_show_color_dialog(hwnd_, color, color); }

  void set_width_from_x(int x) {
    const float left = static_cast<float>(width_track_.left);
    const float right = static_cast<float>(width_track_.right);
    float t = (x - left) / (right - left);
    t = std::clamp(t, 0.f, 1.f);
    settings_.gradient_width =
        kGradientWidthMin + static_cast<int>(std::round(t * (kGradientWidthMax - kGradientWidthMin)));
    width_editing_ = false;
    width_edit_buf_.clear();
    emit();
  }

  void commit_width_edit() {
    if (width_edit_buf_.empty()) {
      width_editing_ = false;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    int value = 0;
    try {
      value = std::stoi(width_edit_buf_);
    } catch (...) {
      value = settings_.gradient_width;
    }
    settings_.gradient_width = std::clamp(value, kGradientWidthMin, kGradientWidthMax);
    width_editing_ = false;
    width_edit_buf_.clear();
    emit();
  }

  void on_mouse(int px, int py, bool down, bool click) {
    const int x = MulDiv(px, 96, dpi_);
    const int y = MulDiv(py, 96, dpi_);
    if (dragging_scroll_ && down && scroll_max_ > 0) {
      const int track_h = scroll_bar_.bottom - scroll_bar_.top;
      const int thumb_h = std::max(dip(24), track_h * client_height_ / std::max(content_height_, 1));
      const int usable = std::max(1, track_h - thumb_h);
      scroll_y_ = (y - scroll_bar_.top - thumb_h / 2) * scroll_max_ / usable;
      scroll_y_ = std::clamp(scroll_y_, 0, scroll_max_);
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }

    hover_ = hit_test(x, y);
    if (dragging_width_ && down) {
      set_width_from_x(x);
      return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (!click) return;

    switch (hover_) {
      case Hit::TabAura:
        activate_tab(Tab::Aura);
        break;
      case Hit::TabFirefly:
        activate_tab(Tab::Firefly);
        break;
      case Hit::TabGeneral:
        activate_tab(Tab::General);
        break;
      case Hit::FireflyToggle: {
        const bool next = !settings_.firefly_enabled;
        start_toggle_anim(ToggleId::Firefly, next);
        settings_.firefly_enabled = next;
        if (!next) firefly_active_ = false;
        emit();
        break;
      }
      case Hit::FireflyCapsPreserve:
        settings_.firefly_caps_mode = kFireflyCapsPreserve;
        emit();
        break;
      case Hit::FireflyCapsUppercase:
        settings_.firefly_caps_mode = kFireflyCapsUppercase;
        emit();
        break;
      case Hit::FireflyCapsLowercase:
        settings_.firefly_caps_mode = kFireflyCapsLowercase;
        emit();
        break;
      case Hit::FireflyBusyChange:
        page_ = Page::FireflyBusyPicker;
        scroll_y_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
      case Hit::FireflyBusyBack:
        page_ = Page::Main;
        firefly_key_capture_ = false;
        emit();
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
      case Hit::FireflyCustomKeyCapture:
        firefly_key_capture_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
      case Hit::FireflyKeepDisplay:
        settings_.firefly_keep_display_on = !settings_.firefly_keep_display_on;
        emit();
        break;
      case Hit::ScrollBar:
        dragging_scroll_ = true;
        break;
      case Hit::LangChange:
        page_ = Page::LangPicker;
        scroll_y_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
      case Hit::LangBack:
        page_ = Page::Main;
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
      case Hit::AddSlot: {
        if (slot_anim_active()) break;
        std::vector<std::string> used;
        for (const auto& s : settings_.aura_slots) used.push_back(s.lang_id);
        auto unused = unused_input_languages(used);
        if (unused.empty()) break;
        capture_slot_anim_from();
        AuraColorSlot slot;
        slot.lang_id = unused.front();
        slot.color = settings_.default_color_for_new_slot(settings_.aura_slots.size());
        settings_.aura_slots.push_back(slot);
        slot_anim_kind_ = SlotAnimKind::Add;
        slot_anim_index_ = settings_.aura_slots.size() - 1;
        slot_anim_pending_ = true;
        slot_exit_.active = false;
        emit();
        break;
      }
      case Hit::ResetColors:
        reset_slot_anim();
        settings_.aura_slots = default_settings().aura_slots;
        flash_reset(true);
        emit();
        break;
      case Hit::WidthTrack:
        dragging_width_ = true;
        set_width_from_x(x);
        break;
      case Hit::WidthValue:
        width_editing_ = true;
        width_edit_buf_.clear();
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
      case Hit::ResetWidth:
        settings_.gradient_width = kDefaultGradientWidth;
        flash_reset(false);
        emit();
        break;
      case Hit::ModeAlways:
        settings_.display_mode = kDisplayModeAlways;
        settings_.show_on_hover = false;
        hover_reveal_t_ = 0.f;
        emit();
        break;
      case Hit::ModeFocus:
        if (settings_.display_mode != kDisplayModeOnFocus) start_hover_reveal();
        settings_.display_mode = kDisplayModeOnFocus;
        emit();
        break;
      case Hit::AuraToggle: {
        const bool next = !settings_.aura_enabled;
        start_toggle_anim(ToggleId::Aura, next);
        settings_.aura_enabled = next;
        emit();
        break;
      }
      case Hit::Hover:
        settings_.show_on_hover = !settings_.show_on_hover;
        emit();
        break;
      case Hit::FontSmall:
        if (settings_.ui_font_size != kFontSizeSmall) {
          start_segment_anim(selected_font_rect(), font_small_);
          settings_.ui_font_size = kFontSizeSmall;
        }
        emit();
        break;
      case Hit::FontMedium:
        if (settings_.ui_font_size != kFontSizeMedium) {
          start_segment_anim(selected_font_rect(), font_medium_);
          settings_.ui_font_size = kFontSizeMedium;
        }
        emit();
        break;
      case Hit::FontLarge:
        if (settings_.ui_font_size != kFontSizeLarge) {
          start_segment_anim(selected_font_rect(), font_large_);
          settings_.ui_font_size = kFontSizeLarge;
        }
        emit();
        break;
      case Hit::About:
        win_show_about_dialog(hwnd_);
        break;
      case Hit::EasyQuitToggle: {
        const bool next = !settings_.easy_quit;
        start_toggle_anim(ToggleId::EasyQuit, next);
        settings_.easy_quit = next;
        emit();
        break;
      }
      case Hit::Quit:
        if (settings_.easy_quit ||
            MessageBoxW(hwnd_, tr(lang(), StringId::kQuitConfirmBody), tr(lang(), StringId::kQuitConfirmTitle),
                        MB_YESNO | MB_ICONWARNING) == IDYES) {
          PostQuitMessage(0);
        }
        break;
      default: {
        const int hv = static_cast<int>(hover_);
        if (hv >= static_cast<int>(Hit::SlotLang0) && hv < static_cast<int>(Hit::SlotLang0) + kMaxAuraSlots) {
          pick_slot_language(static_cast<size_t>(hv - static_cast<int>(Hit::SlotLang0)));
        } else if (hv >= static_cast<int>(Hit::SlotSwatch0) &&
                   hv < static_cast<int>(Hit::SlotSwatch0) + kMaxAuraSlots) {
          const size_t i = static_cast<size_t>(hv - static_cast<int>(Hit::SlotSwatch0));
          if (i < settings_.aura_slots.size() && pick_color(settings_.aura_slots[i].color)) emit();
        } else if (hv >= static_cast<int>(Hit::SlotRemove0) &&
                   hv < static_cast<int>(Hit::SlotRemove0) + kMaxAuraSlots) {
          const size_t i = static_cast<size_t>(hv - static_cast<int>(Hit::SlotRemove0));
          if (slot_anim_active()) break;
          if (i < settings_.aura_slots.size() &&
              settings_.aura_slots.size() > static_cast<size_t>(kMinAuraSlots)) {
            capture_slot_anim_from();
            slot_exit_.slot = settings_.aura_slots[i];
            slot_exit_.from = {slot_lang_[i], slot_swatch_[i], slot_remove_[i]};
            slot_exit_.active = true;
            slot_anim_kind_ = SlotAnimKind::Remove;
            slot_anim_index_ = i;
            slot_anim_pending_ = true;
            settings_.aura_slots.erase(settings_.aura_slots.begin() + static_cast<std::ptrdiff_t>(i));
            emit();
          }
        } else if (hv >= static_cast<int>(Hit::LangPick0) &&
                   hv < static_cast<int>(Hit::LangPick0) + 16) {
          size_t n = 0;
          const auto* cat = ui_language_catalog(n);
          const size_t i = static_cast<size_t>(hv - static_cast<int>(Hit::LangPick0));
          if (i < n) {
            settings_.language = cat[i].id;
            emit();
          }
        } else if (hv >= static_cast<int>(Hit::BusyPick0) &&
                   hv < static_cast<int>(Hit::BusyPick0) + 16) {
          const auto& catalog = busy_action_catalog();
          const size_t i = static_cast<size_t>(hv - static_cast<int>(Hit::BusyPick0));
          if (i < catalog.size() && busy_action_selectable(catalog[i])) {
            settings_.firefly_busy_action = std::string(catalog[i]);
            settings_ = normalize_settings(settings_);
            if (catalog[i] == kFireflyBusyCustomKey) firefly_key_capture_ = true;
            InvalidateRect(hwnd_, nullptr, FALSE);
          }
        }
        break;
      }
    }
  }

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND previous_fg_ = nullptr;
  Settings settings_{};
  std::string prev_display_mode_;
  std::function<void(const Settings&)> callback_;
  ComPtr<ID2D1Factory> d2d_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<ID2D1HwndRenderTarget> rt_;
  ComPtr<ID2D1StrokeStyle> tab_icon_round_stroke_;
  int dpi_ = 96;
  int tab_bar_h_cached_ = 0;
  int content_height_ = 0;
  int client_height_ = 0;
  int scroll_y_ = 0;
  int scroll_max_ = 0;
  Hit hover_ = Hit::None;
  bool dragging_width_ = false;
  bool dragging_scroll_ = false;
  bool entrance_played_ = false;
  int entrance_alpha_ = 255;
  float hover_reveal_t_ = 1.f;
  std::chrono::steady_clock::time_point hover_reveal_start_{};
  bool reset_colors_flash_ = false;
  bool reset_width_flash_ = false;
  bool width_editing_ = false;
  std::wstring width_edit_buf_;
  RECT aura_title_{}, aura_sub_{}, aura_toggle_{};
  RECT sec_color_title_{}, sec_color_sub_{}, reset_colors_{};
  std::vector<RECT> slot_lang_, slot_swatch_, slot_remove_;
  RECT add_slot_{};
  RECT rule1_{}, sec_width_title_{}, sec_width_sub_{}, width_track_{}, width_value_{}, reset_width_{};
  RECT rule2_{}, sec_disp_title_{}, mode_always_{}, mode_focus_{}, hover_box_{};
  RECT rule3_{}, sec_font_title_{}, sec_font_sub_{}, font_bar_{}, font_small_{}, font_medium_{}, font_large_{};
  RECT rule4_{}, about_{}, quit_{}, easy_quit_{}, scroll_bar_{};

  Tab active_tab_ = Tab::Aura;
  Page page_ = Page::Main;
  RECT tab_aura_{}, tab_firefly_{}, tab_general_{};
  int tab_scroll_[3]{0, 0, 0};
  int tab_content_h_[3]{0, 0, 0};
  RECT lang_change_{}, lang_rule_{}, lang_picker_title_{}, lang_back_{};
  std::vector<RECT> lang_pick_rows_;
  RECT ff_title_{}, ff_sub_{}, ff_toggle_{}, ff_busy_title_{}, ff_busy_change_{}, ff_keep_display_{}, ff_custom_key_{},
      ff_caps_title_{}, ff_caps_preserve_{}, ff_caps_upper_{}, ff_caps_lower_{};
  RECT ff_status_{}, ff_caps_ok_{}, ff_led_ok_{}, ff_dnd_ok_{}, ff_keep_awake_ok_{}, ff_voice_ok_{}, ff_mic_ok_{},
      ff_speaker_ok_{}, ff_custom_key_ok_{};
  RECT ff_busy_back_{}, ff_busy_picker_title_{}, ff_custom_key_picker_{};
  std::vector<RECT> ff_busy_pick_rows_;
  FireflyCapabilities firefly_caps_{};
  bool firefly_active_ = false;
  bool firefly_key_capture_ = false;
  bool firefly_caps_ok_ = true;
  bool firefly_led_ok_ = true;
  bool firefly_dnd_ok_ = true;
  bool firefly_keep_awake_ok_ = true;
  bool firefly_voice_ok_ = true;
  bool firefly_mic_ok_ = true;
  bool firefly_speaker_ok_ = true;
  bool firefly_custom_key_ok_ = true;
  bool save_pending_ = false;

 public:
  void set_firefly_active(bool active) {
    if (firefly_active_ == active) return;
    firefly_active_ = active;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void set_firefly_capabilities(const FireflyCapabilities& caps) {
    firefly_caps_ = caps;
    firefly_caps_ok_ = caps.can_intercept_caps;
    firefly_led_ok_ = caps.can_drive_led;
    firefly_dnd_ok_ = caps.can_set_dnd;
    firefly_keep_awake_ok_ = caps.can_keep_awake;
    firefly_voice_ok_ = caps.can_trigger_voice_input;
    firefly_mic_ok_ = caps.can_mute_mic;
    firefly_speaker_ok_ = caps.can_mute_speaker;
    firefly_custom_key_ok_ = caps.can_trigger_custom_key;
    if (!busy_action_selectable(settings_.firefly_busy_action)) {
      settings_.firefly_busy_action = kFireflyBusyDnd;
      settings_.firefly_keep_display_on = false;
      settings_ = normalize_settings(settings_);
      if (callback_) callback_(settings_);
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
  }

 private:
  float tab_anim_t_ = 1.f;
  float tab_from_left_ = 0.f;
  float tab_from_right_ = 0.f;
  float tab_to_left_ = 0.f;
  float tab_to_right_ = 0.f;
  std::chrono::steady_clock::time_point tab_anim_start_{};
  float seg_anim_t_ = 1.f;
  float seg_from_left_ = 0.f;
  float seg_from_right_ = 0.f;
  float seg_to_left_ = 0.f;
  float seg_to_right_ = 0.f;
  std::chrono::steady_clock::time_point seg_anim_start_{};
  SlotAnimKind slot_anim_kind_ = SlotAnimKind::None;
  bool slot_anim_pending_ = false;
  bool slot_anim_is_add_ = false;
  size_t slot_anim_index_ = 0;
  int slot_anim_row_h_ = 0;
  std::vector<SlotRowRects> slot_anim_from_;
  std::vector<SlotRowRects> slot_anim_to_;
  RECT add_slot_anim_from_{};
  RECT add_slot_anim_to_{};
  SlotExitGhost slot_exit_{};
  float slot_anim_t_ = 1.f;
  std::chrono::steady_clock::time_point slot_anim_start_{};
  float toggle_v_[static_cast<int>(ToggleId::Count)] = {0.f, 0.f, 0.f};
  float toggle_from_[static_cast<int>(ToggleId::Count)] = {0.f, 0.f, 0.f};
  float toggle_to_[static_cast<int>(ToggleId::Count)] = {0.f, 0.f, 0.f};
  bool toggle_anim_active_[static_cast<int>(ToggleId::Count)] = {false, false, false};
  std::chrono::steady_clock::time_point toggle_anim_start_[static_cast<int>(ToggleId::Count)]{};

  Lang lang() const { return lang_from_key(settings_.language); }

  void emit_debounced() {
    settings_ = normalize_settings(settings_);
    if (callback_) callback_(settings_);
    if (!save_pending_) {
      save_pending_ = true;
      SetTimer(hwnd_, kSaveTimerId, kSaveDebounceMs, nullptr);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void flush_save() {
    if (save_pending_) {
      save_pending_ = false;
      KillTimer(hwnd_, kSaveTimerId);
    }
  }
};

SettingsUi g_ui;

}  // namespace

namespace win_settings {

bool create(HINSTANCE instance, Settings initial, std::function<void(const Settings&)> cb) {
  return g_ui.create(instance, std::move(initial), std::move(cb));
}
void destroy() { g_ui.destroy(); }
void show() { g_ui.show(); }
void hide() { g_ui.hide(); }
bool visible() { return g_ui.visible(); }
HWND hwnd() { return g_ui.hwnd(); }
void sync(const Settings& settings) { g_ui.sync(settings); }
void set_firefly_active(bool active) { g_ui.set_firefly_active(active); }
void set_firefly_capabilities(const FireflyCapabilities& caps) { g_ui.set_firefly_capabilities(caps); }

}  // namespace win_settings
}  // namespace imeaura
