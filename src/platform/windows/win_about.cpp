#include "platform/windows/win_about.h"

#include "core/tokens.h"
#include "platform/windows/win_assets.h"
#include "platform/windows/win_icon.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace imeaura {
namespace {

constexpr wchar_t kAboutClass[] = L"IMEAuraAbout";
constexpr int kScrollBarWidth = 12;

D2D1_COLOR_F UiColor(const Rgba& c, float scale = 1.f) {
  return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, (c.a / 255.f) * scale);
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

    license_text_ = win_read_asset_utf8("LICENSE");
    notices_text_ = win_read_asset_utf8("THIRD_PARTY_NOTICES.md");
    body_text_ = L"--- LICENSE ---\n\n" + Utf8ToWide(license_text_) + L"\n\n--- THIRD_PARTY_NOTICES ---\n\n" +
                 Utf8ToWide(notices_text_);
    if (body_text_.empty()) body_text_ = L"(LICENSE / THIRD_PARTY_NOTICES を読み込めませんでした)";

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
    const int w = 480;
    const int h = 520;
    const int x = owner_rc.left + ((owner_rc.right - owner_rc.left) - w) / 2;
    const int y = owner_rc.top + ((owner_rc.bottom - owner_rc.top) - h) / 2;

    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kAboutClass, L"IME Aura について",
                            WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, owner, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) return;

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
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
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
      case WM_DPICHANGED:
        release_target();
        layout();
        measure_body();
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
      case WM_LBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (contains(scroll_bar_, pt.x, pt.y) && scroll_max_ > 0) {
          dragging_scroll_ = true;
          SetCapture(hwnd_);
        } else if (contains(close_, pt.x, pt.y)) {
          DestroyWindow(hwnd_);
        }
        return 0;
      }
      case WM_MOUSEMOVE:
        if (dragging_scroll_ && scroll_max_ > 0) {
          const int track_h = scroll_bar_.bottom - scroll_bar_.top;
          const int thumb_h = std::max(dip(24), track_h * client_h_ / std::max(content_h_, 1));
          const int usable = std::max(1, track_h - thumb_h);
          scroll_y_ = (GET_Y_LPARAM(lp) - scroll_bar_.top - thumb_h / 2) * scroll_max_ / usable;
          scroll_y_ = std::clamp(scroll_y_, 0, scroll_max_);
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
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

  int dip(int v) const { return MulDiv(v, dpi_, 96); }

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

  RECT box(int x, int y, int w, int h) const { return RECT{x, y, x + w, y + h}; }

  bool contains(const RECT& r, int x, int y) const {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
  }

  void layout() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    client_w_ = rc.right;
    client_h_ = rc.bottom;
    const int m = dip(24);
    header_ = box(m, m, rc.right - m * 2, dip(120));
    body_ = box(m, header_.bottom + dip(8), rc.right - m - dip(kScrollBarWidth + 8), rc.bottom - dip(56));
    scroll_bar_ = box(rc.right - m - dip(kScrollBarWidth), body_.top, dip(kScrollBarWidth), body_.bottom - body_.top);
    close_ = box(m, rc.bottom - dip(44), rc.right - m * 2, dip(32));
  }

  void measure_body() {
    if (!dwrite_) return;
    ComPtr<IDWriteTextFormat> fmt;
    dwrite_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(dip(11)), L"en-us", fmt.GetAddressOf());
    if (!fmt) {
      dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(dip(11)), L"en-us", fmt.GetAddressOf());
    }
    ComPtr<IDWriteTextLayout> layout;
    dwrite_->CreateTextLayout(body_text_.c_str(), static_cast<UINT32>(body_text_.size()), fmt.Get(),
                              static_cast<FLOAT>(body_.right - body_.left), 100000.f, layout.GetAddressOf());
    DWRITE_TEXT_METRICS metrics{};
    if (layout) layout->GetMetrics(&metrics);
    content_h_ = static_cast<int>(metrics.height) + dip(8);
    scroll_max_ = std::max(0, content_h_ - static_cast<int>(body_.bottom - body_.top));
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

  void draw_text(IDWriteTextFormat* fmt, const wchar_t* text, const D2D1_RECT_F& box, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    rt_->DrawTextW(text, static_cast<UINT32>(wcslen(text)), fmt, box, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP,
                   DWRITE_MEASURING_MODE_NATURAL);
  }

  void paint() {
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);
    if (!ensure_target()) {
      EndPaint(hwnd_, &ps);
      return;
    }
    layout();

    rt_->BeginDraw();
    rt_->Clear(UiColor(kUiBg));

    ComPtr<IDWriteTextFormat> title_fmt;
    dwrite_->CreateTextFormat(L"Yu Gothic UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(dip(18)), L"ja-jp", title_fmt.GetAddressOf());
    ComPtr<IDWriteTextFormat> meta_fmt;
    dwrite_->CreateTextFormat(L"Yu Gothic UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(dip(12)), L"ja-jp", meta_fmt.GetAddressOf());
    ComPtr<IDWriteTextFormat> body_fmt;
    dwrite_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(dip(11)), L"en-us", body_fmt.GetAddressOf());
    if (!body_fmt) {
      dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(dip(11)), L"en-us",
                                body_fmt.GetAddressOf());
    }

    draw_text(title_fmt.Get(), L"IME Aura", D2D1::RectF(static_cast<float>(header_.left + dip(64)),
                                                      static_cast<float>(header_.top),
                                                      static_cast<float>(header_.right),
                                                      static_cast<float>(header_.top + dip(28))),
              UiColor(kUiText));
    draw_text(meta_fmt.Get(), L"バージョン 1.0.0\nCopyright (c) 2026 Mac020k",
              D2D1::RectF(static_cast<float>(header_.left + dip(64)), static_cast<float>(header_.top + dip(30)),
                          static_cast<float>(header_.right), static_cast<float>(header_.top + dip(56))),
              UiColor(kUiTextSecondary));
    draw_text(meta_fmt.Get(),
              L"本ソフトウェアは MIT License のもとで提供されています。\n"
              L"Native C++ build (no Qt / no PySide6).",
              D2D1::RectF(static_cast<float>(header_.left + dip(64)), static_cast<float>(header_.top + dip(62)),
                          static_cast<float>(header_.right), static_cast<float>(header_.bottom)),
              UiColor(kUiText));

    ComPtr<ID2D1SolidColorBrush> sep;
    rt_->CreateSolidColorBrush(UiColor(kUiSeparator), sep.GetAddressOf());
    rt_->FillRectangle(D2D1::RectF(static_cast<float>(header_.left), static_cast<float>(header_.bottom),
                                   static_cast<float>(header_.right), static_cast<float>(header_.bottom + 1)),
                       sep.Get());

    rt_->PushAxisAlignedClip(r2f(body_), D2D1_ANTIALIAS_MODE_ALIASED);
    ComPtr<IDWriteTextLayout> layout;
    dwrite_->CreateTextLayout(body_text_.c_str(), static_cast<UINT32>(body_text_.size()), body_fmt.Get(),
                              static_cast<FLOAT>(body_.right - body_.left), 100000.f, layout.GetAddressOf());
    if (layout) {
      ComPtr<ID2D1SolidColorBrush> body_brush;
      rt_->CreateSolidColorBrush(UiColor(kUiText), body_brush.GetAddressOf());
      rt_->DrawTextLayout(D2D1::Point2F(static_cast<FLOAT>(body_.left),
                                        static_cast<FLOAT>(body_.top - scroll_y_)),
                          layout.Get(), body_brush.Get());
    }
    rt_->PopAxisAlignedClip();

    if (scroll_max_ > 0) {
      ComPtr<ID2D1SolidColorBrush> track;
      rt_->CreateSolidColorBrush(UiColor(kUiFill), track.GetAddressOf());
      rt_->FillRoundedRectangle(D2D1::RoundedRect(r2f(scroll_bar_), 4.f, 4.f), track.Get());
      const int track_h = scroll_bar_.bottom - scroll_bar_.top;
      const int body_h = static_cast<int>(body_.bottom - body_.top);
      const int thumb_h = std::max(dip(24), track_h * body_h / std::max(content_h_, 1));
      const int thumb_y =
          scroll_bar_.top + (track_h - thumb_h) * scroll_y_ / std::max(scroll_max_, 1);
      ComPtr<ID2D1SolidColorBrush> thumb;
      rt_->CreateSolidColorBrush(UiColor(kUiTextSecondary, 0.55f), thumb.GetAddressOf());
      rt_->FillRoundedRectangle(
          D2D1::RoundedRect(D2D1::RectF(static_cast<float>(scroll_bar_.left + 2), static_cast<float>(thumb_y),
                                        static_cast<float>(scroll_bar_.right - 2),
                                        static_cast<float>(thumb_y + thumb_h)),
                            4.f, 4.f),
          thumb.Get());
    }

    ComPtr<ID2D1SolidColorBrush> btn;
    rt_->CreateSolidColorBrush(UiColor(kUiFill), btn.GetAddressOf());
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r2f(close_), static_cast<float>(dip(8)), static_cast<float>(dip(8))),
                              btn.Get());
    meta_fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    meta_fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    draw_text(meta_fmt.Get(), L"閉じる", r2f(close_), UiColor(kUiText));

    const HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) release_target();
    EndPaint(hwnd_, &ps);
  }

  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  bool done_ = false;
  bool dragging_scroll_ = false;
  int dpi_ = 96;
  int client_w_ = 0;
  int client_h_ = 0;
  int content_h_ = 0;
  int scroll_y_ = 0;
  int scroll_max_ = 0;
  std::string license_text_;
  std::string notices_text_;
  std::wstring body_text_;
  ComPtr<ID2D1Factory> d2d_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<ID2D1HwndRenderTarget> rt_;
  RECT header_{}, body_{}, scroll_bar_{}, close_{};
};

}  // namespace

void win_show_about_dialog(HWND owner) {
  AboutDialogUi dlg;
  dlg.show_modal(owner);
}

}  // namespace imeaura
