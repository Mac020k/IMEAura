#include "platform/windows/win_about.h"

#include "core/i18n.h"
#include "core/settings.h"
#include "core/tokens.h"
#include "platform/windows/win_assets.h"
#include "platform/windows/win_icon.h"

#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>

using Microsoft::WRL::ComPtr;

#ifndef IMEAURA_VERSION
#define IMEAURA_VERSION L"2.4.0"
#endif

namespace imeaura {
namespace {

constexpr wchar_t kAboutClass[] = L"IMEAuraAbout";
constexpr UINT kDwmWindowCornerPreference = 33;
constexpr UINT kDwmCornerRound = 2;
constexpr int kAboutMinClientH = 320;
constexpr DWORD kAboutWndStyle = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;

enum class AboutHit : int { None = 0, Close, ScrollBar };

D2D1_COLOR_F C(const Rgba& c, float scale = 1.f) {
  return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, (c.a / 255.f) * scale);
}

D2D1_ROUNDED_RECT RoundRect(const D2D1_RECT_F& r, float radius) {
  return D2D1::RoundedRect(r, radius, radius);
}

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  if (len <= 0) return {};
  std::wstring out(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len);
  return out;
}

class AboutDialogUi {
 public:
  void show_modal(HWND owner) {
    owner_ = owner;
    done_ = false;
    hover_ = AboutHit::None;

    Settings tmp_s;
    load_settings(tmp_s);
    lang_ = lang_from_key(tmp_s.language);

    license_text_ = win_read_asset_utf8("LICENSE");
    notices_text_ = win_read_asset_utf8("THIRD_PARTY_NOTICES.md");
    rebuild_body_text();

    static bool registered = false;
    if (!registered) {
      WNDCLASSEXW wc{};
      wc.cbSize = sizeof(wc);
      wc.lpfnWndProc = WndProc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
      wc.lpszClassName = kAboutClass;
      RegisterClassExW(&wc);
      registered = true;
    }

    RECT owner_rc{};
    GetWindowRect(owner, &owner_rc);
    const int sys_dpi = static_cast<int>(GetDpiForSystem());
    const int client_w = dip(kUiDefaultWindowW);
    const int client_h = 480;
    RECT frame_rc{0, 0, MulDiv(client_w, sys_dpi, 96), MulDiv(client_h, sys_dpi, 96)};
    AdjustWindowRectExForDpi(&frame_rc, kAboutWndStyle, FALSE, 0, static_cast<UINT>(sys_dpi));
    const int frame_w = frame_rc.right - frame_rc.left;
    const int frame_h = frame_rc.bottom - frame_rc.top;
    const int x = owner_rc.left + ((owner_rc.right - owner_rc.left) - frame_w) / 2;
    const int y = owner_rc.top + ((owner_rc.bottom - owner_rc.top) - frame_h) / 2;

    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kAboutClass, tr(lang_, StringId::kAboutDialogTitle), kAboutWndStyle,
                            x, y, frame_w, frame_h, owner, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return;

    DWORD corner = kDwmCornerRound;
    DwmSetWindowAttribute(hwnd_, kDwmWindowCornerPreference, &corner, sizeof(corner));
    win_set_window_icons(hwnd_);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);

    MSG msg{};
    while (!done_ && GetMessageW(&msg, nullptr, 0, 0)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
  }

