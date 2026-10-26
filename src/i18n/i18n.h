#pragma once
// I18n（T10）：13 语言 × 10 命名空间加载 + t(key, args) 插值。
// 对照 src/services/i18n.ts（i18next）语义：
// - 归一 resolve_language：小写、_→-、zh-tw→zhtw、zh-cn→zh、base('-' 前段)
//   命中则用 base，否则 fallback=zh
// - fallback 两级：key 在当前语言缺失 → zh → key 原文（i18next 缺省行为）
// - 插值 {{name}}；热切换：set_language → change 回调（UI invalidate）
// - 语言持久化（localStorage verge-language 等价物）归 T03/T17（patchVerge）
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace clashwin::i18n {

class I18n {
 public:
  static I18n& instance();

  // locales_dir：{dir}/{lang}/{ns}.json；全量预载 13 语言（纯 JSON，开销小）
  // lang 为初始语言（自动归一）。失败返回 false（缺文件等）。
  bool init(const std::wstring& locales_dir, const std::string& lang = "zh");

  const std::string& language() const { return language_; }
  bool inited() const { return inited_; }
  size_t loaded_language_count() const { return resources_.size(); }

  // 归一（i18n.ts resolveLanguage 逐条对照；不依赖 init）
  static std::string resolve_language(const std::string& raw);
  static const std::vector<std::string>& supported_languages();
  static constexpr const char* kFallback = "zh";
  static constexpr const char* kStorageKey = "verge-language";

  // t("ns.key", args)："." 首段为命名空间；插值 {{name}}；
  // fallback：当前语言 → zh → key 原文（返回 UTF-8）
  std::string t(const std::string& ns_dot_key,
                const std::map<std::string, std::string>& args = {}) const;

  // 热切换：归一 → 设当前语言 → 触发 change 回调；返回是否受支持
  bool set_language(const std::string& raw);

  // 切换订阅（UI invalidate；返回 id 供退订）
  uint64_t subscribe(std::function<void(const std::string& lang)> cb);
  void unsubscribe(uint64_t id);

  // 检查（测试用）：某语言某命名空间是否载入
  bool has_bundle(const std::string& lang, const std::string& ns) const;

 private:
  I18n() = default;

  bool load_language(const std::wstring& dir, const std::string& lang);
  // 资源：lang → ns → key → value
  std::map<std::string,
           std::map<std::string, std::map<std::string, std::string>>>
      resources_;
  std::string language_ = kFallback;
  bool inited_ = false;
  std::vector<std::pair<uint64_t, std::function<void(const std::string&)>>>
      listeners_;
  uint64_t next_id_ = 1;
};

}  // namespace clashwin::i18n
