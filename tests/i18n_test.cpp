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
  const Lang langs[] = {Lang::Ja, Lang::En, Lang::ZhHans, Lang::ZhHant, Lang::Ko};

  for (Lang lang : langs) {
    for (size_t i = 0; i < count; ++i) {
      const auto id = static_cast<StringId>(i);
      const wchar_t* s = tr(lang, id);
      EXPECT(s != nullptr && s[0] != L'\0');
    }
  }

  EXPECT(lang_from_key("ja") == Lang::Ja);
  EXPECT(lang_from_key("en") == Lang::En);
  EXPECT(lang_from_key("zh-Hans") == Lang::ZhHans);
  EXPECT(lang_from_key("zh-Hant") == Lang::ZhHant);
  EXPECT(lang_from_key("ko") == Lang::Ko);
  EXPECT(lang_from_key("fr") == Lang::En);

  EXPECT(lang_font_family(Lang::Ja) != nullptr);
  EXPECT(lang_font_family(Lang::En) != nullptr);
  EXPECT(lang_font_family(Lang::Ko) != nullptr);

  EXPECT(string_id_for_ui_lang("zh-Hans") == StringId::kLangZhHans);

  if (failures) return 1;
  std::cout << "i18n_test: OK\n";
  return 0;
}
