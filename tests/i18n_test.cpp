#include "core/i18n.h"

#include <cstddef>
#include <iostream>

using namespace imeaura;

static int failures = 0;
#define EXPECT(cond)                                                     \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << "FAIL: " #cond " (" __FILE__ ":" << __LINE__ << ")\n"; \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

int main() {
  const auto count = static_cast<size_t>(StringId::kCount);

  for (size_t i = 0; i < count; ++i) {
    const auto id = static_cast<StringId>(i);
    const wchar_t* ja = tr(Lang::Ja, id);
    const wchar_t* en = tr(Lang::En, id);
    EXPECT(ja != nullptr && ja[0] != L'\0');
    EXPECT(en != nullptr && en[0] != L'\0');
  }

  EXPECT(lang_from_key("ja") == Lang::Ja);
  EXPECT(lang_from_key("en") == Lang::En);
  EXPECT(lang_from_key("fr") == Lang::Ja);

  EXPECT(lang_font_family(Lang::Ja) != nullptr);
  EXPECT(lang_font_family(Lang::En) != nullptr);

  if (failures) return 1;
  std::cout << "i18n_test: OK\n";
  return 0;
}
