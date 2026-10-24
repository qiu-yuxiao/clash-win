#include "unlock_cache.h"

#include <windows.h>

#include <fstream>
#include <map>

#include "clashwin/config/constants.h"
#include "util/dirs.h"
#include "util/str_utils.h"
#include "util/utf.h"

namespace clashwin::config::unlock_cache {

std::wstring cache_path() {
  return util::dirs::app_home_dir() + L"\\" + utf::utf8_to_utf16(consts::kUnlockCache);
}

namespace {

// ← unlock.tsx::normalizeUnlockName
std::string normalize_name(const std::string& name) {
  return util::to_lower(util::trim(name));
}

// ← unlock.tsx::getStatusPriority：Pending = 0（高），其余 = 1
int status_priority(const std::string& status) {
  return status == "Pending" ? 0 : 1;
}

// ← unlock.tsx::mergeOptionalFields
UnlockItem merge_optional(const UnlockItem& preferred, const UnlockItem& fallback) {
  UnlockItem m = preferred;
  if (!m.region) m.region = fallback.region;
  if (!m.check_time) m.check_time = fallback.check_time;
  return m;
}

bool read_file(const std::wstring& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign((std::istreambuf_iterator<char>(f)),
             std::istreambuf_iterator<char>());
  return true;
}

}  // namespace

std::vector<UnlockItem> dedupe_items(std::vector<UnlockItem> items) {
  // 保持首次出现顺序；同 name 键：优先级高者胜，平级用新项合并旧项可选字段
  std::map<std::string, size_t> index;  // 归一名 → items 下标
  std::vector<UnlockItem> out;
  for (auto& item : items) {
    std::string key = normalize_name(item.name);
    auto it = index.find(key);
    if (it == index.end()) {
      index[key] = out.size();
      out.push_back(std::move(item));
      continue;
    }
    UnlockItem& existing = out[it->second];
    int ep = status_priority(existing.status);
    int ip = status_priority(item.status);
    if (ip > ep) {
      existing = merge_optional(item, existing);
    } else if (ip < ep) {
      existing = merge_optional(existing, item);
    } else {
      existing = merge_optional(item, existing);
    }
  }
  return out;
}

namespace {

nlohmann::json item_to_json(const UnlockItem& it) {
  nlohmann::json j = nlohmann::json::object();
  j["name"] = it.name;
  j["status"] = it.status;
  j["region"] = it.region ? nlohmann::json(*it.region) : nlohmann::json(nullptr);
  j["check_time"] =
      it.check_time ? nlohmann::json(*it.check_time) : nlohmann::json(nullptr);
  return j;
}

std::optional<UnlockItem> item_from_json(const nlohmann::json& j) {
  if (!j.is_object()) return std::nullopt;
  if (!j.contains("name") || !j["name"].is_string()) return std::nullopt;
  if (!j.contains("status") || !j["status"].is_string()) return std::nullopt;
  UnlockItem it;
  it.name = j["name"].get<std::string>();
  it.status = j["status"].get<std::string>();
  if (j.contains("region") && j["region"].is_string())
    it.region = j["region"].get<std::string>();
  if (j.contains("check_time") && j["check_time"].is_string())
    it.check_time = j["check_time"].get<std::string>();
  return it;
}

}  // namespace

Result<void> save(const std::vector<UnlockItem>& items,
                  const std::optional<std::string>& time) {
  nlohmann::json j = nlohmann::json::object();
  j["time"] = time ? nlohmann::json(*time) : nlohmann::json(nullptr);
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& it : items) arr.push_back(item_to_json(it));
  j["items"] = std::move(arr);
  // pretty(2) + 尾换行（可读性优先；读取方容错）
  std::string body = j.dump(2) + "\n";
  std::wstring path = cache_path();
  size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    Error e;
    e.code = 40;
    e.message = "failed to get parent directory";
    return Result<void>::err(std::move(e));
  }
  std::wstring tmp = path + L".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      Error e;
      e.code = 41;
      e.message = "failed to write temp file \"" + utf::utf16_to_utf8(tmp) + "\"";
      return Result<void>::err(std::move(e));
    }
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!f) {
      Error e;
      e.code = 41;
      e.message = "failed to write temp file \"" + utf::utf16_to_utf8(tmp) + "\"";
      return Result<void>::err(std::move(e));
    }
  }
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    Error e;
    e.code = 42;
    e.message = "failed to save file \"" + utf::utf16_to_utf8(path) + "\"";
    return Result<void>::err(std::move(e));
  }
  return Result<void>::ok();
}

std::optional<UnlockCache> load() {
  std::string content;
  if (!read_file(cache_path(), content)) return std::nullopt;
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(content);
  } catch (...) {
    return std::nullopt;
  }
  if (!j.is_object() || !j.contains("items") || !j["items"].is_array())
    return std::nullopt;
  UnlockCache c;
  if (j.contains("time") && j["time"].is_string())
    c.time = j["time"].get<std::string>();
  for (const auto& it : j["items"]) {
    auto item = item_from_json(it);
    if (item) c.items.push_back(std::move(*item));
  }
  c.items = dedupe_items(std::move(c.items));
  return c;
}

}  // namespace clashwin::config::unlock_cache
