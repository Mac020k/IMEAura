#include "platform/backend.h"

#ifdef _WIN32
#include "platform/windows/win_platform.h"
#elif defined(__APPLE__)
#include "platform/macos/mac_platform.h"
#else
#include "platform/linux/linux_platform.h"
#endif

namespace imeaura {

std::unique_ptr<PlatformBackend> create_platform_backend() {
#ifdef _WIN32
  return std::make_unique<WinPlatformBackend>();
#elif defined(__APPLE__)
  return std::make_unique<MacPlatformBackend>();
#else
  return std::make_unique<LinuxPlatformBackend>();
#endif
}

}  // namespace imeaura
