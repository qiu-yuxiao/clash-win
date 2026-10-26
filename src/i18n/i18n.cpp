// I18n 实现（T10）：13 语言 JSON 加载 + fallback/归一/插值/热切换。
// 语义对照 ClashVerge src/services/i18n.ts（只读参考）。
#include "i18n.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

#include "util/log.h"
#include "util/utf.h"

namespace clashwin::i18n {

namespace fs = std::filesystem;
namespace utf = clashwin::utf;

inline std::wstring u8w(const std::string& s) { return utf::utf8_to_utf16(s); }
inline std::string w8(const std::wstring& s) { return utf::utf16_to_utf8(s); }

// 13 语言（i18n.ts supportedLanguages 顺序）
const std::vector<std::string>& I18n::supported_languages() {
  static const std::vector<std::string> kLangs = {
      "zh", "en", "ru", "fa", "tt", "id", "ar",
      "ko", "tr", "de", "es", "jp", "zhtw"};
  return kLangs;
}

// 10 命名空间（每语言 10 个 JSON + index.ts 聚合；tests 仅测试资源）
namespace {
const std::vector<std::string>& namespaces() {
  static const std::vector<std::string> kNs = {
      "connections", "home", "layout", "logs", "profiles",
      "proxies", "rules", "settings", "shared", "tests"};
  return kNs;
}

std::string to_lower_ascii(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}
}  // namespace

I18n& I18n::instance() {
  static I18n i;
  return i;
}

std::string I18n::resolve_language(const std::string& raw) {
  // normalizeLanguage：小写 + _ → -
  std::string normalized = to_lower_ascii(raw);
  for (char& c : normalized)
    if (c == '_') c = '-';

  if (normalized.empty()) return kFallback;

  if (normalized == "zh-tw") return "zhtw";
  if (normalized == "zh-cn") return "zh";

  const auto& supported = supported_languages();
  if (std::find(supported.begin(), supported.end(), normalized) !=
      supported.end())
    return normalized;

  // base 命中（"en-us" → "en"）
  std::string base = normalized.substr(0, normalized.find('-'));
  if (std::find(supported.begin(), supported.end(), base) != supported.end())
    return base;

  return kFallback;
}

bool I18n::load_language(const std::wstring& dir, const std::string& lang) {
  bool any = false;
  // 先装临时容器，成功才入 resources_（失败不留空条目污染计数）
  std::map<std::string, std::map<std::string, std::string>> bundles;
  for (const std::string& ns : namespaces()) {
    fs::path p = fs::path(dir) / u8w(lang) /
                 (ns + ".json");
    std::error_code ec;
    if (!fs::exists(p, ec)) continue;
    std::ifstream f(p, std::ios::binary);
    if (!f) continue;
    try {
      nlohmann::json j = nlohmann::json::parse(
          std::string(std::istreambuf_iterator<char>(f),
                      std::istreambuf_iterator<char>()));
      auto& flat = bundles[ns];      if (j.is_object()) {
        // 递归拍平：{"a":{"b":{"c":"v"}}} → "a.b.c"="v"（原版 i18next 层级 key）
        std::function<void(const nlohmann::json&, const std::string&)> walk =
            [&](const nlohmann::json& obj, const std::string& prefix) {
              for (auto it = obj.begin(); it != obj.end(); ++it) {
                std::string k = prefix.empty() ? it.key() : prefix + "." + it.key();
                if (it.value().is_string()) {
                  flat[k] = it.value().get<std::string>();
                } else if (it.value().is_object()) {
                  walk(it.value(), k);
                }
              }
            };
        walk(j, "");
        any = true;
      }
    } catch (const std::exception& e) {
      CWLOG_WARN("I18n: 解析 " + w8(p.wstring()) +
                 " 失败: " + e.what());
    }
  }
  if (any) resources_[lang] = std::move(bundles);
  return any;
}

bool I18n::init(const std::wstring& locales_dir, const std::string& lang) {
  resources_.clear();
  bool all_ok = true;
  for (const std::string& l : supported_languages()) {
    if (!load_language(locales_dir, l)) {
      CWLOG_WARN("I18n: 语言 " + l + " 加载无内容（目录 " +
                 w8(locales_dir) + "）");
      all_ok = false;
    }
  }
  language_ = resolve_language(lang);
  inited_ = true;
  CWLOG_INFO("I18n: 已加载 " + std::to_string(resources_.size()) +
             " 语言，当前 " + language_);
  return inited_ && all_ok;
}

std::string I18n::t(const std::string& ns_dot_key,
                    const std::map<std::string, std::string>& args) const {
  // "ns.key[.sub]"：首段命名空间
  size_t dot = ns_dot_key.find('.');
  std::string ns = dot == std::string::npos ? ns_dot_key
                                            : ns_dot_key.substr(0, dot);
  std::string key = dot == std::string::npos ? "" : ns_dot_key.substr(dot + 1);

  const std::string* found = nullptr;
  for (const std::string& lang : {language_, std::string(kFallback)}) {
    auto lit = resources_.find(lang);
    if (lit == resources_.end()) continue;
    auto nit = lit->second.find(ns);
    if (nit == lit->second.end()) continue;
    auto kit = nit->second.find(key);
    if (kit != nit->second.end()) {
      found = &kit->second;
      break;
    }
  }
  // i18next 缺省：缺失显示 key 原文
  std::string out = found ? *found : ns_dot_key;

  // 插值 {{name}}
  if (!args.empty() && out.find("{{") != std::string::npos) {
    std::string res;
    res.reserve(out.size());
    for (size_t i = 0; i < out.size();) {
      if (i + 1 < out.size() && out[i] == '{' && out[i + 1] == '{') {
        size_t end = out.find("}}", i + 2);
        if (end != std::string::npos) {
          std::string name = out.substr(i + 2, end - i - 2);
          // 去空白
          size_t b = name.find_first_not_of(" \t");
          size_t e2 = name.find_last_not_of(" \t");
          if (b != std::string::npos) name = name.substr(b, e2 - b + 1);
          auto a = args.find(name);
          res += a != args.end() ? a->second : out.substr(i, end + 2 - i);
          i = end + 2;
          continue;
        }
      }
      res += out[i++];
    }
    out = std::move(res);
  }
  return out;
}

bool I18n::set_language(const std::string& raw) {
  // 不受支持的语言：拒绝切换（resolve 会折叠到 zh，不能让垃圾输入生效）。
  // 判据：resolve 结果落到 fallback 且归一值本身不是 zh。
  std::string normalized = to_lower_ascii(raw);
  for (char& c : normalized)
    if (c == '_') c = '-';
  std::string target = resolve_language(raw);
  if (target == kFallback && normalized != "zh") return false;
  bool changed = target != language_;
  language_ = target;
  if (changed) {
    CWLOG_INFO("I18n: 语言切换 → " + target);
    for (auto& [id, cb] : listeners_) cb(target);
  }
  return true;
}

uint64_t I18n::subscribe(std::function<void(const std::string&)> cb) {
  uint64_t id = next_id_++;
  listeners_.emplace_back(id, std::move(cb));
  return id;
}

void I18n::unsubscribe(uint64_t id) {
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                  [id](const auto& p) { return p.first == id; }),
                   listeners_.end());
}

bool I18n::has_bundle(const std::string& lang, const std::string& ns) const {
  auto lit = resources_.find(lang);
  if (lit == resources_.end()) return false;
  auto nit = lit->second.find(ns);
  return nit != lit->second.end() && !nit->second.empty();
}

}  // namespace clashwin::i18n