 private:
  void rebuild_body_text() {
    body_text_.clear();
    if (license_text_.empty() && notices_text_.empty()) {
      body_text_ = tr(lang_, StringId::kAboutLoadError);
      return;
    }
    if (!license_text_.empty()) {
      body_text_ += tr(lang_, StringId::kAboutLicenseHeading);
      body_text_ += L"\n\n";
      body_text_ += Utf8ToWide(license_text_);
    }
    if (!notices_text_.empty()) {
      if (!body_text_.empty()) body_text_ += L"\n\n";
      body_text_ += tr(lang_, StringId::kAboutThirdPartyHeading);
      body_text_ += L"\n\n";
      body_text_ += Utf8ToWide(notices_text_);
    }
  }

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_GETMINMAXINFO) {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
      int min_dpi = static_cast<int>(GetDpiForWindow(hwnd));
      if (min_dpi == 0) min_dpi = static_cast<int>(GetDpiForSystem());
      RECT min_rc{0, 0, MulDiv(kUiMinWindowW, min_dpi, 96), MulDiv(kAboutMinClientH, min_dpi, 96)};
      AdjustWindowRectExForDpi(&min_rc, kAboutWndStyle, FALSE, 0, static_cast<UINT>(min_dpi));
      mmi->ptMinTrackSize.x = min_rc.right - min_rc.left;
      mmi->ptMinTrackSize.y = min_rc.bottom - min_rc.top;
      return 0;
    }
    AboutDialogUi* self = reinterpret_cast<AboutDialogUi*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      self = reinterpret_cast<AboutDialogUi*>(cs->lpCreateParams);
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
        layout();
        measure_body();
        return 0;
      case WM_SIZE:
        release_target();
        layout();
        measure_body();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      case WM_DPICHANGED: {
        dpi_ = static_cast<int>(HIWORD(wp));
        release_target();
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        measure_body();
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
      case WM_MOUSEMOVE: {
        const int x = MulDiv(GET_X_LPARAM(lp), 96, dpi_);
        const int y = MulDiv(GET_Y_LPARAM(lp), 96, dpi_);
        update_hover(x, y);
        if (dragging_scroll_ && scroll_max_ > 0) {
          const int track_h = scroll_bar_.bottom - scroll_bar_.top;
          const int thumb_h = std::max(dip(24), track_h * client_body_h_ / std::max(content_h_, 1));
          const int usable = std::max(1, track_h - thumb_h);
          scroll_y_ = (y - scroll_bar_.top - thumb_h / 2) * scroll_max_ / usable;
          scroll_y_ = std::clamp(scroll_y_, 0, scroll_max_);
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
      }
      case WM_LBUTTONDOWN: {
        const int x = MulDiv(GET_X_LPARAM(lp), 96, dpi_);
        const int y = MulDiv(GET_Y_LPARAM(lp), 96, dpi_);
        if (contains(scroll_bar_, x, y) && scroll_max_ > 0) {
          dragging_scroll_ = true;
          SetCapture(hwnd_);
        } else if (contains(close_, x, y)) {
          DestroyWindow(hwnd_);
        }
        return 0;
      }
      case WM_LBUTTONUP:
        if (GetCapture() == hwnd_) ReleaseCapture();
        dragging_scroll_ = false;
        return 0;
      case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;
      case WM_DESTROY:
        done_ = true;
        return 0;
      default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
  }

  void update_hover(int x, int y) {
    AboutHit next = AboutHit::None;
    if (contains(close_, x, y)) {
      next = AboutHit::Close;
    } else if (scroll_max_ > 0 && contains(scroll_bar_, x, y)) {
      next = AboutHit::ScrollBar;
    }
    if (next != hover_) {
      hover_ = next;
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }

  int dip(int v) const { return v; }

  int client_dip_w() const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    return MulDiv(rc.right, 96, dpi_);
  }
  int client_dip_h() const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    return MulDiv(rc.bottom, 96, dpi_);
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
    return SUCCEEDED(d2d_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi_f, dpi_f),
        D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(static_cast<UINT>(rc.right), static_cast<UINT>(rc.bottom)),
                                         D2D1_PRESENT_OPTIONS_NONE),
        rt_.GetAddressOf()));
  }

  void release_target() { rt_.Reset(); }

  ComPtr<IDWriteTextFormat> make_format(float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) {
    ComPtr<IDWriteTextFormat> fmt;
    dwrite_->CreateTextFormat(lang_font_family(lang_), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", fmt.GetAddressOf());
    return fmt;
  }

  RECT box(int x, int y, int w, int h) const { return RECT{x, y, x + w, y + h}; }

  bool contains(const RECT& r, int x, int y) const {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
  }

  void layout() {
    const int cw = client_dip_w();
    const int ch = client_dip_h();
    client_w_ = cw;
    client_h_ = ch;

    const int m = dip(kUiMargin);
    const int gap = dip(kUiRowGap);
    const int sec = dip(kUiSectionGap);
    const int row = dip(kUiHitMin);
    const int x0 = m;
    const int w0 = cw - m * 2;

    auto title_fmt = make_format(15.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto sub_fmt = make_format(13.f);
    ComPtr<IDWriteTextLayout> title_layout;
    ComPtr<IDWriteTextLayout> meta_layout;
    ComPtr<IDWriteTextLayout> note_layout;
    dwrite_->CreateTextLayout(tr(lang_, StringId::kSettingsTitle), static_cast<UINT32>(wcslen(tr(lang_, StringId::kSettingsTitle))),
                              title_fmt.Get(), static_cast<FLOAT>(w0), 1000.f, title_layout.GetAddressOf());
    wchar_t version_line[96];
    swprintf_s(version_line, tr(lang_, StringId::kAboutVersionFmt), IMEAURA_VERSION);
    std::wstring meta = version_line;
    meta += L"\n";
    meta += tr(lang_, StringId::kAboutCopyright);
    dwrite_->CreateTextLayout(meta.c_str(), static_cast<UINT32>(meta.size()), sub_fmt.Get(), static_cast<FLOAT>(w0),
                              1000.f, meta_layout.GetAddressOf());
    const wchar_t* note = tr(lang_, StringId::kAboutLicenseNote);
    dwrite_->CreateTextLayout(note, static_cast<UINT32>(wcslen(note)), sub_fmt.Get(), static_cast<FLOAT>(w0), 1000.f,
                              note_layout.GetAddressOf());

    DWRITE_TEXT_METRICS tm{};
    float title_h = 22.f;
    float meta_h = 36.f;
    float note_h = 18.f;
    if (title_layout) {
      title_layout->GetMetrics(&tm);
      title_h = tm.height;
    }
    if (meta_layout) {
      meta_layout->GetMetrics(&tm);
      meta_h = tm.height;
    }
    if (note_layout) {
      note_layout->GetMetrics(&tm);
      note_h = tm.height;
    }

    int y = m;
    title_ = box(x0, y, w0, static_cast<int>(title_h));
    y += static_cast<int>(title_h) + gap;
    meta_ = box(x0, y, w0, static_cast<int>(meta_h));
    y += static_cast<int>(meta_h) + gap;
    note_ = box(x0, y, w0, static_cast<int>(note_h));
    y += static_cast<int>(note_h) + sec;
    rule_ = box(x0, y, w0, 1);
    y += 1 + sec;

    close_ = box(x0, ch - m - row, w0, row);
    const int panel_bottom = close_.top - sec;
    const int panel_h = std::max(0, panel_bottom - y);
    license_panel_ = box(x0, y, w0, panel_h);

    const int pad = dip(12);
    const int sb_w = dip(kUiScrollBarWidth);
    const int sb_gap = dip(4);
    const int inner_w = std::max(0, w0 - pad * 2);
    const int inner_h = std::max(0, panel_h - pad * 2);
    const int body_w = std::max(0, inner_w - sb_w - sb_gap);
    body_ = box(license_panel_.left + pad, license_panel_.top + pad, body_w, inner_h);
    scroll_bar_ = box(body_.right + sb_gap, body_.top, sb_w, inner_h);
    client_body_h_ = std::max(0, static_cast<int>(body_.bottom - body_.top));
  }

  void measure_body() {
    if (!dwrite_) return;
    auto body_fmt = make_format(12.f);
    ComPtr<IDWriteTextLayout> layout;
    dwrite_->CreateTextLayout(body_text_.c_str(), static_cast<UINT32>(body_text_.size()), body_fmt.Get(),
                              static_cast<FLOAT>(std::max(1, static_cast<int>(body_.right - body_.left))), 100000.f,
                              layout.GetAddressOf());
    DWRITE_TEXT_METRICS metrics{};
    if (layout) layout->GetMetrics(&metrics);
    content_h_ = static_cast<int>(metrics.height) + dip(8);
    scroll_max_ = std::max(0, content_h_ - client_body_h_);
    scroll_y_ = std::clamp(scroll_y_, 0, scroll_max_);
  }

  void scroll_by(int delta) {
    if (scroll_max_ <= 0) return;
    scroll_y_ = std::clamp(scroll_y_ + delta, 0, scroll_max_);
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  static D2D1_RECT_F r2f(const RECT& r) {
    return D2D1::RectF(static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
                       static_cast<float>(r.bottom));
  }

  void fill_round(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (brush) rt_->FillRoundedRectangle(RoundRect(r, radius), brush.Get());
  }

  void draw_text(IDWriteTextFormat* fmt, const wchar_t* text, const D2D1_RECT_F& box, const D2D1_COLOR_F& color,
                 DWRITE_TEXT_ALIGNMENT align_h = DWRITE_TEXT_ALIGNMENT_LEADING,
                 DWRITE_PARAGRAPH_ALIGNMENT align_v = DWRITE_PARAGRAPH_ALIGNMENT_NEAR, bool wrap = true) {
    if (!text || !fmt) return;
    const float w = std::max(0.f, box.right - box.left);
    const float h = std::max(0.f, box.bottom - box.top);
    if (w <= 0.f || h <= 0.f) return;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text, static_cast<UINT32>(wcslen(text)), fmt, w, h, layout.GetAddressOf())) ||
        !layout) {
      return;
    }
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetTextAlignment(align_h);
    layout->SetParagraphAlignment(align_v);
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (!brush) return;
    rt_->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
    rt_->DrawTextLayout(D2D1::Point2F(box.left, box.top), layout.Get(), brush.Get());
    rt_->PopAxisAlignedClip();
  }

  void paint_wash(float w, float h) {
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
  }

  void paint_rule(const RECT& rc) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(C(kUiSeparator), brush.GetAddressOf());
    if (brush) rt_->FillRectangle(r2f(rc), brush.Get());
  }

  void paint_button(const RECT& rc, const wchar_t* label, bool primary, bool hover) {
    D2D1_COLOR_F fill = primary ? C(kDefaultColorEn) : C(kUiFill);
    if (!primary && hover) fill = C(kUiFillHover);
    if (primary && hover) fill = D2D1::ColorF(fill.r * 0.92f, fill.g * 0.92f, fill.b * 0.92f, fill.a);
    fill_round(r2f(rc), static_cast<float>(dip(10)), fill);
    auto fmt = make_format(13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    draw_text(fmt.Get(), label, r2f(rc), primary ? D2D1::ColorF(1, 1, 1, 1) : C(kUiText),
              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
  }

  void paint_scroll_bar() {
    if (scroll_max_ <= 0) return;
    fill_round(r2f(scroll_bar_), 4.f, C(kUiFill));
    const int track_h = scroll_bar_.bottom - scroll_bar_.top;
    const int thumb_h = std::max(dip(24), track_h * client_body_h_ / std::max(content_h_, 1));
    const int thumb_y = scroll_bar_.top + (track_h - thumb_h) * scroll_y_ / std::max(scroll_max_, 1);
    ComPtr<ID2D1SolidColorBrush> thumb;
    rt_->CreateSolidColorBrush(C(kUiTextSecondary, 0.55f), thumb.GetAddressOf());
    if (!thumb) return;
    rt_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(static_cast<float>(scroll_bar_.left + 2), static_cast<float>(thumb_y),
                                      static_cast<float>(scroll_bar_.right - 2),
                                      static_cast<float>(thumb_y + thumb_h)),
                          4.f, 4.f),
        thumb.Get());
  }

  void paint() {
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);
    if (!ensure_target()) {
      EndPaint(hwnd_, &ps);
      return;
    }
    layout();
    measure_body();

    const float w = static_cast<float>(client_w_);
    const float h = static_cast<float>(client_h_);

    rt_->BeginDraw();
    rt_->Clear(C(kUiBg));
    paint_wash(w, h);

    auto title_fmt = make_format(15.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto sub_fmt = make_format(13.f);
    auto body_fmt = make_format(12.f);

    draw_text(title_fmt.Get(), tr(lang_, StringId::kSettingsTitle), r2f(title_), C(kUiText));
    wchar_t version_line[96];
    swprintf_s(version_line, tr(lang_, StringId::kAboutVersionFmt), IMEAURA_VERSION);
    std::wstring meta = version_line;
    meta += L"\n";
    meta += tr(lang_, StringId::kAboutCopyright);
    draw_text(sub_fmt.Get(), meta.c_str(), r2f(meta_), C(kUiTextSecondary));
    draw_text(sub_fmt.Get(), tr(lang_, StringId::kAboutLicenseNote), r2f(note_), C(kUiTextSecondary));
    paint_rule(rule_);

    fill_round(r2f(license_panel_), static_cast<float>(dip(10)), C(kUiFill));

    const D2D1_RECT_F clip = r2f(body_);
    rt_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    ComPtr<IDWriteTextLayout> layout;
    dwrite_->CreateTextLayout(body_text_.c_str(), static_cast<UINT32>(body_text_.size()), body_fmt.Get(),
                              static_cast<FLOAT>(std::max(1, static_cast<int>(body_.right - body_.left))), 100000.f,
                              layout.GetAddressOf());
    if (layout) {
      ComPtr<ID2D1SolidColorBrush> body_brush;
      rt_->CreateSolidColorBrush(C(kUiText), body_brush.GetAddressOf());
      if (body_brush) {
        rt_->DrawTextLayout(D2D1::Point2F(static_cast<FLOAT>(body_.left), static_cast<FLOAT>(body_.top - scroll_y_)),
                            layout.Get(), body_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      }
    }
    rt_->PopAxisAlignedClip();

    rt_->PushAxisAlignedClip(r2f(license_panel_), D2D1_ANTIALIAS_MODE_ALIASED);
    paint_scroll_bar();
    rt_->PopAxisAlignedClip();
    paint_button(close_, tr(lang_, StringId::kClose), true, hover_ == AboutHit::Close);

    const HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) release_target();
    EndPaint(hwnd_, &ps);
  }

  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  Lang lang_ = Lang::Ja;
  bool done_ = false;
  bool dragging_scroll_ = false;
  AboutHit hover_ = AboutHit::None;
  int dpi_ = 96;
  int client_w_ = 0;
  int client_h_ = 0;
  int client_body_h_ = 0;
  int content_h_ = 0;
  int scroll_y_ = 0;
  int scroll_max_ = 0;
  std::string license_text_;
  std::string notices_text_;
  std::wstring body_text_;
  ComPtr<ID2D1Factory> d2d_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<ID2D1HwndRenderTarget> rt_;
  RECT title_{}, meta_{}, note_{}, rule_{}, license_panel_{}, body_{}, scroll_bar_{}, close_{};
};

}  // namespace

void win_show_about_dialog(HWND owner) {
  AboutDialogUi dlg;
  dlg.show_modal(owner);
}

}  // namespace imeaura
