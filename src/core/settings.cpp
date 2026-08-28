#include "core/settings.h"

#include "core/json.h"
#include "core/tokens.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace imeaura {
namespace {

Rgba color_from_json(const json::Value* v, Rgba fallback) {
  if (!v || v->type != json::Value::Type::Array || v->elements.size() != 4) return fallback;
  Rgba c = fallback;
  for (int i = 0; i < 4; ++i) {
    const auto& item = v->elements[static_cast<size_t>(i)];
    if (item.type != json::Value::Type::Number) return fallback;
    int n = static_cast<int>(item.number_value);
    if (n < 0 || n > 255) return fallback;
    switch (i) {
      case 0:
        c.r = static_cast<uint8_t>(n);
        break;
      case 1:
        c.g = static_cast<uint8_t>(n);
        break;
      case 2:
        c.b = static_cast<uint8_t>(n);
        break;
      case 3:
        c.a = static_cast<uint8_t>(n);
        break;
    }
  }
  return c;
}

json::Value color_to_json(const Rgba& c) {
  return json::Value::make_array({
      json::Value::make_number(static_cast<double>(c.r)),
      json::Value::make_number(static_cast<double>(c.g)),
      json::Value::make_number(static_cast<double>(c.b)),
      json::Value::make_number(static_cast<double>(c.a)),
  });
}

std::string normalize_display_mode(const std::string& v) {
  if (v == kDisplayModeAlways || v == kDisplayModeOnFocus) return v;
  return kDisplayModeAlways;
}

std::string normalize_font_size(const std::string& v) {
  if (v == kFontSizeSmall || v == kFontSizeMedium || v == kFontSizeLarge) return v;
  return kFontSizeMedium;
}

int clamp_width(int w) {
  if (w < kGradientWidthMin) return kGradientWidthMin;
  if (w > kGradientWidthMax) return kGradientWidthMax;
  return w;
}

std::vector<AuraColorSlot> default_aura_slots() {
  return {
      {kInputJa, kDefaultColorJp},
      {kInputEn, kDefaultColorEn},
  };
}

std::vector<AuraColorSlot> normalize_aura_slots(std::vector<AuraColorSlot> slots) {
  std::vector<AuraColorSlot> out;
  std::unordered_set<std::string> seen;
  for (auto& s : slots) {
    if (!is_known_input_language(s.lang_id)) continue;
    if (seen.count(s.lang_id)) continue;
    seen.insert(s.lang_id);
    out.push_back(std::move(s));
    if (static_cast<int>(out.size()) >= kMaxAuraSlots) break;
  }
  if (out.empty()) return default_aura_slots();
  if (static_cast<int>(out.size()) < kMinAuraSlots) {
    for (const auto& d : default_aura_slots()) {
      if (seen.count(d.lang_id)) continue;
      out.push_back(d);
      seen.insert(d.lang_id);
      if (static_cast<int>(out.size()) >= kMinAuraSlots) break;
    }
  }
  return out;
}

#ifdef _WIN32
fs::path config_dir() {
  const char* appdata = std::getenv("APPDATA");
  fs::path base = appdata ? fs::path(appdata) : fs::path();
  return base / "IMEAura";
}
#elif defined(__APPLE__)
fs::path config_dir() {
  const char* home = std::getenv("HOME");
  return fs::path(home ? home : "") / "Library" / "Application Support" / "IMEAura";
}
#else
fs::path config_dir() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) return fs::path(xdg) / "ime_aura";
  const char* home = std::getenv("HOME");
  return fs::path(home ? home : "") / ".config" / "ime_aura";
}
#endif

}  // namespace

Settings default_settings() {
  Settings s;
  s.aura_slots = default_aura_slots();
  return s;
}

Rgba Settings::color_for_lang(std::string_view lang_id) const {
  for (const auto& slot : aura_slots) {
    if (slot.lang_id == lang_id) return slot.color;
  }
  for (const auto& slot : aura_slots) {
    if (slot.lang_id == kInputEn) return slot.color;
  }
  if (!aura_slots.empty()) return aura_slots.front().color;
  return kDefaultColorEn;
}

