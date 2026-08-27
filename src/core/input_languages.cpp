#include "core/input_languages.h"

#include <algorithm>

namespace imeaura {
namespace {

constexpr InputLanguageInfo kCatalog[] = {
    {kInputJa, L"\u65E5\u672C\u8A9E", L"Japanese"},
    {kInputEn, L"English", L"English"},
    {kInputZhHans, L"\u7B80\u4F53\u4E2D\u6587", L"Chinese (Simplified)"},
    {kInputZhHant, L"\u7E41\u9AD4\u4E2D\u6587", L"Chinese (Traditional)"},
    {kInputKo, L"\uD55C\uAD6D\uC5B4", L"Korean"},
    {kInputVi, L"Ti\u1EBFng Vi\u1EC7t", L"Vietnamese"},
    {kInputTh, L"\u0E44\u0E17\u0E22", L"Thai"},
    {kInputKm, L"\u1781\u17D2\u1798\u17B6\u179F\u17B6\u17C6\u1781\u17D2\u1798\u17B6\u179A", L"Khmer"},
    {kInputMy, L"\u1019\u103C\u1014\u103A\u1039\u1019\u1031\u101C\u103A\u1038\u1019\u103D\u1014\u103A", L"Myanmar (Burmese)"},
    {kInputLo, L"\u0E9E\u0EB2\u0EAA\u0EB2\u0EA5\u0EB2\u0EA7", L"Lao"},
};

constexpr InputLanguageInfo kUiCatalog[] = {
    {kInputJa, L"\u65E5\u672C\u8A9E", L"Japanese"},
    {kInputEn, L"English", L"English"},
    {kInputZhHans, L"\u7B80\u4F53\u4E2D\u6587", L"Chinese (Simplified)"},
    {kInputZhHant, L"\u7E41\u9AD4\u4E2D\u6587", L"Chinese (Traditional)"},
    {kInputKo, L"\uD55C\uAD6D\uC5B4", L"Korean"},
};

}  // namespace

const InputLanguageInfo* input_language_catalog(size_t& count) {
  count = sizeof(kCatalog) / sizeof(kCatalog[0]);
  return kCatalog;
}

const InputLanguageInfo* ui_language_catalog(size_t& count) {
  count = sizeof(kUiCatalog) / sizeof(kUiCatalog[0]);
  return kUiCatalog;
}

bool is_known_input_language(std::string_view id) {
  size_t n = 0;
  const auto* cat = input_language_catalog(n);
  for (size_t i = 0; i < n; ++i) {
    if (id == cat[i].id) return true;
  }
  return false;
}

bool is_ui_language(std::string_view id) {
  size_t n = 0;
  const auto* cat = ui_language_catalog(n);
  for (size_t i = 0; i < n; ++i) {
    if (id == cat[i].id) return true;
  }
  return false;
}

std::string normalize_input_language(std::string_view id) {
  if (is_known_input_language(id)) return std::string(id);
  return kInputEn;
}

std::string normalize_ui_language(std::string_view id) {
  if (is_ui_language(id)) return std::string(id);
  return kInputJa;
}

const wchar_t* input_language_display_name(std::string_view id, bool prefer_native) {
  size_t n = 0;
  const auto* cat = input_language_catalog(n);
  for (size_t i = 0; i < n; ++i) {
    if (id == cat[i].id) return prefer_native ? cat[i].native_name : cat[i].english_name;
  }
  return L"?";
}

std::vector<std::string> unused_input_languages(const std::vector<std::string>& used) {
  size_t n = 0;
  const auto* cat = input_language_catalog(n);
  std::vector<std::string> out;
  for (size_t i = 0; i < n; ++i) {
    const std::string id = cat[i].id;
    if (std::find(used.begin(), used.end(), id) == used.end()) out.push_back(id);
  }
  return out;
}

std::vector<std::string> aura_slot_language_choices(const std::vector<std::string>& used_by_others,
                                                    std::string_view current) {
  auto choices = unused_input_languages(used_by_others);
  const auto it = std::find(choices.begin(), choices.end(), current);
  if (it == choices.end()) {
    if (is_known_input_language(current)) choices.insert(choices.begin(), std::string(current));
  } else if (it != choices.begin()) {
    std::rotate(choices.begin(), it, it + 1);
  }
  return choices;
}

}  // namespace imeaura
