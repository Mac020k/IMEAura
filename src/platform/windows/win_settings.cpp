#include "platform/windows/win_settings_api.h"

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

enum class Hit : int {
  None = 0,
  JpSwatch,
  EnSwatch,
  ResetColors,
  WidthTrack,
  WidthValue,
  ResetWidth,
  ModeAlways,
  ModeFocus,
  ModeHidden,
  Hover,
  FontSmall,
  FontMedium,
  FontLarge,
  About,
  Quit,
  ScrollBar,
};

enum class AlignH { Left, Center };
enum class AlignV { Top, Center };

struct UiMetrics {
  int title_h = 22;
  int body_h = 18;
  int sub_h = 16;
  int row_h = 32;
  int value_w = 64;
  int font_bar_h = 32;
  int swatch_w = 104;
  int swatch_h = 32;
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

float ease_out_cubic(float t) {
  const float inv = 1.f - t;
  return 1.f - inv * inv * inv;
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

    const int w = MulDiv(kUiMinWindowW, 96, 96);
    const int h = MulDiv(640, 96, 96);
    hwnd_ = CreateWindowExW(0, kSettingsClass, L"IME Aura",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, w,
                            h, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    SetWindowLongPtrW(hwnd_, GWL_STYLE, style & ~WS_MAXIMIZEBOX);
    DWORD corner = kDwmCornerRound;
    DwmSetWindowAttribute(hwnd_, kDwmWindowCornerPreference, &corner, sizeof(corner));
    win_set_window_icons(hwnd_);
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
    settings_ = normalize_settings(s);
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
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
      case WM_DPICHANGED:
        release_target();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
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
        return 0;
      case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
          POINT pt{};
          GetCursorPos(&pt);
          ScreenToClient(hwnd_, &pt);
          const Hit h = hit_test(pt.x, pt.y);
          SetCursor(LoadCursorW(nullptr, h == Hit::None ? IDC_ARROW : IDC_HAND));
          return TRUE;
        }
        break;
      case WM_CLOSE:
        hide();
        return 0;
      case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
      default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
  }

  int dip(int v) const { return MulDiv(v, dpi_, 96); }