Rgba Settings::default_color_for_new_slot(size_t existing_count) const {
  if (existing_count < 2) {
    return existing_count == 0 ? kDefaultColorJp : kDefaultColorEn;
  }
  const size_t idx = existing_count - 2;
  if (idx < 5) return kDefaultAuraSlotColors[idx];
  return kDefaultAuraSlotColors[4];
}

std::string normalize_language(const std::string& v) { return normalize_ui_language(v); }

std::string normalize_led_mode(const std::string& v) {
  if (v == kFireflyLedHid || v == kFireflyLedNone) return v;
  return kFireflyLedAuto;
}

std::string normalize_caps_mode(const std::string& v) {
  if (v == kFireflyCapsPreserve || v == kFireflyCapsLowercase) return v;
  return kFireflyCapsUppercase;
}

Settings normalize_settings(const Settings& raw) {
  Settings s = raw;
  s.aura_slots = normalize_aura_slots(std::move(s.aura_slots));
  // Migrate legacy display_mode=hidden → aura_enabled=false.
  if (s.display_mode == kDisplayModeHidden) {
    s.aura_enabled = false;
    s.display_mode = kDisplayModeAlways;
  }
  s.display_mode = normalize_display_mode(s.display_mode);
  s.ui_font_size = normalize_font_size(s.ui_font_size);
  s.gradient_width = clamp_width(s.gradient_width);
  s.language = normalize_language(s.language);
  s.firefly_led_mode = normalize_led_mode(s.firefly_led_mode);
  s.firefly_caps_mode = normalize_caps_mode(s.firefly_caps_mode);
  s.firefly_busy_action = normalize_busy_action(s.firefly_busy_action);
  if (s.firefly_busy_action != kFireflyBusyKeepAwake) s.firefly_keep_display_on = false;
  if (s.firefly_custom_vk < 0) s.firefly_custom_vk = 0;
  if (s.firefly_custom_vk > 0xFE) s.firefly_custom_vk = 0xFE;
  if (s.display_mode != kDisplayModeOnFocus) s.show_on_hover = false;
  return s;
}

std::string settings_path() { return (config_dir() / "settings.json").string(); }

