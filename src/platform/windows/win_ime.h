#pragma once

#include <windows.h>

#include <functional>

namespace imeaura {

bool win_is_japanese_input();
bool win_native_edit_is_focused();
bool win_native_edit_is_hovered();

void win_ime_worker_start(std::function<void()> on_change);
void win_ime_worker_stop();
bool win_ime_worker_japanese();
void win_ime_worker_poke();

void win_text_input_start();
void win_text_input_stop();
bool win_text_input_focused();
bool win_text_input_hovered();
void win_text_input_set_changed_callback(std::function<void()> cb);
void win_text_input_set_hover_enabled(bool enabled);

}  // namespace imeaura
