#include "core/policy.h"

#include "core/tokens.h"

namespace imeaura {

PolicyOutput evaluate_policy(const Settings& settings, const PolicyInput& input) {
  PolicyOutput out{};
  out.focused = input.text_focused;
  out.hovered = input.text_hovered;

  if (settings.display_mode == kDisplayModeHidden) {
    out.visible = false;
    out.follow = FollowTarget::ActiveWindow;
  } else if (settings.display_mode == kDisplayModeAlways) {
    out.visible = true;
    out.follow = FollowTarget::ActiveWindow;
  } else {
    const bool show = input.text_focused || (settings.show_on_hover && input.text_hovered);
    out.visible = show;
    if (show && input.text_hovered && !input.text_focused) {
      out.follow = FollowTarget::Cursor;
    } else {
      out.follow = FollowTarget::ActiveWindow;
    }
  }

  out.target_color = input.ime_japanese ? settings.color_jp : settings.color_en;
  out.fade_ms = motion_ms(kFadeMs, input.reduce_motion);
  out.blend_ms = motion_ms(kStatusBlendMs, input.reduce_motion);
  return out;
}

}  // namespace imeaura
