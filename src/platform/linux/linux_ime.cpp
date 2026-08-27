#include "platform/linux/linux_ime.h"

#include "core/input_languages.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace imeaura {
namespace {

std::string ReadCommand(const char* cmd) {
  FILE* f = popen(cmd, "r");
  if (!f) return {};
  char buf[256]{};
  const bool got = fgets(buf, sizeof(buf), f) != nullptr;
  pclose(f);
  if (!got) return {};
  std::string s(buf);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  return s;
}

std::string MapEngine(const std::string& engine) {
  std::string e = engine;
  for (char& c : e) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  if (e.find("ja") != std::string::npos || e.find("japanese") != std::string::npos ||
      e.find("mozc") != std::string::npos || e.find("anthy") != std::string::npos ||
      e.find("kotoeri") != std::string::npos)
    return kInputJa;
  if (e.find("hangul") != std::string::npos || e.find("korean") != std::string::npos ||
      e.find(":ko") != std::string::npos || e.find("libhangul") != std::string::npos)
    return kInputKo;
  if (e.find("zhuyin") != std::string::npos || e.find("cangjie") != std::string::npos ||
      e.find("quick") != std::string::npos || e.find("zh-hant") != std::string::npos ||
      e.find("chewing") != std::string::npos)
    return kInputZhHant;
  if (e.find("pinyin") != std::string::npos || e.find("sunpinyin") != std::string::npos ||
      e.find("rime") != std::string::npos || e.find("zh-hans") != std::string::npos ||
      e.find("chinese") != std::string::npos || e.find("libpinyin") != std::string::npos)
    return kInputZhHans;
  if (e.find("vietnamese") != std::string::npos || e.find("telex") != std::string::npos ||
      e.find("vni") != std::string::npos || e.find("unikey") != std::string::npos || e.find(":vi") != std::string::npos)
    return kInputVi;
  if (e.find("thai") != std::string::npos || e.find(":th") != std::string::npos) return kInputTh;
  if (e.find("khmer") != std::string::npos || e.find(":km") != std::string::npos) return kInputKm;
  if (e.find("burmese") != std::string::npos || e.find("myanmar") != std::string::npos ||
      e.find(":my") != std::string::npos)
    return kInputMy;
  if (e.find("lao") != std::string::npos || e.find(":lo") != std::string::npos) return kInputLo;
  return kInputEn;
}

}  // namespace

std::string linux_active_input_language() {
  std::string engine = ReadCommand("ibus engine 2>/dev/null");
  if (engine.empty()) engine = ReadCommand("fcitx5-remote -n 2>/dev/null");
  if (engine.empty()) engine = ReadCommand("fcitx-remote -n 2>/dev/null");
  if (engine.empty()) return kInputEn;
  return MapEngine(engine);
}

bool linux_is_japanese_input() { return linux_active_input_language() == kInputJa; }

}  // namespace imeaura
