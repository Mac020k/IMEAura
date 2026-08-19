#include "core/overlay_layout.h"

#include <iostream>

using namespace imeaura;

#define EXPECT(cond)                                                     \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " #cond \
                << "\n";                                                 \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

int main() {
  int failures = 0;

  EXPECT(clamp_gradient_thickness(15, 1920, 1080) == 15);
  EXPECT(clamp_gradient_thickness(500, 800, 600) == 300);
  EXPECT(clamp_gradient_thickness(100, 800, 600) == 100);
  EXPECT(clamp_gradient_thickness(1, 100, 50) == 1);
  EXPECT(clamp_gradient_thickness(0, 800, 600) == 1);
  EXPECT(clamp_gradient_thickness(50, 80, 60) == 30);

  return failures == 0 ? 0 : 1;
}