bool load_settings(Settings& out) {
  const auto path = settings_path();
  if (!fs::exists(path)) {
    out = default_settings();
    return true;
  }
  std::ifstream f(path);
  if (!f) {
    out = default_settings();
    return false;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  json::Value root;
  std::string err;
  if (!json::parse(ss.str(), root, err) || root.type != json::Value::Type::Object) {
    out = default_settings();
    return false;
  }
  Settings s = default_settings();
  bool has_aura = false;
  if (const auto* v = root.find("aura_colors")) {
    if (v->type == json::Value::Type::Array) {
      std::vector<AuraColorSlot> slots;
      for (const auto& item : v->elements) {
        if (item.type != json::Value::Type::Object) continue;
        const auto* lang = item.find("lang");
        const auto* color = item.find("color");
        if (!lang || lang->type != json::Value::Type::String) continue;
        AuraColorSlot slot;
        slot.lang_id = lang->string_value;
        slot.color = color_from_json(color, kDefaultColorEn);
        slots.push_back(std::move(slot));
      }
      if (!slots.empty()) {
        s.aura_slots = std::move(slots);
        has_aura = true;
      }
    }
  }
  if (!has_aura) {
    Rgba jp = kDefaultColorJp;
    Rgba en = kDefaultColorEn;
    if (const auto* v = root.find("color_jp")) jp = color_from_json(v, jp);
    if (const auto* v = root.find("color_en")) en = color_from_json(v, en);
    s.aura_slots = {{kInputJa, jp}, {kInputEn, en}};
  }
  if (const auto* v = root.find("aura_enabled")) {
    if (v->type == json::Value::Type::Bool) s.aura_enabled = v->bool_value;
  }
  if (const auto* v = root.find("display_mode")) {
    if (v->type == json::Value::Type::String) s.display_mode = v->string_value;
  }
  if (const auto* v = root.find("show_on_hover")) {
    if (v->type == json::Value::Type::Bool) s.show_on_hover = v->bool_value;
  }
  if (const auto* v = root.find("ui_font_size")) {
    if (v->type == json::Value::Type::String) s.ui_font_size = v->string_value;
  }
  if (const auto* v = root.find("gradient_width")) {
    if (v->type == json::Value::Type::Number) s.gradient_width = static_cast<int>(v->number_value);
  }
  if (const auto* v = root.find("firefly_enabled")) {
    if (v->type == json::Value::Type::Bool) s.firefly_enabled = v->bool_value;
  }
  if (const auto* v = root.find("firefly_led_mode")) {
    if (v->type == json::Value::Type::String) s.firefly_led_mode = v->string_value;
  }
  if (const auto* v = root.find("firefly_caps_mode")) {
    if (v->type == json::Value::Type::String) s.firefly_caps_mode = v->string_value;
  }
  if (const auto* v = root.find("firefly_busy_action")) {
    if (v->type == json::Value::Type::String) s.firefly_busy_action = v->string_value;
  }
  if (const auto* v = root.find("firefly_keep_display_on")) {
    if (v->type == json::Value::Type::Bool) s.firefly_keep_display_on = v->bool_value;
  }
  if (const auto* v = root.find("firefly_custom_vk")) {
    if (v->type == json::Value::Type::Number) s.firefly_custom_vk = static_cast<int>(v->number_value);
  }
  if (const auto* v = root.find("language")) {
    if (v->type == json::Value::Type::String) s.language = v->string_value;
  }
  if (const auto* v = root.find("easy_quit")) {
    if (v->type == json::Value::Type::Bool) s.easy_quit = v->bool_value;
  }
  out = normalize_settings(s);
  return true;
}

bool save_settings(const Settings& settings) {
  Settings s = normalize_settings(settings);
  const auto dir = config_dir();
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::vector<json::Value> aura_arr;
  aura_arr.reserve(s.aura_slots.size());
  for (const auto& slot : s.aura_slots) {
    aura_arr.push_back(json::Value::make_object({
        {"lang", json::Value::make_string(slot.lang_id)},
        {"color", color_to_json(slot.color)},
    }));
  }
  Rgba jp = kDefaultColorJp;
  Rgba en = kDefaultColorEn;
  for (const auto& slot : s.aura_slots) {
    if (slot.lang_id == kInputJa) jp = slot.color;
    if (slot.lang_id == kInputEn) en = slot.color;
  }
  json::Value root = json::Value::make_object({
      {"aura_colors", json::Value::make_array(std::move(aura_arr))},
      {"color_jp", color_to_json(jp)},
      {"color_en", color_to_json(en)},
      {"aura_enabled", json::Value::make_bool(s.aura_enabled)},
      {"display_mode", json::Value::make_string(s.display_mode)},
      {"show_on_hover", json::Value::make_bool(s.show_on_hover)},
      {"ui_font_size", json::Value::make_string(s.ui_font_size)},
      {"gradient_width", json::Value::make_number(static_cast<double>(s.gradient_width))},
      {"firefly_enabled", json::Value::make_bool(s.firefly_enabled)},
      {"firefly_led_mode", json::Value::make_string(s.firefly_led_mode)},
      {"firefly_caps_mode", json::Value::make_string(s.firefly_caps_mode)},
      {"firefly_busy_action", json::Value::make_string(s.firefly_busy_action)},
      {"firefly_keep_display_on", json::Value::make_bool(s.firefly_keep_display_on)},
      {"firefly_custom_vk", json::Value::make_number(static_cast<double>(s.firefly_custom_vk))},
      {"language", json::Value::make_string(s.language)},
      {"easy_quit", json::Value::make_bool(s.easy_quit)},
  });
  std::ofstream f(settings_path());
  if (!f) return false;
  f << json::stringify(root);
  return true;
}

int ui_font_point_size(const std::string& size_key) {
  const std::string k = normalize_font_size(size_key);
  if (k == kFontSizeSmall) return kFontPointSizeSmall;
  if (k == kFontSizeLarge) return kFontPointSizeLarge;
  return kFontPointSizeMedium;
}

}  // namespace imeaura
