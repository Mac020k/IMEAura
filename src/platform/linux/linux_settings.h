#pragma once

#include "core/settings.h"

#include <functional>

namespace imeaura::linux_settings {

bool create(Settings initial, std::function<void(const Settings&)> cb);
void destroy();
void show();
void hide();
bool visible();
void sync(const Settings& s);
void set_firefly_active(bool active);

}  // namespace imeaura::linux_settings
