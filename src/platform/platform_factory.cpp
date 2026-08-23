#include "platform/backend.h"
#include "platform/firefly_backend.h"

#ifdef _WIN32
#include "platform/windows/win_platform.h"
#include "platform/windows/win_firefly.h"
#elif defined(__APPLE__)
#include "platform/macos/mac_platform.h"
#include "platform/macos/mac_firefly.h"
#else
#include "platform/linux/linux_platform.h"
#include "platform/linux/linux_firefly.h"
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

std::unique_ptr<FireflyBackend> create_firefly_backend() {
#ifdef _WIN32
  return std::make_unique<WinFireflyBackend>();
#elif defined(__APPLE__)
  return std::make_unique<MacFireflyBackend>();
#else
  return std::make_unique<LinuxFireflyBackend>();
#endif
}

}  // namespace imeaura
