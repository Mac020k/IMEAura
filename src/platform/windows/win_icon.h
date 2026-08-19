#pragma once

#include <windows.h>

namespace imeaura {

// Load the application icon at the requested size. Returns nullptr on failure.
HICON win_load_app_icon(int size_px);

// Set large/small window icons from img/icon.svg (or img/icon.ico fallback).
void win_set_window_icons(HWND hwnd);

}  // namespace imeaura
