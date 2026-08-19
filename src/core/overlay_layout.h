#pragma once

#include <algorithm>

namespace imeaura {

inline int clamp_gradient_thickness(int requested, int monitor_w, int monitor_h) {
  const int max_thickness = std::max(1, std::min(monitor_w, monitor_h) / 2);
  return std::max(1, std::min(requested, max_thickness));
}

}  // namespace imeaura
