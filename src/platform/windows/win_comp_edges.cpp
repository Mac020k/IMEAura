#include "platform/windows/win_comp_edges.h"

#include "core/overlay_layout.h"

#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.h>

#include <algorithm>
#include <array>
#include <chrono>

using namespace winrt;
using namespace Windows::System;
using namespace Windows::UI::Composition;

namespace imeaura {
namespace {

enum class EdgeKind { Top, Bottom, Left, Right };

Windows::UI::Color WinColor(const Rgba& c, float alpha_scale) {
  const auto a = static_cast<uint8_t>(std::clamp(c.a * alpha_scale, 0.f, 255.f));
  return Windows::UI::ColorHelper::FromArgb(a, c.r, c.g, c.b);
}

CompositionLinearGradientBrush MakeEdgeBrush(const Compositor& c, const Rgba& color, EdgeKind edge) {
  auto brush = c.CreateLinearGradientBrush();
  brush.ColorStops().Append(c.CreateColorGradientStop(0.0f, WinColor(color, 1.f)));
  brush.ColorStops().Append(c.CreateColorGradientStop(0.55f, WinColor(color, 0.35f)));
  brush.ColorStops().Append(c.CreateColorGradientStop(1.0f, WinColor(color, 0.f)));

  switch (edge) {
    case EdgeKind::Top:
      brush.StartPoint({0.f, 0.f});
      brush.EndPoint({0.f, 1.f});
      break;
    case EdgeKind::Bottom:
      brush.StartPoint({0.f, 1.f});
      brush.EndPoint({0.f, 0.f});
      break;
    case EdgeKind::Left:
      brush.StartPoint({0.f, 0.f});
      brush.EndPoint({1.f, 0.f});
      break;
    case EdgeKind::Right:
      brush.StartPoint({1.f, 0.f});
      brush.EndPoint({0.f, 0.f});
      break;
  }
  return brush;
}

constexpr EdgeKind EdgeAt(int index) { return static_cast<EdgeKind>(index); }

void ApplyEdgeHostInputPassthrough(HWND host) {
  if (!host) return;
  LONG_PTR ex = GetWindowLongPtrW(host, GWL_EXSTYLE);
  ex |= WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
  SetWindowLongPtrW(host, GWL_EXSTYLE, ex);
  SetLayeredWindowAttributes(host, 0, 255, LWA_ALPHA);
  SetWindowPos(host, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                                               SWP_FRAMECHANGED);
}

}  // namespace

void win_edge_host_set_input_passthrough(HWND host) { ApplyEdgeHostInputPassthrough(host); }

struct WinCompEdges::Impl {
  Compositor compositor{nullptr};
  DispatcherQueueController queue{nullptr};

  struct EdgeLayer {
    HWND host = nullptr;
    Desktop::DesktopWindowTarget target{nullptr};
    ContainerVisual root{nullptr};
    SpriteVisual sprite{nullptr};
  };

  std::array<EdgeLayer, kEdgeHostCount> layers{};
  float opacity = -1.f;
  Rgba color{};
  bool color_set = false;
};

WinCompEdges::~WinCompEdges() { shutdown(); }

bool win_check_os_and_compositor() {
  using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  struct OsVersion {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
  } vi{};
  vi.dwOSVersionInfoSize = sizeof(vi);
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) return false;
  const auto rtl = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
  if (!rtl || rtl(reinterpret_cast<PRTL_OSVERSIONINFOW>(&vi)) != 0) return false;
  if (vi.dwMajorVersion < 10) return false;
  if (vi.dwBuildNumber < 17134) return false;
  return true;
}

