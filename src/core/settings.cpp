#include "core/settings.h"

#include "core/json.h"
#include "core/tokens.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace imeaura {
namespace {

Rgba color_from_json(const json::Value* v, Rgba fallback) {
  if (!v || v->type != json::Value::Type::Array || v->array_items.size() != 4) return fallback;
  Rgba c = fallback;
  try {
    for (int i = 0; i < 4; ++i) {
      const auto& item = v->array_items[static_cast<size_t>(i)];
      if (item.kind != json::Value::ArrayItem::Number) return fallback;
      int n = static_cast<int>(item.number);
      if (n < 0 || n > 255) return fallback;
      switch (i) {
        case 0: c.r = static_cast<uint8_t>(n); break;
        case 1: c.g = static_cast<uint8_t>(n); break;
        case 2: c.b = static_cast<uint8_t>(n); break;
        case 3: c.a = static_cast<uint8_t>(n); break;
      }
    }
  } catch (...) {
    return fallback;
  }
  return c;
}

std::string normalize_display_mode(const std::string& v) {
  if (v == kDisplayModeAlways || v == kDisplayModeOnFocus || v == kDisplayModeHidden) return v;
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

Settings default_settings() { return Settings{}; }

std::string normalize_language(const std::string& v) {
  if (v == kLangEn) return v;
  return kLangJa;
}

Settings normalize_settings(const Settings& raw) {
  Settings s = raw;
  s.display_mode = normalize_display_mode(s.display_mode);
  s.ui_font_size = normalize_font_size(s.ui_font_size);
  s.gradient_width = clamp_width(s.gradient_width);
  s.language = normalize_language(s.language);
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
  if (const auto* v = root.find("color_jp")) s.color_jp = color_from_json(v, s.color_jp);
  if (const auto* v = root.find("color_en")) s.color_en = color_from_json(v, s.color_en);
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
  if (const auto* v = root.find("language")) {
    if (v->type == json::Value::Type::String) s.language = v->string_value;
  }
  out = normalize_settings(s);
  return true;
}

bool save_settings(const Settings& settings) {
  Settings s = normalize_settings(settings);
  const auto dir = config_dir();
  std::error_code ec;
  fs::create_directories(dir, ec);
  json::Value root = json::Value::make_object({
      {"color_jp",
       json::Value::make_array(
           std::vector<json::Value::ArrayItem>{
               {json::Value::ArrayItem::Number, static_cast<double>(s.color_jp.r), {}},
               {json::Value::ArrayItem::Number, static_cast<double>(s.color_jp.g), {}},
               {json::Value::ArrayItem::Number, static_cast<double>(s.color_jp.b), {}},
               {json::Value::ArrayItem::Number, static_cast<double>(s.color_jp.a), {}}})},
      {"color_en",
       json::Value::make_array(
           std::vector<json::Value::ArrayItem>{
               {json::Value::ArrayItem::Number, static_cast<double>(s.color_en.r), {}},
               {json::Value::ArrayItem::Number, static_cast<double>(s.color_en.g), {}},
               {json::Value::ArrayItem::Number, static_cast<double>(s.color_en.b), {}},
               {json::Value::ArrayItem::Number, static_cast<double>(s.color_en.a), {}}})},
      {"display_mode", json::Value::make_string(s.display_mode)},
      {"show_on_hover", json::Value::make_bool(s.show_on_hover)},
      {"ui_font_size", json::Value::make_string(s.ui_font_size)},
      {"gradient_width", json::Value::make_number(static_cast<double>(s.gradient_width))},
      {"language", json::Value::make_string(s.language)},
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
