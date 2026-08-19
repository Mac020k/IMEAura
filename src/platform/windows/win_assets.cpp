#include "platform/windows/win_assets.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <windows.h>

namespace fs = std::filesystem;

namespace imeaura {

std::wstring win_module_dir() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return fs::path(path).parent_path().wstring();
}

std::wstring win_find_asset(const wchar_t* relative_path) {
  fs::path dir = win_module_dir();
  for (int i = 0; i < 6; ++i) {
    const auto candidate = dir / relative_path;
    if (fs::exists(candidate)) return candidate.wstring();
    if (!dir.has_parent_path()) break;
    dir = dir.parent_path();
  }
  return {};
}

std::string win_read_asset_utf8(const char* relative_path) {
  const std::wstring wide(relative_path, relative_path + strlen(relative_path));
  const auto path = win_find_asset(wide.c_str());
  if (path.empty()) return {};
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace imeaura
