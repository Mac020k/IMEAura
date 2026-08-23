#pragma once

#include "core/settings.h"

#include <functional>
#include <windows.h>

namespace imeaura::win_settings {

bool create(HINSTANCE instance, Settings initial, std::function<void(const Settings&)> cb);
void destroy();
void show();
void hide();
bool visible();
HWND hwnd();
void sync(const Settings& settings);
void set_firefly_active(bool active);

}  // namespace imeaura::win_settings
