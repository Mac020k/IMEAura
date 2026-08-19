#pragma once

#include "core/color.h"

namespace imeaura {

inline constexpr Rgba kDefaultColorJp{248, 40, 70, 255};
inline constexpr Rgba kDefaultColorEn{45, 129, 253, 255};

inline constexpr int kGradientWidthMin = 1;
inline constexpr int kGradientWidthMax = 100;
inline constexpr int kDefaultGradientWidth = 15;

inline constexpr int kFadeMs = 150;
inline constexpr int kStatusBlendMs = 180;
inline constexpr int kEntranceMs = 240;
inline constexpr int kRevealMs = 220;
inline constexpr int kStatusFlashMs = 1100;

inline constexpr int kFontPointSizeSmall = 11;
inline constexpr int kFontPointSizeMedium = 13;
inline constexpr int kFontPointSizeLarge = 16;

inline constexpr Rgba kUiBg{248, 249, 252, 255};
inline constexpr Rgba kUiText{28, 28, 30, 255};
inline constexpr Rgba kUiTextSecondary{90, 90, 98, 255};
inline constexpr Rgba kUiSeparator{60, 60, 67, 45};
inline constexpr Rgba kUiFill{120, 120, 128, 28};
inline constexpr Rgba kUiFillHover{120, 120, 128, 72};
inline constexpr Rgba kUiDanger{200, 50, 50, 255};
inline constexpr Rgba kUiDangerFill{200, 50, 50, 22};

inline constexpr int kUiMargin = 24;
inline constexpr int kUiRowGap = 8;
inline constexpr int kUiSectionGap = 16;
inline constexpr int kUiSpace1 = 4;
inline constexpr int kUiButtonPadY = 8;
inline constexpr int kUiButtonPadX = 14;
inline constexpr int kUiHitMin = 32;
inline constexpr int kUiSwatchW = 104;
inline constexpr int kUiSwatchH = 32;
inline constexpr int kUiMinWindowW = 300;
inline constexpr int kUiMinWindowH = 300;
inline constexpr int kUiScrollBarWidth = 12;
inline constexpr int kUiScrollBarMarginY = 8;
inline constexpr int kUiScrollBarMarginRight = 3;
inline constexpr int kUiScrollBarGutter = kUiScrollBarWidth + kUiScrollBarMarginRight;

inline constexpr int kUiTabBarHeight = 40;
inline constexpr int kUiTabPadX = 16;
inline constexpr int kUiTabIndicatorH = 3;
inline constexpr Rgba kUiTabActive{45, 129, 253, 255};
inline constexpr int kUiToggleW = 44;
inline constexpr int kUiToggleH = 24;
inline constexpr int kUiToggleKnob = 18;

inline int motion_ms(int duration, bool reduce_motion) {
  return reduce_motion ? 0 : (duration < 0 ? 0 : duration);
}

}  // namespace imeaura
