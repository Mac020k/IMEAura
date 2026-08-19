#include "platform/windows/win_color_dialog.h"

#include "core/tokens.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

using Microsoft::WRL::ComPtr;

namespace imeaura {
namespace {

constexpr wchar_t kColorDialogClass[] = L"IMEAuraColorDialog";

enum class ColorHit : int {
  None = 0,
  SliderR,
  SliderG,
  SliderB,
  SliderA,
  Ok,
  Cancel,
};

D2D1_COLOR_F UiColor(const Rgba& c, float scale = 1.f) {
  return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, (c.a / 255.f) * scale);
}

class ColorDialogUi {
 public:
  bool show_modal(HWND owner, Rgba initial) {
    owner_ = owner;
    color_ = initial;
    accepted_ = false;
    done_ = false;

    static bool registered = false;
    if (!registered) {
      WNDCLASSEXW wc{};
      wc.cbSize = sizeof(wc);
      wc.lpfnWndProc = WndProc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
      wc.lpszClassName = kColorDialogClass;
      RegisterClassExW(&wc);
      registered = true;
    }

    RECT owner_rc{};
    GetWindowRect(owner, &owner_rc);
    const int w = 360;
    const int h = 300;
    const int x = owner_rc.left + ((owner_rc.right - owner_rc.left) - w) / 2;
    const int y = owner_rc.top + ((owner_rc.bottom - owner_rc.top) - h) / 2;

    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kColorDialogClass, L"色を選択",
                            WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, owner, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;

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
    return accepted_;
  }

  Rgba result() const { return color_; }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ColorDialogUi* self = reinterpret_cast<ColorDialogUi*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      self = reinterpret_cast<ColorDialogUi*>(cs->lpCreateParams);
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
        return 0;
      case WM_SIZE:
      case WM_DPICHANGED:
        release_target();
        layout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      case WM_ERASEBKGND:
        return 1;
      case WM_PAINT:
        paint();
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
        dragging_ = ColorHit::None;
        return 0;
      case WM_CLOSE:
        accepted_ = false;
        done_ = true;
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

  void layout() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int m = dip(16);
    const int row = dip(26);
    const int row_gap = dip(6);
    int y = m;
    preview_ = box(m, y, rc.right - m * 2, dip(44));
    y += dip(44) + dip(12);
    slider_r_ = box(m, y, rc.right - m * 2, row);
    y += row + row_gap;
    slider_g_ = box(m, y, rc.right - m * 2, row);
    y += row + row_gap;
    slider_b_ = box(m, y, rc.right - m * 2, row);
    y += row + row_gap;
    slider_a_ = box(m, y, rc.right - m * 2, row);
    y += row + dip(12);
    const int btn_w = (rc.right - m * 2 - dip(8)) / 2;
    ok_ = box(m, y, btn_w, row);
    cancel_ = box(m + btn_w + dip(8), y, btn_w, row);
  }

  bool contains(const RECT& r, int x, int y) const {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
  }

  ColorHit hit_test(int x, int y) const {
    if (contains(slider_r_, x, y)) return ColorHit::SliderR;
    if (contains(slider_g_, x, y)) return ColorHit::SliderG;
    if (contains(slider_b_, x, y)) return ColorHit::SliderB;
    if (contains(slider_a_, x, y)) return ColorHit::SliderA;
    if (contains(ok_, x, y)) return ColorHit::Ok;
    if (contains(cancel_, x, y)) return ColorHit::Cancel;
    return ColorHit::None;
  }

  void set_channel_from_x(ColorHit hit, int x) {
    const RECT* track = nullptr;
    uint8_t Rgba::* channel = nullptr;
    switch (hit) {
      case ColorHit::SliderR:
        track = &slider_r_;
        channel = &Rgba::r;
        break;
      case ColorHit::SliderG:
        track = &slider_g_;
        channel = &Rgba::g;
        break;
      case ColorHit::SliderB:
        track = &slider_b_;
        channel = &Rgba::b;
        break;
      case ColorHit::SliderA:
        track = &slider_a_;
        channel = &Rgba::a;
        break;
      default:
        return;
    }
    float t = (x - track->left) / static_cast<float>(track->right - track->left);
    t = std::clamp(t, 0.f, 1.f);
    color_.*channel = static_cast<uint8_t>(std::lround(t * 255.f));
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void on_mouse(int x, int y, bool down, bool click) {
    hover_ = hit_test(x, y);
    if (dragging_ != ColorHit::None && down) {
      set_channel_from_x(dragging_, x);
      return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (!click) return;
    switch (hover_) {
      case ColorHit::SliderR:
      case ColorHit::SliderG:
      case ColorHit::SliderB:
      case ColorHit::SliderA:
        dragging_ = hover_;
        set_channel_from_x(dragging_, x);
        break;
      case ColorHit::Ok:
        accepted_ = true;
        DestroyWindow(hwnd_);
        break;
      case ColorHit::Cancel:
        accepted_ = false;
        DestroyWindow(hwnd_);
        break;
      default:
        break;
    }
  }

  ComPtr<IDWriteTextFormat> make_slider_text_format(DWRITE_TEXT_ALIGNMENT align) {
    ComPtr<IDWriteTextFormat> fmt;
    dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(dip(12)), L"en-us",
                              fmt.GetAddressOf());
    fmt->SetTextAlignment(align);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    return fmt;
  }

  void paint_slider(const RECT& rc, const wchar_t* label, uint8_t value, const D2D1_COLOR_F& accent) {
    const auto label_fmt = make_slider_text_format(DWRITE_TEXT_ALIGNMENT_CENTER);
    const auto value_fmt = make_slider_text_format(DWRITE_TEXT_ALIGNMENT_TRAILING);
    const D2D1_RECT_F row_box = r2f(rc);

    draw_text(label_fmt.Get(), label, D2D1::RectF(row_box.left, row_box.top, row_box.left + dip(18), row_box.bottom),
              UiColor(kUiTextSecondary));

    const float track_left = row_box.left + dip(22);
    const float track_right = row_box.right - dip(40);
    const float mid_y = (row_box.top + row_box.bottom) * 0.5f;
    const float track_h = static_cast<float>(dip(4));
    const float knob_r = static_cast<float>(dip(6));

    ComPtr<ID2D1SolidColorBrush> back;
    rt_->CreateSolidColorBrush(UiColor(kUiFill), back.GetAddressOf());
    rt_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(track_left, mid_y - track_h * 0.5f, track_right, mid_y + track_h * 0.5f), 2, 2),
        back.Get());

