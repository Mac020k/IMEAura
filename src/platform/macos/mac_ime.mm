#include "platform/macos/mac_ime.h"

#include <Carbon/Carbon.h>

#include <string>

namespace imeaura {

bool mac_is_japanese_input() {
  TISInputSourceRef src = TISCopyCurrentKeyboardInputSource();
  if (!src) return false;
  CFStringRef langs =
      static_cast<CFStringRef>(TISGetInputSourceProperty(src, kTISPropertyInputSourceLanguages));
  bool jp = false;
  if (langs && CFGetTypeID(langs) == CFStringGetTypeID()) {
    char buf[256];
    if (CFStringGetCString(langs, buf, sizeof(buf), kCFStringEncodingUTF8)) {
      const std::string s(buf);
      jp = s.find("ja") != std::string::npos || s.find("Japanese") != std::string::npos;
    }
  }
  CFRelease(src);
  return jp;
}

}  // namespace imeaura
