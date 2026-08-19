#pragma once

#include "core/color.h"
#include "core/policy.h"

#include <windows.h>

namespace imeaura {

inline constexpr int kEdgeHostCount = 4;

class WinCompEdges {
 public:
  WinCompEdges() = default;
  ~WinCompEdges();

  bool init(const HWND hosts[kEdgeHostCount]);
  void shutdown();
  void layout(const Rect& monitor, int thickness_px);
  void set_color(const Rgba& color, int blend_ms);
  void set_visible(bool visible, int fade_ms);

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

bool win_check_os_and_compositor();

// Composition-backed edge hosts cannot rely on WM_NCHITTEST alone; disable HWND input.
void win_edge_host_set_input_passthrough(HWND host);

}  // namespace imeaura
