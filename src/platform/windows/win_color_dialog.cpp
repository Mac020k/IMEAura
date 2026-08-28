#include "platform/windows/win_color_dialog.h"

#include "core/i18n.h"
#include "core/settings.h"
#include "core/tokens.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

using Microsoft::WRL::ComPtr;

namespace imeaura {
namespace {

constexpr wchar_t kColorDialogClass[] = L"IMEAuraColorDialog";

constexpr Rgba kColorPresets[] = {
    kDefaultColorJp,
    kDefaultColorEn,
    kDefaultAuraSlotColors[0],
    kDefaultAuraSlotColors[1],
    kDefaultAuraSlotColors[2],
    kDefaultAuraSlotColors[3],
    kDefaultAuraSlotColors[4],
};
constexpr int kPresetCount = static_cast<int>(sizeof(kColorPresets) / sizeof(kColorPresets[0]));

enum class ColorMode : int { Rgb = 0, Hsb = 1 };

enum class EditField : int {
  None = 0,
  Hex,
  Ch0,
  Ch1,
  Ch2,
  Ch3,
};

enum class ColorHit : int {
  None = 0,
  Slider0,
  Slider1,
  Slider2,
  Slider3,
  Value0,
  Value1,
  Value2,
  Value3,
  Hex,
  ModeRgb,
  ModeHsb,
  Preset0,
  Preset1,
  Preset2,
  Preset3,
  Preset4,
  Preset5,
  Preset6,
  Ok,
  Cancel,
};

struct Hsb {
  float h = 0.f;  // 0..360
  float s = 0.f;  // 0..1
  float b = 0.f;  // 0..1
};

D2D1_COLOR_F UiColor(const Rgba& c, float scale = 1.f) {
  return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, (c.a / 255.f) * scale);
}

Hsb RgbaToHsb(const Rgba& c) {
  const float r = c.r / 255.f;
  const float g = c.g / 255.f;
  const float b = c.b / 255.f;
  const float maxv = std::max({r, g, b});
  const float minv = std::min({r, g, b});
  const float d = maxv - minv;
  Hsb out;
  out.b = maxv;
  out.s = maxv <= 0.f ? 0.f : d / maxv;
  if (d <= 1e-6f) {
    out.h = 0.f;
  } else if (maxv == r) {
    out.h = 60.f * std::fmod((g - b) / d + 6.f, 6.f);
  } else if (maxv == g) {
    out.h = 60.f * ((b - r) / d + 2.f);
  } else {
    out.h = 60.f * ((r - g) / d + 4.f);
  }
  return out;
}

Rgba HsbToRgba(const Hsb& hsb, uint8_t a) {
  const float h = std::fmod(std::fmod(hsb.h, 360.f) + 360.f, 360.f);
  const float s = std::clamp(hsb.s, 0.f, 1.f);
  const float v = std::clamp(hsb.b, 0.f, 1.f);
  const float c = v * s;
  const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
  const float m = v - c;
  float r = 0.f, g = 0.f, b = 0.f;
  if (h < 60.f) {
    r = c;
    g = x;
  } else if (h < 120.f) {
    r = x;
    g = c;
  } else if (h < 180.f) {
    g = c;
    b = x;
  } else if (h < 240.f) {
    g = x;
    b = c;
  } else if (h < 300.f) {
    r = x;
    b = c;
  } else {
    r = c;
    b = x;
  }
  Rgba out;
  out.r = static_cast<uint8_t>(std::lround((r + m) * 255.f));
  out.g = static_cast<uint8_t>(std::lround((g + m) * 255.f));
  out.b = static_cast<uint8_t>(std::lround((b + m) * 255.f));
  out.a = a;
  return out;
}

bool ParseHexColor(const std::wstring& text, Rgba& out, bool keep_alpha) {
  std::wstring s;
  s.reserve(text.size());
  for (wchar_t ch : text) {
    if (ch == L'#' || ch == L' ' || ch == L'\t') continue;
    if ((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F')) {
      s.push_back(ch);
    } else {
      return false;
    }
  }
  if (s.size() != 6 && s.size() != 8) return false;
  auto nibble = [](wchar_t ch) -> int {
    if (ch >= L'0' && ch <= L'9') return ch - L'0';
    if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
    return ch - L'A' + 10;
  };
  auto byte_at = [&](size_t i) -> uint8_t {
    return static_cast<uint8_t>((nibble(s[i]) << 4) | nibble(s[i + 1]));
  };
  out.r = byte_at(0);
  out.g = byte_at(2);
  out.b = byte_at(4);
  if (s.size() == 8) {
    out.a = byte_at(6);
  } else if (!keep_alpha) {
    out.a = 255;
  }
  return true;
}

std::wstring FormatHex(const Rgba& c) {
  wchar_t buf[16];
  swprintf_s(buf, L"#%02X%02X%02X", c.r, c.g, c.b);
  return buf;
}

class ColorDialogUi {
 public:
  bool show_modal(HWND owner, Rgba initial) {
    owner_ = owner;
    color_ = initial;
    hsb_ = RgbaToHsb(color_);
    mode_ = ColorMode::Rgb;
    edit_ = EditField::None;
    edit_buf_.clear();
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
    const int w = 520;
    const int h = 420;
    const int x = owner_rc.left + ((owner_rc.right - owner_rc.left) - w) / 2;
    const int y = owner_rc.top + ((owner_rc.bottom - owner_rc.top) - h) / 2;

    Settings tmp_s;
    load_settings(tmp_s);
    lang_ = lang_from_key(tmp_s.language);
    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kColorDialogClass,
                            tr(lang_, StringId::kColorDialogTitle),
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
        release_target();
        layout();
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
      case WM_CHAR:
        on_char(static_cast<wchar_t>(wp));
        return 0;
      case WM_KEYDOWN:
        on_keydown(wp);
        return 0;
      case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
          POINT pt{};
          GetCursorPos(&pt);
          ScreenToClient(hwnd_, &pt);
          const ColorHit h = hit_test(MulDiv(pt.x, 96, dpi_), MulDiv(pt.y, 96, dpi_));
          const bool hand = h != ColorHit::None;
          SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
          return TRUE;
        }
        break;
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
    if (!SUCCEEDED(d2d_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi_f,
                                         dpi_f),
            D2D1::HwndRenderTargetProperties(hwnd_,
                                             D2D1::SizeU(static_cast<UINT>(rc.right), static_cast<UINT>(rc.bottom)),
                                             D2D1_PRESENT_OPTIONS_NONE),
            rt_.GetAddressOf())))
      return false;
    return true;
  }

  void release_target() { rt_.Reset(); }

  RECT box(int x, int y, int w, int h) const { return RECT{x, y, x + w, y + h}; }

  void layout() {
    const int cw = client_dip_w();
    const int m = 16;
    const int row = 26;
    const int row_gap = 6;
    int y = m;

    const int hex_w = dip(108);
    preview_ = box(m, y, cw - m * 2 - hex_w - 8, 44);
    hex_ = box(m + (cw - m * 2 - hex_w), y + 8, hex_w, 28);
    y += 44 + 10;

    presets_label_ = box(m, y, cw - m * 2, 16);
    y += 18;
    const int preset_gap = 8;
    const int preset_size = 24;
    const int presets_w = kPresetCount * preset_size + (kPresetCount - 1) * preset_gap;
    int px = m;
    for (int i = 0; i < kPresetCount; ++i) {
      presets_[i] = box(px, y, preset_size, preset_size);
      px += preset_size + preset_gap;
    }
    (void)presets_w;
    y += preset_size + 12;

    const int tab_w = dip(64);
    mode_rgb_ = box(m, y, tab_w, row);
    mode_hsb_ = box(m + tab_w + 6, y, tab_w, row);
    y += row + 10;

    slider_[0] = box(m, y, cw - m * 2, row);
    value_[0] = box(slider_[0].right - dip(44), y, dip(44), row);
    y += row + row_gap;
    slider_[1] = box(m, y, cw - m * 2, row);
    value_[1] = box(slider_[1].right - dip(44), y, dip(44), row);
    y += row + row_gap;
    slider_[2] = box(m, y, cw - m * 2, row);
    value_[2] = box(slider_[2].right - dip(44), y, dip(44), row);
    y += row + row_gap;
    slider_[3] = box(m, y, cw - m * 2, row);
    value_[3] = box(slider_[3].right - dip(44), y, dip(44), row);
    y += row + 14;

    const int btn_w = (cw - m * 2 - 8) / 2;
    ok_ = box(m, y, btn_w, row);
    cancel_ = box(m + btn_w + 8, y, btn_w, row);
  }

  bool contains(const RECT& r, int x, int y) const {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
  }

  ColorHit hit_test(int x, int y) const {
    if (contains(hex_, x, y)) return ColorHit::Hex;
    if (contains(mode_rgb_, x, y)) return ColorHit::ModeRgb;
    if (contains(mode_hsb_, x, y)) return ColorHit::ModeHsb;
    for (int i = 0; i < kPresetCount; ++i) {
      if (contains(presets_[i], x, y)) return static_cast<ColorHit>(static_cast<int>(ColorHit::Preset0) + i);
    }
    for (int i = 0; i < 4; ++i) {
      if (contains(value_[i], x, y)) return static_cast<ColorHit>(static_cast<int>(ColorHit::Value0) + i);
      if (contains(slider_[i], x, y)) return static_cast<ColorHit>(static_cast<int>(ColorHit::Slider0) + i);
    }
    if (contains(ok_, x, y)) return ColorHit::Ok;
    if (contains(cancel_, x, y)) return ColorHit::Cancel;
    return ColorHit::None;
  }

  int channel_max(int index) const {
    if (index == 3) return 255;
    if (mode_ == ColorMode::Rgb) return 255;
    if (index == 0) return 360;
    return 100;
  }

  int channel_value(int index) const {
    if (index == 3) return color_.a;
    if (mode_ == ColorMode::Rgb) {
      if (index == 0) return color_.r;
      if (index == 1) return color_.g;
      return color_.b;
    }
    if (index == 0) return static_cast<int>(std::lround(hsb_.h));
    if (index == 1) return static_cast<int>(std::lround(hsb_.s * 100.f));
    return static_cast<int>(std::lround(hsb_.b * 100.f));
  }

  void set_channel_value(int index, int value) {
    value = std::clamp(value, 0, channel_max(index));
    if (index == 3) {
      color_.a = static_cast<uint8_t>(value);
      return;
    }
    if (mode_ == ColorMode::Rgb) {
      if (index == 0) color_.r = static_cast<uint8_t>(value);
      else if (index == 1) color_.g = static_cast<uint8_t>(value);
      else color_.b = static_cast<uint8_t>(value);
      const float prev_h = hsb_.h;
      hsb_ = RgbaToHsb(color_);
      if (hsb_.s <= 1e-6f) hsb_.h = prev_h;
      return;
    }
    if (index == 0) hsb_.h = static_cast<float>(value);
    else if (index == 1) hsb_.s = value / 100.f;
    else hsb_.b = value / 100.f;
    color_ = HsbToRgba(hsb_, color_.a);
  }

  void sync_hsb_from_rgb() {
    const float prev_h = hsb_.h;
    hsb_ = RgbaToHsb(color_);
    if (hsb_.s <= 1e-6f) hsb_.h = prev_h;
  }

  void set_color(const Rgba& c) {
    color_ = c;
    sync_hsb_from_rgb();
  }

  RECT track_rect(int index) const {
    RECT r = slider_[index];
    r.left += dip(22);
    r.right = value_[index].left - dip(6);
    return r;
  }

  void set_channel_from_x(int index, int x) {
    const RECT track = track_rect(index);
    float t = (x - track.left) / static_cast<float>(std::max<LONG>(1, track.right - track.left));
    t = std::clamp(t, 0.f, 1.f);
    set_channel_value(index, static_cast<int>(std::lround(t * channel_max(index))));
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void begin_edit(EditField field) {
    if (edit_ != EditField::None && edit_ != field) commit_edit();
    edit_ = field;
    if (field == EditField::Hex) {
      edit_buf_ = FormatHex(color_);
    } else {
      const int idx = static_cast<int>(field) - static_cast<int>(EditField::Ch0);
      wchar_t buf[16];
      swprintf_s(buf, L"%d", channel_value(idx));
      edit_buf_ = buf;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void cancel_edit() {
    edit_ = EditField::None;
    edit_buf_.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void commit_edit() {
    if (edit_ == EditField::None) return;
    if (edit_ == EditField::Hex) {
      Rgba parsed = color_;
      if (ParseHexColor(edit_buf_, parsed, true)) {
        set_color(parsed);
      }
    } else {
      const int idx = static_cast<int>(edit_) - static_cast<int>(EditField::Ch0);
      int value = 0;
      if (!edit_buf_.empty()) {
        value = _wtoi(edit_buf_.c_str());
      }
      set_channel_value(idx, value);
    }
    edit_ = EditField::None;
    edit_buf_.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void on_char(wchar_t ch) {
    if (edit_ == EditField::None) return;
    if (ch == L'\r' || ch == L'\n') {
      commit_edit();
      return;
    }
    if (ch == 0x1B) {
      cancel_edit();
      return;
    }
    if (ch == 0x08) {
      if (!edit_buf_.empty()) edit_buf_.pop_back();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (edit_ == EditField::Hex) {
      const bool ok = (ch == L'#') || (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') ||
                      (ch >= L'A' && ch <= L'F');
      if (!ok || edit_buf_.size() >= 9) return;
      edit_buf_.push_back(ch);
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (ch >= L'0' && ch <= L'9' && edit_buf_.size() < 3) {
      edit_buf_.push_back(ch);
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }

  void paste_clipboard() {
    if (edit_ == EditField::None) return;
    if (!OpenClipboard(hwnd_)) return;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
      const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(h));
      if (text) {
        std::wstring filtered;
        for (const wchar_t* p = text; *p; ++p) {
          const wchar_t ch = *p;
          if (edit_ == EditField::Hex) {
            if (ch == L'#' || (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') ||
                (ch >= L'A' && ch <= L'F')) {
              filtered.push_back(ch);
            }
          } else if (ch >= L'0' && ch <= L'9') {
            filtered.push_back(ch);
          }
        }
        if (edit_ == EditField::Hex) {
          if (filtered.size() > 9) filtered.resize(9);
        } else if (filtered.size() > 3) {
          filtered.resize(3);
        }
        if (!filtered.empty()) {
          edit_buf_ = filtered;
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        GlobalUnlock(h);
      }
    }
    CloseClipboard();
  }

  void on_keydown(WPARAM vk) {
    if (edit_ == EditField::None) {
      if (vk == VK_ESCAPE) {
        accepted_ = false;
        DestroyWindow(hwnd_);
      } else if (vk == VK_RETURN) {
        accepted_ = true;
        DestroyWindow(hwnd_);
      }
      return;
    }
    if (vk == VK_ESCAPE) {
      cancel_edit();
    } else if (vk == VK_RETURN) {
      commit_edit();
    } else if (vk == VK_BACK) {
      if (!edit_buf_.empty()) {
        edit_buf_.pop_back();
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
    } else if (vk == L'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
      paste_clipboard();
    }
  }

  void on_mouse(int px, int py, bool down, bool click) {
    const int x = MulDiv(px, 96, dpi_);
    const int y = MulDiv(py, 96, dpi_);
    hover_ = hit_test(x, y);

    if (dragging_ != ColorHit::None && down) {
      const int idx = static_cast<int>(dragging_) - static_cast<int>(ColorHit::Slider0);
      if (idx >= 0 && idx < 4) set_channel_from_x(idx, x);
      return;
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
    if (!click) return;

    if (edit_ != EditField::None) {
      const bool keep = (edit_ == EditField::Hex && hover_ == ColorHit::Hex) ||
                        (edit_ == EditField::Ch0 && hover_ == ColorHit::Value0) ||
                        (edit_ == EditField::Ch1 && hover_ == ColorHit::Value1) ||
                        (edit_ == EditField::Ch2 && hover_ == ColorHit::Value2) ||
                        (edit_ == EditField::Ch3 && hover_ == ColorHit::Value3);
      if (!keep) commit_edit();
    }

    switch (hover_) {
      case ColorHit::Slider0:
      case ColorHit::Slider1:
      case ColorHit::Slider2:
      case ColorHit::Slider3: {
        dragging_ = hover_;
        const int idx = static_cast<int>(hover_) - static_cast<int>(ColorHit::Slider0);
        set_channel_from_x(idx, x);
        break;
      }
      case ColorHit::Value0:
        begin_edit(EditField::Ch0);
        break;
      case ColorHit::Value1:
        begin_edit(EditField::Ch1);
        break;
      case ColorHit::Value2:
        begin_edit(EditField::Ch2);
        break;
      case ColorHit::Value3:
        begin_edit(EditField::Ch3);
        break;
      case ColorHit::Hex:
        begin_edit(EditField::Hex);
        break;
      case ColorHit::ModeRgb:
        if (mode_ != ColorMode::Rgb) {
          mode_ = ColorMode::Rgb;
          cancel_edit();
        }
        break;
      case ColorHit::ModeHsb:
        if (mode_ != ColorMode::Hsb) {
          mode_ = ColorMode::Hsb;
          sync_hsb_from_rgb();
          cancel_edit();
        }
        break;
      case ColorHit::Preset0:
      case ColorHit::Preset1:
      case ColorHit::Preset2:
      case ColorHit::Preset3:
      case ColorHit::Preset4:
      case ColorHit::Preset5:
      case ColorHit::Preset6: {
        const int idx = static_cast<int>(hover_) - static_cast<int>(ColorHit::Preset0);
        Rgba c = kColorPresets[idx];
        c.a = color_.a;
        set_color(c);
        cancel_edit();
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
      }
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

  ComPtr<IDWriteTextFormat> make_text_format(float size, DWRITE_TEXT_ALIGNMENT align,
                                             DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) {
    ComPtr<IDWriteTextFormat> fmt;
    dwrite_->CreateTextFormat(lang_font_family(lang_), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", fmt.GetAddressOf());
    fmt->SetTextAlignment(align);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    return fmt;
  }

  void draw_text(IDWriteTextFormat* fmt, const wchar_t* text, const D2D1_RECT_F& box,
                 const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    rt_->DrawTextW(text, static_cast<UINT32>(wcslen(text)), fmt, box, brush.Get());
  }

  static D2D1_RECT_F r2f(const RECT& r) {
    return D2D1::RectF(static_cast<float>(r.left), static_cast<float>(r.top),
                       static_cast<float>(r.right), static_cast<float>(r.bottom));
  }

  void paint_mode_tab(const RECT& rc, const wchar_t* label, bool active) {
    ComPtr<ID2D1SolidColorBrush> fill;
    rt_->CreateSolidColorBrush(active ? UiColor(kDefaultColorEn) : UiColor(kUiFill), fill.GetAddressOf());
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r2f(rc), 8.f, 8.f), fill.Get());
    auto fmt = make_text_format(12.f, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    draw_text(fmt.Get(), label, r2f(rc), active ? D2D1::ColorF(1, 1, 1, 1) : UiColor(kUiText));
  }

  void paint_edit_box(const RECT& rc, const std::wstring& text, bool editing) {
    ComPtr<ID2D1SolidColorBrush> fill;
    rt_->CreateSolidColorBrush(editing ? UiColor(kUiFillHover) : UiColor(kUiFill), fill.GetAddressOf());
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r2f(rc), 6.f, 6.f), fill.Get());
    if (editing) {
      ComPtr<ID2D1SolidColorBrush> stroke;
      rt_->CreateSolidColorBrush(UiColor(kDefaultColorEn), stroke.GetAddressOf());
      rt_->DrawRoundedRectangle(D2D1::RoundedRect(r2f(rc), 6.f, 6.f), stroke.Get(), 1.2f);
    }
    auto fmt = make_text_format(12.f, DWRITE_TEXT_ALIGNMENT_CENTER);
    draw_text(fmt.Get(), text.c_str(), r2f(rc), UiColor(kUiText));
  }

  void paint_slider(int index, const wchar_t* label, const D2D1_COLOR_F& accent) {
    const RECT& rc = slider_[index];
    const RECT& val_rc = value_[index];
    auto label_fmt = make_text_format(12.f, DWRITE_TEXT_ALIGNMENT_CENTER);
    draw_text(label_fmt.Get(), label, D2D1::RectF(static_cast<float>(rc.left), static_cast<float>(rc.top),
                                                  static_cast<float>(rc.left + dip(18)),
                                                  static_cast<float>(rc.bottom)),
              UiColor(kUiTextSecondary));

    const RECT track = track_rect(index);
    const float mid_y = (rc.top + rc.bottom) * 0.5f;
    const float track_h = 4.f;
    const float knob_r = 6.f;

    ComPtr<ID2D1SolidColorBrush> back;
    rt_->CreateSolidColorBrush(UiColor(kUiFill), back.GetAddressOf());
    rt_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(static_cast<float>(track.left), mid_y - track_h * 0.5f,
                                     static_cast<float>(track.right), mid_y + track_h * 0.5f),
                          2, 2),
        back.Get());

    const float t = channel_value(index) / static_cast<float>(channel_max(index));
    const float knob_x = track.left + t * (track.right - track.left);
    ComPtr<ID2D1SolidColorBrush> acc;
    rt_->CreateSolidColorBrush(accent, acc.GetAddressOf());
    rt_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(static_cast<float>(track.left), mid_y - track_h * 0.5f, knob_x,
                                     mid_y + track_h * 0.5f),
                          2, 2),
        acc.Get());
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knob_x, mid_y), knob_r, knob_r), acc.Get());

    const EditField field = static_cast<EditField>(static_cast<int>(EditField::Ch0) + index);
    const bool editing = edit_ == field;
    std::wstring shown;
    if (editing) {
      shown = edit_buf_;
    } else {
      wchar_t buf[16];
      swprintf_s(buf, L"%d", channel_value(index));
      shown = buf;
    }
    paint_edit_box(val_rc, shown, editing);
  }

  void paint_button(const RECT& rc, const wchar_t* label, bool primary) {
    ComPtr<ID2D1SolidColorBrush> fill;
    rt_->CreateSolidColorBrush(primary ? UiColor(kDefaultColorEn) : UiColor(kUiFill), fill.GetAddressOf());
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r2f(rc), 8.f, 8.f), fill.Get());
    auto fmt = make_text_format(13.f, DWRITE_TEXT_ALIGNMENT_CENTER);
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
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r2f(preview_), 10.f, 10.f), preview_brush.Get());

    const bool hex_editing = edit_ == EditField::Hex;
    paint_edit_box(hex_, hex_editing ? edit_buf_ : FormatHex(color_), hex_editing);

    auto presets_fmt = make_text_format(11.f, DWRITE_TEXT_ALIGNMENT_LEADING);
    draw_text(presets_fmt.Get(), tr(lang_, StringId::kColorDialogPresets), r2f(presets_label_),
              UiColor(kUiTextSecondary));

    for (int i = 0; i < kPresetCount; ++i) {
      ComPtr<ID2D1SolidColorBrush> brush;
      rt_->CreateSolidColorBrush(UiColor(kColorPresets[i]), brush.GetAddressOf());
      rt_->FillRoundedRectangle(D2D1::RoundedRect(r2f(presets_[i]), 6.f, 6.f), brush.Get());
      const bool selected = color_.r == kColorPresets[i].r && color_.g == kColorPresets[i].g &&
                            color_.b == kColorPresets[i].b;
      if (selected || hover_ == static_cast<ColorHit>(static_cast<int>(ColorHit::Preset0) + i)) {
        ComPtr<ID2D1SolidColorBrush> ring;
        rt_->CreateSolidColorBrush(UiColor(kUiText), ring.GetAddressOf());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(r2f(presets_[i]), 6.f, 6.f), ring.Get(), 1.5f);
      }
    }

    paint_mode_tab(mode_rgb_, tr(lang_, StringId::kColorDialogRgb), mode_ == ColorMode::Rgb);
    paint_mode_tab(mode_hsb_, tr(lang_, StringId::kColorDialogHsb), mode_ == ColorMode::Hsb);

    if (mode_ == ColorMode::Rgb) {
      paint_slider(0, L"R", D2D1::ColorF(0.96f, 0.26f, 0.21f, 1.f));
      paint_slider(1, L"G", D2D1::ColorF(0.13f, 0.77f, 0.37f, 1.f));
      paint_slider(2, L"B", D2D1::ColorF(0.18f, 0.51f, 0.99f, 1.f));
    } else {
      paint_slider(0, L"H", D2D1::ColorF(0.95f, 0.45f, 0.15f, 1.f));
      paint_slider(1, L"S", D2D1::ColorF(0.55f, 0.35f, 0.90f, 1.f));
      paint_slider(2, L"B", D2D1::ColorF(0.35f, 0.35f, 0.40f, 1.f));
    }
    paint_slider(3, L"A", UiColor(kUiTextSecondary));

    paint_button(ok_, tr(lang_, StringId::kOk), true);
    paint_button(cancel_, tr(lang_, StringId::kCancel), false);

    const HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) release_target();
    EndPaint(hwnd_, &ps);
  }

  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  Rgba color_{};
  Hsb hsb_{};
  ColorMode mode_ = ColorMode::Rgb;
  EditField edit_ = EditField::None;
  std::wstring edit_buf_;
  Lang lang_ = Lang::Ja;
  bool accepted_ = false;
  bool done_ = false;
  int dpi_ = 96;
  ColorHit hover_ = ColorHit::None;
  ColorHit dragging_ = ColorHit::None;
  ComPtr<ID2D1Factory> d2d_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<ID2D1HwndRenderTarget> rt_;
  RECT preview_{}, hex_{}, presets_label_{};
  RECT presets_[kPresetCount]{};
  RECT mode_rgb_{}, mode_hsb_{};
  RECT slider_[4]{}, value_[4]{};
  RECT ok_{}, cancel_{};
};

}  // namespace

bool win_show_color_dialog(HWND owner, Rgba initial, Rgba& out) {
  ColorDialogUi dlg;
  if (!dlg.show_modal(owner, initial)) return false;
  out = dlg.result();
  return true;
}

}  // namespace imeaura
