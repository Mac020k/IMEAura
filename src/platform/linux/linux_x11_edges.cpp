#include "platform/linux/linux_x11_edges.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace imeaura {

bool LinuxX11Edges::init(Display* dpy) {
  if (!dpy) return false;
  dpy_ = dpy;
  root_ = DefaultRootWindow(dpy_);
  return true;
}

void LinuxX11Edges::shutdown() {
  if (!dpy_) return;
  for (int i = 0; i < 4; ++i) {
    if (edges_[i]) {
      XDestroyWindow(dpy_, edges_[i]);
      edges_[i] = 0;
      mapped_[i] = false;
    }
  }
  dpy_ = nullptr;
  root_ = 0;
}

unsigned long LinuxX11Edges::rgba_to_pixel(const Rgba& c) const {
  if (!dpy_) return 0;
  XColor xc{};
  xc.red = static_cast<unsigned short>(c.r) * 257;
  xc.green = static_cast<unsigned short>(c.g) * 257;
  xc.blue = static_cast<unsigned short>(c.b) * 257;
  xc.flags = DoRed | DoGreen | DoBlue;
  Colormap cmap = DefaultColormap(dpy_, DefaultScreen(dpy_));
  if (!XAllocColor(dpy_, cmap, &xc)) {
    return BlackPixel(dpy_, DefaultScreen(dpy_));
  }
  return xc.pixel;
}

void LinuxX11Edges::ensure_window(int idx) {
  if (!dpy_ || edges_[idx]) return;

  const unsigned long bg = rgba_to_pixel(color_);
  edges_[idx] = XCreateSimpleWindow(dpy_, root_, 0, 0, 1, 1, 0, bg, bg);

  XSetWindowAttributes attrs{};
  attrs.override_redirect = True;
  attrs.event_mask = NoEventMask;
  attrs.background_pixel = bg;
  XChangeWindowAttributes(dpy_, edges_[idx], CWOverrideRedirect | CWEventMask | CWBackPixel, &attrs);

  Atom wm_state = XInternAtom(dpy_, "_NET_WM_STATE", False);
  Atom above = XInternAtom(dpy_, "_NET_WM_STATE_ABOVE", False);
  Atom wtype = XInternAtom(dpy_, "_NET_WM_WINDOW_TYPE", False);
  Atom dock = XInternAtom(dpy_, "_NET_WM_WINDOW_TYPE_DOCK", False);
  Atom states[1] = {above};
  XChangeProperty(dpy_, edges_[idx], wm_state, XA_ATOM, 32, PropModeReplace,
                  reinterpret_cast<unsigned char*>(states), 1);
  Atom types[1] = {dock};
  XChangeProperty(dpy_, edges_[idx], wtype, XA_ATOM, 32, PropModeReplace,
                  reinterpret_cast<unsigned char*>(types), 1);
}

void LinuxX11Edges::update_geometry(int idx) {
  if (!dpy_ || !edges_[idx]) return;
  const int t = thickness_;
  int x = 0, y = 0, w = 0, h = 0;
  switch (idx) {
    case 0:  // top
      x = monitor_.x;
      y = monitor_.y;
      w = monitor_.width;
      h = t;
      break;
    case 1:  // bottom
      x = monitor_.x;
      y = monitor_.y + monitor_.height - t;
      w = monitor_.width;
      h = t;
      break;
    case 2:  // left
      x = monitor_.x;
      y = monitor_.y;
      w = t;
      h = monitor_.height;
      break;
    case 3:  // right
      x = monitor_.x + monitor_.width - t;
      y = monitor_.y;
      w = t;
      h = monitor_.height;
      break;
    default:
      return;
  }
  XMoveResizeWindow(dpy_, edges_[idx], x, y, static_cast<unsigned>(w), static_cast<unsigned>(h));
}

void LinuxX11Edges::layout(const Rect& monitor, int thickness) {
  if (!dpy_) return;
  monitor_ = monitor;
  thickness_ = thickness > 0 ? thickness : 1;
  for (int i = 0; i < 4; ++i) {
    ensure_window(i);
    update_geometry(i);
  }
  XFlush(dpy_);
}

void LinuxX11Edges::set_color(const Rgba& color) {
  if (!dpy_) return;
  color_ = color;
  const unsigned long pixel = rgba_to_pixel(color_);
  for (int i = 0; i < 4; ++i) {
    ensure_window(i);
    XSetWindowBackground(dpy_, edges_[i], pixel);
    if (mapped_[i]) XClearWindow(dpy_, edges_[i]);
  }
  XFlush(dpy_);
}

void LinuxX11Edges::set_visible(bool visible) {
  if (!dpy_) return;
  visible_ = visible;
  for (int i = 0; i < 4; ++i) {
    ensure_window(i);
    if (visible && !mapped_[i]) {
      XMapRaised(dpy_, edges_[i]);
      mapped_[i] = true;
    } else if (!visible && mapped_[i]) {
      XUnmapWindow(dpy_, edges_[i]);
      mapped_[i] = false;
    }
  }
  XFlush(dpy_);
}

}  // namespace imeaura
