#pragma once

#include "core/color.h"
#include "core/policy.h"

#include <X11/Xlib.h>

namespace imeaura {

class LinuxX11Edges {
 public:
  bool init(Display* dpy);
  void shutdown();

  void layout(const Rect& monitor, int thickness);
  void set_color(const Rgba& color);
  void set_visible(bool visible);

 private:
  void ensure_window(int idx);
  void update_geometry(int idx);
  unsigned long rgba_to_pixel(const Rgba& c) const;

  Display* dpy_ = nullptr;
  Window root_ = 0;
  int thickness_ = 15;
  Rect monitor_{};
  Rgba color_{};
  bool visible_ = false;
  Window edges_[4]{};
  bool mapped_[4]{false, false, false, false};
};

}  // namespace imeaura
