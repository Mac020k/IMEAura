#include "core/policy.h"
#include "core/settings.h"
#include "core/tokens.h"

#include <cstdlib>
#include <iostream>

using namespace imeaura;

#define EXPECT(cond)                                                     \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " #cond \
                << "\n";                                                 \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

int main() {
  int failures = 0;
  Settings s = default_settings();

  PolicyInput in{};
  in.ime_lang = "ja";
  auto out = evaluate_policy(s, in);
  EXPECT(out.visible);
  EXPECT(out.target_color.r == kDefaultColorJp.r);

  in.ime_lang = "en";
  out = evaluate_policy(s, in);
  EXPECT(out.target_color.r == kDefaultColorEn.r);

  s.aura_slots.push_back({"ko", kDefaultAuraSlotColors[0]});
  s = normalize_settings(s);
  in.ime_lang = "ko";
  out = evaluate_policy(s, in);
  EXPECT(out.target_color.r == kDefaultAuraSlotColors[0].r);

  in.ime_lang = "vi";  // unmatched → en fallback
  out = evaluate_policy(s, in);
  EXPECT(out.target_color.r == kDefaultColorEn.r);

  s.aura_enabled = false;
  out = evaluate_policy(s, in);
  EXPECT(!out.visible);

  s.aura_enabled = true;
  s.display_mode = kDisplayModeOnFocus;
  in.text_focused = true;
  out = evaluate_policy(s, in);
  EXPECT(out.visible);

  s.show_on_hover = true;
  in.text_focused = false;
  in.text_hovered = true;
  out = evaluate_policy(s, in);
  EXPECT(out.visible);
  EXPECT(out.follow == FollowTarget::Cursor);

  in.reduce_motion = true;
  out = evaluate_policy(s, in);
  EXPECT(out.fade_ms == 0);
  EXPECT(out.blend_ms == 0);

  if (failures) return 1;
  std::cout << "policy_table_test: OK\n";
  return 0;
}
