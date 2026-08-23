#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace imeaura {

inline constexpr const char* kInputJa = "ja";
inline constexpr const char* kInputEn = "en";
inline constexpr const char* kInputZhHans = "zh-Hans";
inline constexpr const char* kInputZhHant = "zh-Hant";
inline constexpr const char* kInputKo = "ko";

inline constexpr int kMaxAuraSlots = 7;
inline constexpr int kMinAuraSlots = 2;

struct InputLanguageInfo {
  const char* id;
  const wchar_t* native_name;
  const wchar_t* english_name;
};

const InputLanguageInfo* input_language_catalog(size_t& count);
bool is_known_input_language(std::string_view id);
bool is_ui_language(std::string_view id);
std::string normalize_input_language(std::string_view id);
std::string normalize_ui_language(std::string_view id);
const wchar_t* input_language_display_name(std::string_view id, bool prefer_native);
std::vector<std::string> unused_input_languages(const std::vector<std::string>& used);

}  // namespace imeaura