    const float t = value / 255.f;
    const float knob_x = track_left + t * (track_right - track_left);
    ComPtr<ID2D1SolidColorBrush> acc;
    rt_->CreateSolidColorBrush(accent, acc.GetAddressOf());
    rt_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(track_left, mid_y - track_h * 0.5f, knob_x, mid_y + track_h * 0.5f), 2, 2),
        acc.Get());
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knob_x, mid_y), knob_r, knob_r), acc.Get());

    wchar_t buf[16];
    swprintf_s(buf, L"%u", static_cast<unsigned>(value));
    draw_text(value_fmt.Get(), buf,
              D2D1::RectF(track_right + dip(6), row_box.top, row_box.right, row_box.bottom), UiColor(kUiText));
  }

  void draw_text(IDWriteTextFormat* fmt, const wchar_t* text, const D2D1_RECT_F& box, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    rt_->DrawTextW(text, static_cast<UINT32>(wcslen(text)), fmt, box, brush.Get());
  }

  static D2D1_RECT_F r2f(const RECT& r) {
    return D2D1::RectF(static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
                       static_cast<float>(r.bottom));
  }

  void paint_button(const RECT& rc, const wchar_t* label, bool primary) {
    ComPtr<ID2D1SolidColorBrush> fill;
    rt_->CreateSolidColorBrush(primary ? UiColor(kDefaultColorEn) : UiColor(kUiFill), fill.GetAddressOf());
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r2f(rc), static_cast<float>(dip(8)), static_cast<float>(dip(8))),
                              fill.Get());
    ComPtr<IDWriteTextFormat> fmt;
    dwrite_->CreateTextFormat(L"Yu Gothic UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(dip(13)), L"ja-jp",
                              fmt.GetAddressOf());
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    draw_text(fmt.Get(), label, r2f(rc), primary ? D2D1::ColorF(1, 1, 1, 1) : UiColor(kUiText));
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

    ComPtr<ID2D1SolidColorBrush> preview_brush;
    rt_->CreateSolidColorBrush(UiColor(color_), preview_brush.GetAddressOf());
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r2f(preview_), static_cast<float>(dip(10)), static_cast<float>(dip(10))),
                              preview_brush.Get());

    paint_slider(slider_r_, L"R", color_.r, D2D1::ColorF(0.96f, 0.26f, 0.21f, 1.f));
    paint_slider(slider_g_, L"G", color_.g, D2D1::ColorF(0.13f, 0.77f, 0.37f, 1.f));
    paint_slider(slider_b_, L"B", color_.b, D2D1::ColorF(0.18f, 0.51f, 0.99f, 1.f));
    paint_slider(slider_a_, L"A", color_.a, UiColor(kUiTextSecondary));

    paint_button(ok_, L"OK", true);
    paint_button(cancel_, L"キャンセル", false);

    const HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) release_target();
    EndPaint(hwnd_, &ps);
  }

  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  Rgba color_{};
  bool accepted_ = false;
  bool done_ = false;
  int dpi_ = 96;
  ColorHit hover_ = ColorHit::None;
  ColorHit dragging_ = ColorHit::None;
  ComPtr<ID2D1Factory> d2d_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<ID2D1HwndRenderTarget> rt_;
  RECT preview_{}, slider_r_{}, slider_g_{}, slider_b_{}, slider_a_{}, ok_{}, cancel_{};
};

}  // namespace

bool win_show_color_dialog(HWND owner, Rgba initial, Rgba& out) {
  ColorDialogUi dlg;
  if (!dlg.show_modal(owner, initial)) return false;
  out = dlg.result();
  return true;
}

}  // namespace imeaura
