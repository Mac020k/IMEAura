#include "platform/linux/linux_ime.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace imeaura {

bool linux_is_japanese_input() {
  const char* cmd =
      "ibus engine 2>/dev/null | grep -qiE 'ja|japanese|mozc|anthy|kotoeri' && echo 1 || echo 0";
  FILE* f = popen(cmd, "r");
  if (!f) return false;
  char buf[16]{};
  const bool got = fgets(buf, sizeof(buf), f) != nullptr;
  pclose(f);
  return got && std::strncmp(buf, "1", 1) == 0;
}

}  // namespace imeaura
