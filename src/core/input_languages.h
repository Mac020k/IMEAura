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
inline constexpr const char* kInputVi = "vi";
inline constexpr const char* kInputTh = "th";
inline constexpr const char* kInputKm = "km";
inline constexpr const char* kInputMy = "my";
inline constexpr const char* kInputLo = "lo";

inline constexpr int kMaxAuraSlots = 7;
inline constexpr int kMinAuraSlots = 2;

struct InputLanguageInfo {
  const char* id;
  const wchar_t* native_name;
  const wchar_t* english_name;
};

const InputLanguageInfo* input_language_catalog(size_t& count);
const InputLanguageInfo* ui_language_catalog(size_t& count);
bool is_known_input_language(std::string_view id);
bool is_ui_language(std::string_view id);
std::string normalize_input_language(std::string_view id);
std::string normalize_ui_language(std::string_view id);
const wchar_t* input_language_display_name(std::string_view id, bool prefer_native);
std::vector<std::string> unused_input_languages(const std::vector<std::string>& used);
// Languages still free for a slot, with `current` first and never duplicated.
std::vector<std::string> aura_slot_language_choices(const std::vector<std::string>& used_by_others,
                                                    std::string_view current);

}  // namespace imeaura
