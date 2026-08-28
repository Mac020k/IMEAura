#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace imeaura::json {

struct Value {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool bool_value = false;
  double number_value = 0;
  std::string string_value;
  std::vector<Value> elements;
  std::map<std::string, Value> object_entries;

  static Value make_bool(bool v);
  static Value make_number(double v);
  static Value make_string(std::string v);
  static Value make_array(std::vector<Value> items);
  static Value make_object(std::map<std::string, Value> entries);

  const Value* find(std::string_view key) const;
};

bool parse(std::string_view text, Value& out, std::string& error);
std::string stringify(const Value& value, int indent = 2);

}  // namespace imeaura::json
