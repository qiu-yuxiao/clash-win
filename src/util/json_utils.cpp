#include "util/json_utils.h"

#include "util/utf.h"

namespace clashwin::util {

int64_t get_or_int(const nlohmann::json& j, std::string_view key, int64_t def) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_number_integer()) return def;
  return it->get<int64_t>();
}

std::string get_or_string(const nlohmann::json& j, std::string_view key,
                          std::string_view def) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return std::string(def);
  return it->get<std::string>();
}

bool get_or_bool(const nlohmann::json& j, std::string_view key, bool def) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_boolean()) return def;
  return it->get<bool>();
}

std::optional<int64_t> try_get_int(const nlohmann::json& j,
                                   std::string_view key) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_number_integer()) return std::nullopt;
  return it->get<int64_t>();
}

std::optional<std::string> try_get_string(const nlohmann::json& j,
                                          std::string_view key) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return std::nullopt;
  return it->get<std::string>();
}

std::wstring get_or_wstring(const nlohmann::json& j, std::string_view key,
                            const std::wstring& def) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return def;
  // 合法空串同样返回空（仅 key 缺失/类型不符才回退默认值）
  return utf::utf8_to_utf16(it->get<std::string>());
}

}  // namespace clashwin::util
