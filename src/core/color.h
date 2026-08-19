#pragma once

#include <cstdint>

namespace imeaura {

struct Rgba {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;

  bool operator==(const Rgba& o) const {
    return r == o.r && g == o.g && b == o.b && a == o.a;
  }
};

}  // namespace imeaura