bool WinCompEdges::init(const HWND hosts[kEdgeHostCount]) {
  if (!hosts) return false;
  impl_ = new Impl();

  try {
    init_apartment(apartment_type::single_threaded);
    DispatcherQueueOptions opts{sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
    check_hresult(CreateDispatcherQueueController(
        opts, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(put_abi(impl_->queue))));

    impl_->compositor = Compositor();
    winrt::com_ptr<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop> interop;
    impl_->compositor.as(interop);

    const Rgba dummy{255, 255, 255, 255};
    for (int i = 0; i < kEdgeHostCount; ++i) {
      if (!hosts[i]) {
        shutdown();
        return false;
      }

      auto& layer = impl_->layers[static_cast<size_t>(i)];
      layer.host = hosts[i];

      Desktop::DesktopWindowTarget target{nullptr};
      check_hresult(interop->CreateDesktopWindowTarget(
          hosts[i], true,
          reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(put_abi(target))));
      layer.target = target;

      layer.root = impl_->compositor.CreateContainerVisual();
      layer.root.RelativeSizeAdjustment({1.f, 1.f});
      layer.root.Opacity(1.f);
      layer.root.IsHitTestVisible(false);
      layer.target.Root(layer.root);

      layer.sprite = impl_->compositor.CreateSpriteVisual();
      layer.sprite.RelativeSizeAdjustment({1.f, 1.f});
      layer.sprite.Brush(MakeEdgeBrush(impl_->compositor, dummy, EdgeAt(i)));
      layer.sprite.Opacity(0.f);
      layer.sprite.IsHitTestVisible(false);
      layer.root.Children().InsertAtTop(layer.sprite);
      ApplyEdgeHostInputPassthrough(hosts[i]);
    }
    return true;
  } catch (...) {
    shutdown();
    return false;
  }
}

void WinCompEdges::shutdown() {
  if (!impl_) return;
  for (auto& layer : impl_->layers) {
    layer.target = nullptr;
    layer.root = nullptr;
    layer.sprite = nullptr;
    layer.host = nullptr;
  }
  impl_->compositor = nullptr;
  impl_->queue = nullptr;
  delete impl_;
  impl_ = nullptr;
}

void WinCompEdges::layout(const Rect& monitor, int thickness_px) {
  if (!impl_) return;
  const int t = clamp_gradient_thickness(thickness_px, monitor.width, monitor.height);

  const struct {
    int x;
    int y;
    int w;
    int h;
  } placements[kEdgeHostCount] = {
      {monitor.x, monitor.y, monitor.width, t},
      {monitor.x, monitor.y + monitor.height - t, monitor.width, t},
      {monitor.x, monitor.y, t, monitor.height},
      {monitor.x + monitor.width - t, monitor.y, t, monitor.height},
  };

  for (int i = 0; i < kEdgeHostCount; ++i) {
    const auto& p = placements[i];
    const HWND host = impl_->layers[static_cast<size_t>(i)].host;
    if (!host) continue;
    SetWindowPos(host, HWND_TOPMOST, p.x, p.y, p.w, p.h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ApplyEdgeHostInputPassthrough(host);
  }
}

void WinCompEdges::set_color(const Rgba& color, int blend_ms) {
  if (!impl_) return;
  if (impl_->color_set && impl_->color == color) return;
  const Rgba from = impl_->color_set ? impl_->color : color;
  impl_->color = color;
  impl_->color_set = true;
  const bool animate = blend_ms > 0 && !(from == color);

  for (int i = 0; i < kEdgeHostCount; ++i) {
    auto& sprite = impl_->layers[static_cast<size_t>(i)].sprite;
    if (!sprite) continue;

    auto brush = MakeEdgeBrush(impl_->compositor, animate ? from : color, EdgeAt(i));
    sprite.Brush(brush);
    if (!animate) continue;

    auto snap = [&](uint32_t index, float scale) {
      auto anim = impl_->compositor.CreateColorKeyFrameAnimation();
      anim.Duration(std::chrono::milliseconds(blend_ms));
      anim.InterpolationColorSpace(CompositionColorSpace::Rgb);
      anim.InsertKeyFrame(1.0f, WinColor(color, scale));
      brush.ColorStops().GetAt(index).StartAnimation(L"Color", anim);
    };
    snap(0, 1.f);
    snap(1, 0.35f);
    snap(2, 0.f);
  }
}

void WinCompEdges::set_visible(bool visible, int fade_ms) {
  if (!impl_) return;
  const float target = visible ? 1.f : 0.f;
  if (impl_->opacity == target) return;
  impl_->opacity = target;

  for (auto& layer : impl_->layers) {
    if (layer.root) layer.root.Opacity(1.f);
    if (!layer.sprite) continue;

    layer.sprite.StopAnimation(L"Opacity");
    if (fade_ms <= 0) {
      layer.sprite.Opacity(target);
      continue;
    }
    auto anim = impl_->compositor.CreateScalarKeyFrameAnimation();
    anim.Duration(std::chrono::milliseconds(fade_ms));
    anim.InsertKeyFrame(1.0f, target);
    layer.sprite.StartAnimation(L"Opacity", anim);
    layer.sprite.Opacity(target);
  }
}

}  // namespace imeaura
