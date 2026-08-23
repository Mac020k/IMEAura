#include "core/input_languages.h"

#include <cassert>
#include <string>
#include <vector>

using namespace imeaura;

int main() {
  {
    const std::vector<std::string> used = {kInputEn};
    auto choices = aura_slot_language_choices(used, kInputJa);
    assert(!choices.empty());
    assert(choices.front() == kInputJa);
    int ja_count = 0;
    for (const auto& id : choices) {
      if (id == kInputJa) ++ja_count;
      assert(id != kInputEn);
    }
    assert(ja_count == 1);
  }

  {
    // Current already unused among others - must not duplicate when prepending.
    const std::vector<std::string> used = {kInputZhHant, kInputKo};
    auto choices = aura_slot_language_choices(used, kInputJa);
    int ja_count = 0;
    for (const auto& id : choices) {
      if (id == kInputJa) ++ja_count;
    }
    assert(ja_count == 1);
    assert(choices.front() == kInputJa);
  }

  return 0;
}
