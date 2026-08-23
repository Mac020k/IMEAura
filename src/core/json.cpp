#include "core/json.h"

#include <cctype>
#include <sstream>

namespace imeaura::json {

namespace {

void skip_ws(std::string_view s, size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

bool parse_value(std::string_view s, size_t& i, Value& out, std::string& error);

bool parse_string(std::string_view s, size_t& i, std::string& out) {
  if (i >= s.size() || s[i] != '"') return false;
  ++i;
  out.clear();
  while (i < s.size()) {
    char c = s[i++];
    if (c == '"') return true;
    if (c == '\\' && i < s.size()) {
      char e = s[i++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        default: out += e; break;
      }
    } else {
      out += c;
    }
  }
  return false;
}

bool parse_number(std::string_view s, size_t& i, double& out) {
  size_t start = i;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
  if (i < s.size() && s[i] == '.') {
    ++i;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
  }
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
  }
  auto part = s.substr(start, i - start);
  try {
    out = std::stod(std::string(part));
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_array(std::string_view s, size_t& i, Value& out, std::string& error) {
  if (s[i] != '[') return false;
  ++i;
  skip_ws(s, i);
  out = Value::make_array({});
  if (i < s.size() && s[i] == ']') {
    ++i;
    return true;
  }
  while (i < s.size()) {
    Value item;
    if (!parse_value(s, i, item, error)) return false;
    out.elements.push_back(std::move(item));
    skip_ws(s, i);
    if (i < s.size() && s[i] == ',') {
      ++i;
      skip_ws(s, i);
      continue;
    }
    if (i < s.size() && s[i] == ']') {
      ++i;
      return true;
    }
    error = "expected , or ]";
    return false;
  }
  return false;
}

bool parse_object(std::string_view s, size_t& i, Value& out, std::string& error) {
  if (s[i] != '{') return false;
  ++i;
  skip_ws(s, i);
  out = Value::make_object({});
  if (i < s.size() && s[i] == '}') {
    ++i;
    return true;
  }
  while (i < s.size()) {
    std::string key;
    if (!parse_string(s, i, key)) {
      error = "expected key";
      return false;
    }
    skip_ws(s, i);
    if (i >= s.size() || s[i] != ':') {
      error = "expected :";
      return false;
    }
    ++i;
    skip_ws(s, i);
    Value val;
    if (!parse_value(s, i, val, error)) return false;
    out.object_entries.emplace(std::move(key), std::move(val));
    skip_ws(s, i);
    if (i < s.size() && s[i] == ',') {
      ++i;
      skip_ws(s, i);
      continue;
    }
    if (i < s.size() && s[i] == '}') {
      ++i;
      return true;
    }
    error = "expected , or }";
    return false;
  }
  return false;
}

bool parse_value(std::string_view s, size_t& i, Value& out, std::string& error) {
  skip_ws(s, i);
  if (i >= s.size()) {
    error = "unexpected end";
    return false;
  }
  if (s[i] == '"') {
    out = Value::make_string("");
    return parse_string(s, i, out.string_value);
  }
  if (s[i] == '{') return parse_object(s, i, out, error);
  if (s[i] == '[') return parse_array(s, i, out, error);
  if (s.substr(i, 4) == "true") {
    out = Value::make_bool(true);
    i += 4;
    return true;
  }
  if (s.substr(i, 5) == "false") {
    out = Value::make_bool(false);
    i += 5;
    return true;
  }
  if (s.substr(i, 4) == "null") {
    out.type = Value::Type::Null;
    i += 4;
    return true;
  }
  double num = 0;
  if (parse_number(s, i, num)) {
    out = Value::make_number(num);
    return true;
  }
  error = "invalid token";
  return false;
}

}  // namespace

Value Value::make_bool(bool v) {
  Value x;
  x.type = Type::Bool;
  x.bool_value = v;
  return x;
}

Value Value::make_number(double v) {
  Value x;
  x.type = Type::Number;
  x.number_value = v;
  return x;
}

Value Value::make_string(std::string v) {
  Value x;
  x.type = Type::String;
  x.string_value = std::move(v);
  return x;
}

Value Value::make_array(std::vector<Value> items) {
  Value x;
  x.type = Type::Array;
  x.elements = std::move(items);
  return x;
}

Value Value::make_object(std::map<std::string, Value> entries) {
  Value x;
  x.type = Type::Object;
  x.object_entries = std::move(entries);
  return x;
}

const Value* Value::find(std::string_view key) const {
  if (type != Type::Object) return nullptr;
  const auto it = object_entries.find(std::string(key));
  if (it == object_entries.end()) return nullptr;
  return &it->second;
}

bool parse(std::string_view text, Value& out, std::string& error) {
  size_t i = 0;
  skip_ws(text, i);
  if (!parse_value(text, i, out, error)) return false;
  skip_ws(text, i);
  if (i != text.size()) {
    error = "trailing content";
    return false;
  }
  return true;
}

static void indent_stream(std::ostringstream& os, int n) {
  for (int j = 0; j < n; ++j) os << ' ';
}

static void stringify_impl(const Value& v, std::ostringstream& os, int depth);

static void stringify_array(const Value& v, std::ostringstream& os, int depth) {
  const bool nested = !v.elements.empty() && (v.elements.front().type == Value::Type::Object ||
                                              v.elements.front().type == Value::Type::Array);
  if (!nested) {
    os << '[';
    for (size_t i = 0; i < v.elements.size(); ++i) {
      if (i) os << ", ";
      stringify_impl(v.elements[i], os, depth);
    }
    os << ']';
    return;
  }
  os << "[\n";
  for (size_t i = 0; i < v.elements.size(); ++i) {
    indent_stream(os, depth + 2);
    stringify_impl(v.elements[i], os, depth + 2);
    os << (i + 1 < v.elements.size() ? ",\n" : "\n");
  }
  indent_stream(os, depth);
  os << ']';
}

static void stringify_object(const Value& v, std::ostringstream& os, int depth) {
  os << "{\n";
  size_t i = 0;
  for (const auto& [key, val] : v.object_entries) {
    indent_stream(os, depth + 2);
    os << '"' << key << "\": ";
    stringify_impl(val, os, depth + 2);
    os << (++i < v.object_entries.size() ? ",\n" : "\n");
  }
  indent_stream(os, depth);
  os << '}';
}

static void stringify_impl(const Value& v, std::ostringstream& os, int depth) {
  switch (v.type) {
    case Value::Type::Null:
      os << "null";
      break;
    case Value::Type::Bool:
      os << (v.bool_value ? "true" : "false");
      break;
    case Value::Type::Number:
      os << static_cast<int>(v.number_value);
      break;
    case Value::Type::String:
      os << '"' << v.string_value << '"';
      break;
    case Value::Type::Array:
      stringify_array(v, os, depth);
      break;
    case Value::Type::Object:
      stringify_object(v, os, depth);
      break;
  }
}

std::string stringify(const Value& value, int indent) {
  std::ostringstream os;
  stringify_impl(value, os, indent);
  os << '\n';
  return os.str();
}

}  // namespace imeaura::json
