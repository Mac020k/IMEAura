#pragma once

#include "core/color.h"
#include "core/settings.h"

namespace imeaura {

enum class FollowTarget { ActiveWindow, Cursor };

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  bool operator==(const Rect& o) const {
    return x == o.x && y == o.y && width == o.width && height == o.height;
  }
};

struct PolicyInput {
  bool ime_japanese = false;
  bool text_focused = false;
  bool text_hovered = false;
  bool reduce_motion = false;

  bool operator==(const PolicyInput& o) const {
    return ime_japanese == o.ime_japanese && text_focused == o.text_focused &&
           text_hovered == o.text_hovered && reduce_motion == o.reduce_motion;
  }
  bool operator!=(const PolicyInput& o) const { return !(*this == o); }
};

struct PolicyOutput {
  bool visible = false;
  bool focused = false;
  bool hovered = false;
  Rgba target_color{};
  FollowTarget follow = FollowTarget::ActiveWindow;
  int fade_ms = 0;
  int blend_ms = 0;
};

PolicyOutput evaluate_policy(const Settings& settings, const PolicyInput& input);

}  // namespace imeaura