  int content_width() const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int gutter = scroll_max_ > 0 ? dip(kUiScrollBarGutter) : 0;
    return std::max(0, static_cast<int>(rc.right) - dip(kUiMargin) * 2 - gutter);
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
    return SUCCEEDED(d2d_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(static_cast<UINT>(rc.right), static_cast<UINT>(rc.bottom)),
                                         D2D1_PRESENT_OPTIONS_NONE),
        rt_.GetAddressOf()));
  }

  void release_target() { rt_.Reset(); }

  ComPtr<IDWriteTextFormat> make_format(int pt, DWRITE_FONT_WEIGHT weight) {
    ComPtr<IDWriteTextFormat> fmt;
    const float px = static_cast<float>(MulDiv(pt, dpi_, 72));
    dwrite_->CreateTextFormat(L"Yu Gothic UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                              px, L"ja-jp", fmt.GetAddressOf());
    if (!fmt) {
      dwrite_->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                px, L"en-us", fmt.GetAddressOf());
    }
    return fmt;
  }

  DWRITE_TEXT_METRICS measure_text(IDWriteTextFormat* fmt, const wchar_t* text) const {
    DWRITE_TEXT_METRICS metrics{};
    if (!dwrite_ || !fmt || !text) return metrics;
    ComPtr<IDWriteTextLayout> layout;
    const UINT32 n = static_cast<UINT32>(wcslen(text));
    if (FAILED(dwrite_->CreateTextLayout(text, n, fmt, 16384.f, 16384.f, layout.GetAddressOf())) || !layout) {
      return metrics;
    }
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->GetMetrics(&metrics);
    return metrics;
  }

  int text_height(IDWriteTextFormat* fmt) const {
    return std::max(1, static_cast<int>(std::ceil(measure_text(fmt, L"Agあ").height)));
  }

  int text_width(IDWriteTextFormat* fmt, const wchar_t* text) const {
    return std::max(1, static_cast<int>(std::ceil(measure_text(fmt, text).widthIncludingTrailingWhitespace)));
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
    int seg_line = u.body_h;
    const char* keys[] = {kFontSizeSmall, kFontSizeMedium, kFontSizeLarge};
    for (const char* key : keys) {
      auto preview = make_format(ui_font_point_size(key), DWRITE_FONT_WEIGHT_NORMAL);
      seg_line = std::max(seg_line, text_height(preview.Get()));
    }
    u.font_bar_h = std::max(dip(kUiHitMin), seg_line + pad_y * 2) + dip(kUiSpace1) * 2;
    u.swatch_h = std::max(dip(kUiHitMin), u.body_h + pad_y * 2);
    u.swatch_w =
        std::max(1, static_cast<int>(std::lround(u.swatch_h * static_cast<double>(kUiSwatchW) / kUiSwatchH)));
    return u;
  }

  void draw_text(ID2D1RenderTarget* rt, IDWriteTextFormat* fmt, const wchar_t* text, const D2D1_RECT_F& box,
                 const D2D1_COLOR_F& color, AlignH align_h = AlignH::Left, AlignV align_v = AlignV::Top) {
    if (!rt || !fmt || !text || !dwrite_) return;
    const float w = std::max(0.f, box.right - box.left);
    const float h = std::max(0.f, box.bottom - box.top);
    if (w <= 0.f || h <= 0.f) return;
    ComPtr<IDWriteTextLayout> layout;
    const UINT32 n = static_cast<UINT32>(wcslen(text));
    if (FAILED(dwrite_->CreateTextLayout(text, n, fmt, w, 16384.f, layout.GetAddressOf())) || !layout) return;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    float x = box.left - metrics.left;
    float y = box.top - metrics.top;
    if (align_h == AlignH::Center) x += (w - metrics.width) * 0.5f;
    if (align_v == AlignV::Center) y += (h - metrics.height) * 0.5f;
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (!brush) return;
    rt->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
    rt->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush.Get());
    rt->PopAxisAlignedClip();
  }

  void fill_round(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(color, brush.GetAddressOf());
    rt->FillRoundedRectangle(RoundRect(r, radius), brush.Get());
  }

  void clamp_scroll() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    client_height_ = rc.bottom;
    scroll_max_ = std::max(0, content_height_ - client_height_);
    scroll_y_ = std::clamp(scroll_y_, 0, scroll_max_);
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
    RECT crc{};
    GetClientRect(hwnd_, &crc);
    const int body = ui_font_point_size(settings_.ui_font_size);
    auto title_fmt = make_format(body + 2, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto body_fmt = make_format(body, DWRITE_FONT_WEIGHT_NORMAL);
    auto sub_fmt = make_format(std::max(body - 2, 10), DWRITE_FONT_WEIGHT_NORMAL);
    const UiMetrics um = make_metrics(title_fmt.Get(), body_fmt.Get(), sub_fmt.Get());
    layout(um);
    clamp_scroll();
    layout(um);
    clamp_scroll();

    rt_->BeginDraw();
    const float w = static_cast<float>(crc.right);
    const float h = static_cast<float>(crc.bottom);
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

    rt_->PushAxisAlignedClip(D2D1::RectF(0, 0, w, h), D2D1_ANTIALIAS_MODE_ALIASED);
    rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, -static_cast<float>(scroll_y_)));

    draw_text(rt_.Get(), title_fmt.Get(), L"色", r2f(sec_color_title_), C(kUiText));
    draw_text(rt_.Get(), sub_fmt.Get(), L"クリックして画面縁の色を変更します", r2f(sec_color_sub_),
              C(kUiTextSecondary));
    draw_text(rt_.Get(), body_fmt.Get(), L"日本語", r2f(jp_label_), C(kUiText), AlignH::Left, AlignV::Center);
    draw_text(rt_.Get(), body_fmt.Get(), L"英語", r2f(en_label_), C(kUiText), AlignH::Left, AlignV::Center);
    paint_swatch(jp_swatch_, settings_.color_jp, hover_ == Hit::JpSwatch);
    paint_swatch(en_swatch_, settings_.color_en, hover_ == Hit::EnSwatch);
    draw_text(rt_.Get(), body_fmt.Get(), reset_colors_flash_ ? L"戻しました" : L"デフォルトの色に戻す",
              r2f(reset_colors_), hover_ == Hit::ResetColors ? C(kDefaultColorEn) : C(kUiTextSecondary), AlignH::Left,
              AlignV::Center);
    paint_rule(rule1_);

    draw_text(rt_.Get(), title_fmt.Get(), L"グラデーションの幅", r2f(sec_width_title_), C(kUiText));
    draw_text(rt_.Get(), sub_fmt.Get(), L"画面縁の帯の厚さ (1-100 px)", r2f(sec_width_sub_), C(kUiTextSecondary));
    paint_slider();
    wchar_t pxbuf[32];
    if (width_editing_) {
      swprintf_s(pxbuf, L"%s px", width_edit_buf_.empty() ? L"" : width_edit_buf_.c_str());
    } else {
      swprintf_s(pxbuf, L"%d px", settings_.gradient_width);
    }
    fill_round(rt_.Get(), r2f(width_value_), dip(8), width_editing_ ? C(kUiFillHover) : C(kUiFill));
    draw_text(rt_.Get(), body_fmt.Get(), pxbuf, r2f(width_value_), C(kUiText), AlignH::Center, AlignV::Center);
    draw_text(rt_.Get(), body_fmt.Get(), reset_width_flash_ ? L"戻しました" : L"デフォルトの幅に戻す", r2f(reset_width_),
              hover_ == Hit::ResetWidth ? C(kDefaultColorEn) : C(kUiTextSecondary), AlignH::Left, AlignV::Center);
    paint_rule(rule2_);

    draw_text(rt_.Get(), title_fmt.Get(), L"グラデーション表示", r2f(sec_disp_title_), C(kUiText));
    paint_radio(mode_always_, settings_.display_mode == kDisplayModeAlways, L"常に表示", body_fmt.Get());
    paint_radio(mode_focus_, settings_.display_mode == kDisplayModeOnFocus, L"テキスト入力時のみ", body_fmt.Get());
    if (settings_.display_mode == kDisplayModeOnFocus && hover_box_.bottom > hover_box_.top) {
      paint_check(hover_box_, settings_.show_on_hover, L"テキストボックスへホバー時も表示", body_fmt.Get());
    }
    paint_radio(mode_hidden_, settings_.display_mode == kDisplayModeHidden, L"非表示", body_fmt.Get());
    paint_rule(rule3_);

    draw_text(rt_.Get(), title_fmt.Get(), L"文字サイズ", r2f(sec_font_title_), C(kUiText));
    draw_text(rt_.Get(), sub_fmt.Get(), L"このウィンドウの文字の大きさ", r2f(sec_font_sub_), C(kUiTextSecondary));
    paint_segments();
    paint_rule(rule4_);

    fill_round(rt_.Get(), r2f(about_), dip(12), hover_ == Hit::About ? C(kUiFillHover) : C(kUiFill));
    draw_text(rt_.Get(), body_fmt.Get(), L"バージョン情報...", r2f(about_), C(kUiText), AlignH::Center, AlignV::Center);
    fill_round(rt_.Get(), r2f(quit_), dip(12),
               hover_ == Hit::Quit ? D2D1::ColorF(200 / 255.f, 50 / 255.f, 50 / 255.f, 38 / 255.f)
                                   : C(kUiDangerFill));
    draw_text(rt_.Get(), body_fmt.Get(), L"アプリケーションを終了", r2f(quit_), C(kUiDanger), AlignH::Center,
              AlignV::Center);

    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->PopAxisAlignedClip();

    if (scroll_max_ > 0) {
      const int sb_w = dip(kUiScrollBarWidth);
      const int sb_mr = dip(kUiScrollBarMarginRight);
      const int sb_my = dip(kUiScrollBarMarginY);
      scroll_bar_ = box(crc.right - sb_mr - sb_w, sb_my, sb_w, crc.bottom - sb_my * 2);
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

  void paint_swatch(const RECT& rc, const Rgba& color, bool hover) {
    const float rad = (rc.bottom - rc.top) * 0.5f;
    fill_round(rt_.Get(), r2f(rc), rad, C(color));
    if (hover) {
      ComPtr<ID2D1SolidColorBrush> edge;
      rt_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.45f), edge.GetAddressOf());
      rt_->DrawRoundedRectangle(RoundRect(r2f(rc), rad), edge.Get(), 1.5f);
    }
    ComPtr<ID2D1SolidColorBrush> chev;
    rt_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), chev.GetAddressOf());
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

  void paint_radio(const RECT& rc, bool on, const wchar_t* label, IDWriteTextFormat* fmt) {
    if (rc.bottom <= rc.top) return;
    const float r = static_cast<float>(std::max(1, dip(7)));
    const float cy = (rc.top + rc.bottom) * 0.5f;
    const float cx = static_cast<float>(rc.left) + r + static_cast<float>(dip(4));
    ComPtr<ID2D1SolidColorBrush> ring;
    rt_->CreateSolidColorBrush(on ? C(kDefaultColorEn) : C(kUiTextSecondary), ring.GetAddressOf());
    rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), ring.Get(), 1.5f);
    if (on) rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 4.f / 7.f, r * 4.f / 7.f), ring.Get());
    D2D1_RECT_F text = r2f(rc);
    text.left = cx + r + static_cast<float>(dip(kUiRowGap));
    draw_text(rt_.Get(), fmt, label, text, C(kUiText), AlignH::Left, AlignV::Center);
  }

  void paint_check(const RECT& rc, bool on, const wchar_t* label, IDWriteTextFormat* fmt) {
    const float s = static_cast<float>(std::max(1, dip(14)));
    const float x = static_cast<float>(rc.left + dip(4));
    const float y = (rc.top + rc.bottom) * 0.5f - s * 0.5f;
    fill_round(rt_.Get(), D2D1::RectF(x, y, x + s, y + s), 3.f, on ? C(kDefaultColorEn) : C(kUiFill));
    D2D1_RECT_F text = r2f(rc);
    text.left = x + s + static_cast<float>(dip(kUiRowGap));
    draw_text(rt_.Get(), fmt, label, text, C(kUiText), AlignH::Left, AlignV::Center);
  }

  void paint_segments() {
    fill_round(rt_.Get(), r2f(font_bar_), dip(10), C(kUiFill));
    const wchar_t* labels[3] = {L"小", L"中", L"大"};
    const char* keys[3] = {kFontSizeSmall, kFontSizeMedium, kFontSizeLarge};
    RECT parts[3] = {font_small_, font_medium_, font_large_};
    for (int i = 0; i < 3; ++i) {
      if (settings_.ui_font_size == keys[i]) {
        fill_round(rt_.Get(), r2f(parts[i]), dip(8), D2D1::ColorF(1, 1, 1, 0.92f));
      }
      auto fmt = make_format(ui_font_point_size(keys[i]), DWRITE_FONT_WEIGHT_NORMAL);
      draw_text(rt_.Get(), fmt.Get(), labels[i], r2f(parts[i]), C(kUiText), AlignH::Center, AlignV::Center);
    }
  }

  static D2D1_RECT_F r2f(const RECT& r) {
    return D2D1::RectF(static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
                       static_cast<float>(r.bottom));
  }

  RECT box(int x, int y, int w, int h) const { return RECT{x, y, x + w, y + h}; }

  void layout(const UiMetrics& um) {
    const int m = dip(kUiMargin);
    const int gap = dip(kUiRowGap);
    const int sec = dip(kUiSectionGap);
    const int row = um.row_h;
    const int inner = content_width();
    const int radio_gap = dip(kUiSpace1);
    int y = m;

    sec_color_title_ = box(m, y, inner, um.title_h);
    y += um.title_h;
    sec_color_sub_ = box(m, y, inner, um.sub_h);
    y += um.sub_h + gap;
    const int color_row = std::max(row, um.swatch_h);
    jp_label_ = box(m, y, std::max(1, inner - um.swatch_w - gap), color_row);
    jp_swatch_ = box(m + inner - um.swatch_w, y + (color_row - um.swatch_h) / 2, um.swatch_w, um.swatch_h);
    y += color_row + gap;
    en_label_ = box(m, y, std::max(1, inner - um.swatch_w - gap), color_row);
    en_swatch_ = box(m + inner - um.swatch_w, y + (color_row - um.swatch_h) / 2, um.swatch_w, um.swatch_h);
    y += color_row + gap;
    reset_colors_ = box(m, y, inner, row);
    y += row + sec;
    rule1_ = box(m, y, inner, 1);
    y += sec;
    sec_width_title_ = box(m, y, inner, um.title_h);
    y += um.title_h;
    sec_width_sub_ = box(m, y, inner, um.sub_h);
    y += um.sub_h + gap;
    const int slider_min = dip(80);
    int value_w = um.value_w;
    if (value_w + gap + slider_min > inner) value_w = std::max(dip(48), inner - gap - slider_min);
    width_value_ = box(m + inner - value_w, y, value_w, row);
    width_track_ = box(m, y, std::max(1, inner - value_w - gap), row);
    y += row + gap;
    reset_width_ = box(m, y, inner, row);
    y += row + sec;
    rule2_ = box(m, y, inner, 1);
    y += sec;
    sec_disp_title_ = box(m, y, inner, um.title_h);
    y += um.title_h + gap;
    mode_always_ = box(m, y, inner, row);
    y += row + radio_gap;
    mode_focus_ = box(m, y, inner, row);
    y += row;
    if (settings_.display_mode == kDisplayModeOnFocus) {
      const int hover_h = std::max(1, static_cast<int>(std::lround(row * hover_reveal_t_)));
      hover_box_ = box(m + dip(12), y, std::max(0, inner - dip(12)), hover_h);
      y += hover_h;
    } else {
      hover_box_ = box(m, y, 0, 0);
    }
    y += radio_gap;
    mode_hidden_ = box(m, y, inner, row);
    y += row + sec;
    rule3_ = box(m, y, inner, 1);
    y += sec;
    sec_font_title_ = box(m, y, inner, um.title_h);
    y += um.title_h;
    sec_font_sub_ = box(m, y, inner, um.sub_h);
    y += um.sub_h + gap;
    font_bar_ = box(m, y, inner, um.font_bar_h);
    const int inset = dip(kUiSpace1);
    const int inner_h = std::max(1, um.font_bar_h - inset * 2);
    const int seg = inner / 3;
    font_small_ = box(m + inset, y + inset, std::max(1, seg - inset - 1), inner_h);
    font_medium_ = box(m + seg + 1, y + inset, std::max(1, seg - 2), inner_h);
    font_large_ = box(m + seg * 2 + 1, y + inset, std::max(1, inner - seg * 2 - inset - 1), inner_h);
    y += um.font_bar_h + sec;
    rule4_ = box(m, y, inner, 1);
    y += sec;
    about_ = box(m, y, inner, row);
    y += row + gap;
    quit_ = box(m, y, inner, row);
    y += row + m;
    content_height_ = y;
  }

  bool contains(const RECT& r, int x, int y) const {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
  }

  Hit hit_test(int x, int y) const {
    const int cy = y + scroll_y_;
    if (scroll_max_ > 0 && contains(scroll_bar_, x, y)) return Hit::ScrollBar;
    if (contains(jp_swatch_, x, cy)) return Hit::JpSwatch;
    if (contains(en_swatch_, x, cy)) return Hit::EnSwatch;
    if (contains(reset_colors_, x, cy)) return Hit::ResetColors;
    if (contains(width_track_, x, cy)) return Hit::WidthTrack;
    if (contains(width_value_, x, cy)) return Hit::WidthValue;
    if (contains(reset_width_, x, cy)) return Hit::ResetWidth;
    if (contains(mode_always_, x, cy)) return Hit::ModeAlways;
    if (contains(mode_focus_, x, cy)) return Hit::ModeFocus;
    if (contains(mode_hidden_, x, cy)) return Hit::ModeHidden;
    if (settings_.display_mode == kDisplayModeOnFocus && hover_box_.bottom > hover_box_.top &&
        contains(hover_box_, x, cy))
      return Hit::Hover;
    if (contains(font_small_, x, cy)) return Hit::FontSmall;
    if (contains(font_medium_, x, cy)) return Hit::FontMedium;
    if (contains(font_large_, x, cy)) return Hit::FontLarge;
    if (contains(about_, x, cy)) return Hit::About;
    if (contains(quit_, x, cy)) return Hit::Quit;
    return Hit::None;
  }

  void emit() {
    settings_ = normalize_settings(settings_);
    if (callback_) callback_(settings_);
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

  void on_mouse(int x, int y, bool down, bool click) {
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
      case Hit::ScrollBar:
        dragging_scroll_ = true;
        break;
      case Hit::JpSwatch:
        if (pick_color(settings_.color_jp)) emit();
        break;
      case Hit::EnSwatch:
        if (pick_color(settings_.color_en)) emit();
        break;
      case Hit::ResetColors:
        settings_.color_jp = kDefaultColorJp;
        settings_.color_en = kDefaultColorEn;
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
      case Hit::ModeHidden:
        settings_.display_mode = kDisplayModeHidden;
        settings_.show_on_hover = false;
        hover_reveal_t_ = 0.f;
        emit();
        break;
      case Hit::Hover:
        settings_.show_on_hover = !settings_.show_on_hover;
        emit();
        break;
      case Hit::FontSmall:
        settings_.ui_font_size = kFontSizeSmall;
        emit();
        break;
      case Hit::FontMedium:
        settings_.ui_font_size = kFontSizeMedium;
        emit();
        break;
      case Hit::FontLarge:
        settings_.ui_font_size = kFontSizeLarge;
        emit();
        break;
      case Hit::About:
        win_show_about_dialog(hwnd_);
        break;
      case Hit::Quit:
        if (MessageBoxW(hwnd_, L"IME Aura を終了しますか？\n画面縁のグラデーション表示も消えます。", L"IME Aura",
                        MB_YESNO | MB_ICONWARNING) == IDYES) {
          PostQuitMessage(0);
        }
        break;
      default:
        break;
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
  int dpi_ = 96;
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
  RECT sec_color_title_{}, sec_color_sub_{}, jp_label_{}, jp_swatch_{}, en_label_{}, en_swatch_{}, reset_colors_{};
  RECT rule1_{}, sec_width_title_{}, sec_width_sub_{}, width_track_{}, width_value_{}, reset_width_{};
  RECT rule2_{}, sec_disp_title_{}, mode_always_{}, mode_focus_{}, hover_box_{}, mode_hidden_{};
  RECT rule3_{}, sec_font_title_{}, sec_font_sub_{}, font_bar_{}, font_small_{}, font_medium_{}, font_large_{};
  RECT rule4_{}, about_{}, quit_{}, scroll_bar_{};
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

}  // namespace win_settings
}  // namespace imeaura
