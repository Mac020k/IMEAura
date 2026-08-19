#pragma once

#include <windows.h>

#include <functional>

namespace imeaura {

bool win_is_japanese_input();
bool win_native_edit_is_focused();
bool win_native_edit_is_hovered();

void win_text_input_start();
void win_text_input_stop();
bool win_text_input_focused();
bool win_text_input_hovered();
void win_text_input_set_changed_callback(std::function<void()> cb);

}  // namespace imeaura
