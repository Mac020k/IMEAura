#include "platform/macos/mac_ime.h"

#include "core/input_languages.h"

#include <Carbon/Carbon.h>

#include <string>

namespace imeaura {
namespace {

std::string CfStringToUtf8(CFStringRef s) {
  if (!s) return {};
  char buf[512]{};
  if (CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8)) return buf;
  return {};
}

std::string MapFromLanguages(CFStringRef langs) {
  if (!langs) return kInputEn;
  const std::string s = CfStringToUtf8(langs);
  if (s.find("ja") != std::string::npos) return kInputJa;
  if (s.find("ko") != std::string::npos) return kInputKo;
  if (s.find("zh-Hans") != std::string::npos || s.find("zh_CN") != std::string::npos) return kInputZhHans;
  if (s.find("zh-Hant") != std::string::npos || s.find("zh_TW") != std::string::npos ||
      s.find("zh_HK") != std::string::npos)
    return kInputZhHant;
  if (s.find("zh") != std::string::npos) return kInputZhHans;
  if (s.find("vi") != std::string::npos) return kInputVi;
  if (s.find("th") != std::string::npos) return kInputTh;
  if (s.find("km") != std::string::npos) return kInputKm;
  if (s.find("my") != std::string::npos) return kInputMy;
  if (s.find("lo") != std::string::npos) return kInputLo;
  return kInputEn;
}

std::string MapFromSourceId(const std::string& id) {
  const auto lower = [&] {
    std::string t = id;
    for (char& c : t) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return t;
  }();
  if (lower.find("japanese") != std::string::npos || lower.find(".ja") != std::string::npos) return kInputJa;
  if (lower.find("korean") != std::string::npos || lower.find(".ko") != std::string::npos) return kInputKo;
  if (lower.find("simplified") != std::string::npos || lower.find("pinyin") != std::string::npos ||
      lower.find("zh-hans") != std::string::npos)
    return kInputZhHans;
  if (lower.find("traditional") != std::string::npos || lower.find("zhuyin") != std::string::npos ||
      lower.find("cangjie") != std::string::npos || lower.find("zh-hant") != std::string::npos)
    return kInputZhHant;
  if (lower.find("chinese") != std::string::npos || lower.find("zh.") != std::string::npos) return kInputZhHans;
  if (lower.find("vietnamese") != std::string::npos || lower.find("telex") != std::string::npos ||
      lower.find("vni") != std::string::npos || lower.find(".vi") != std::string::npos)
    return kInputVi;
  if (lower.find("thai") != std::string::npos || lower.find(".th") != std::string::npos) return kInputTh;
  if (lower.find("khmer") != std::string::npos || lower.find(".km") != std::string::npos) return kInputKm;
  if (lower.find("burmese") != std::string::npos || lower.find("myanmar") != std::string::npos ||
      lower.find(".my") != std::string::npos)
    return kInputMy;
  if (lower.find("lao") != std::string::npos || lower.find(".lo") != std::string::npos) return kInputLo;
  return kInputEn;
}

}  // namespace

std::string mac_active_input_language() {
  TISInputSourceRef src = TISCopyCurrentKeyboardInputSource();
  if (!src) return kInputEn;
  CFStringRef langs =
      static_cast<CFStringRef>(TISGetInputSourceProperty(src, kTISPropertyInputSourceLanguages));
  std::string mapped = MapFromLanguages(langs);
  if (mapped == kInputEn) {
    CFStringRef sid =
        static_cast<CFStringRef>(TISGetInputSourceProperty(src, kTISPropertyInputSourceID));
    mapped = MapFromSourceId(CfStringToUtf8(sid));
  }
  CFRelease(src);
  return mapped;
}

bool mac_is_japanese_input() { return mac_active_input_language() == kInputJa; }

}  // namespace imeaura
