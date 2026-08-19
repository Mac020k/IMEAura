#pragma once

#include "core/color.h"

#include <windows.h>

namespace imeaura {

bool win_show_color_dialog(HWND owner, Rgba initial, Rgba& out);

}  // namespace imeaura
