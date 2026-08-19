#pragma once

#include <string>

namespace imeaura {

std::wstring win_module_dir();
std::wstring win_find_asset(const wchar_t* relative_path);
std::string win_read_asset_utf8(const char* relative_path);

}  // namespace imeaura
