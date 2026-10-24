#include "prfitem.h"

#include <windows.h>

#include <chrono>
#include <fstream>

#include "clashwin/config/constants.h"
#include "profiles.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"
#include "util/uuid.h"
#include "yaml_serde.h"

namespace clashwin::config::prfitem {

using namespace clashwin::config::consts;

std::string gen_uid_prefixed(std::string_view prefix) {
  return util::gen_uid(prefix);
}

int64_t now_timestamp() {
  return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

std::string file_name_for(const std::string& uid, bool is_script) {
  return is_script ? uid + ".js" : uid + ".yaml";
}

std::wstring profiles_dir() { return util::dirs::app_profiles_dir(); }

namespace {
// 首启时 profiles/ 目录尚不存在（← utils/init.rs 的目录初始化职责，
// 写入点就近保证父目录存在）
void ensure_parent_dir(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return;
  std::wstring parent = path.substr(0, slash);
  if (parent.empty()) return;
  CreateDirectoryW(parent.c_str(), nullptr);  // 已存在则无害
}
}  // namespace

Result<void> write_file_if_changed(const std::wstring& path,
                                   const std::string& content) {
  ensure_parent_dir(path);
  // ← profiles.rs:664-676：内容一致跳过写
  std::string existing;
  {
    std::ifstream f(path, std::ios::binary);
    if (f) {
      existing.assign((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
      if (existing == content) return Result<void>::ok();
    }
  }
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    Error e;
    e.code = 10;
    e.message = "failed to write to file \"" + utf::utf16_to_utf8(path) + "\"";
    return Result<void>::err(std::move(e));
  }
  f.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!f) {
    Error e;
    e.code = 11;
    e.message = "failed to write to file \"" + utf::utf16_to_utf8(path) + "\"";
    return Result<void>::err(std::move(e));
  }
  return Result<void>::ok();
}

// ---------- ensure_enhance_items（← prfitem.rs:167-202） ----------
Result<PrfOption> ensure_enhance_items(const std::optional<PrfOption>& option) {
  PrfOption out = option.value_or(PrfOption{});
  if (!out.merge) {
    PrfItem item = PrfItem::from_merge(std::nullopt);
    auto r = profiles::append_item_safe(item);
    if (!r.is_ok()) return Result<PrfOption>::err(r.error());
    out.merge = item.uid;
  }
  if (!out.script) {
    PrfItem item = PrfItem::from_script(std::nullopt);
    auto r = profiles::append_item_safe(item);
    if (!r.is_ok()) return Result<PrfOption>::err(r.error());
    out.script = item.uid;
  }
  if (!out.rules) {
    PrfItem item = PrfItem::from_rules();
    auto r = profiles::append_item_safe(item);
    if (!r.is_ok()) return Result<PrfOption>::err(r.error());
    out.rules = item.uid;
  }
  if (!out.proxies) {
    PrfItem item = PrfItem::from_proxies();
    auto r = profiles::append_item_safe(item);
    if (!r.is_ok()) return Result<PrfOption>::err(r.error());
    out.proxies = item.uid;
  }
  // groups 只读返回不创建（阻止生成死 g*.yaml 文件）
  return Result<PrfOption>::ok(std::move(out));
}

}  // namespace clashwin::config::prfitem

// ---------- PrfItem 成员实现 ----------
namespace clashwin::config {

using namespace consts;

const char* prf_type_to_string(PrfType t) {
  switch (t) {
    case PrfType::Remote: return "remote";
    case PrfType::Local: return "local";
    case PrfType::Merge: return "merge";
    case PrfType::Script: return "script";
    case PrfType::Rules: return "rules";
    case PrfType::Proxies: return "proxies";
    case PrfType::Groups: return "groups";
  }
  return "remote";
}

std::optional<PrfType> prf_type_from_string(const std::string& s) {
  if (s == "remote") return PrfType::Remote;
  if (s == "local") return PrfType::Local;
  if (s == "merge") return PrfType::Merge;
  if (s == "script") return PrfType::Script;
  if (s == "rules") return PrfType::Rules;
  if (s == "proxies") return PrfType::Proxies;
  if (s == "groups") return PrfType::Groups;
  return std::nullopt;
}

PrfType PrfItem::type() const {
  auto t = prf_type_from_string(itype.value_or(""));
  return t.value_or(PrfType::Remote);
}

void PrfItem::set_type(PrfType t) { itype = prf_type_to_string(t); }

// ---------- from_local（← prfitem.rs:239-279） ----------
Result<PrfItem> PrfItem::from_local(const std::string& name,
                                    const std::string& desc,
                                    std::optional<std::string> file_data,
                                    const std::optional<PrfOption>& option) {
  auto enhanced = prfitem::ensure_enhance_items(option);
  if (!enhanced.is_ok()) return Result<PrfItem>::err(enhanced.error());

  PrfItem item;
  item.uid = prfitem::gen_uid_prefixed("L");
  item.set_type(PrfType::Local);
  item.name = name;
  item.desc = desc;
  item.file = *item.uid + ".yaml";
  PrfOption opt = enhanced.value();
  // ← prfitem.rs:266-274：只保留 update_interval 与关联子项 uid
  opt.user_agent = std::nullopt;
  opt.with_proxy = std::nullopt;
  opt.self_proxy = std::nullopt;
  opt.timeout_seconds = std::nullopt;
  opt.danger_accept_invalid_certs = std::nullopt;
  opt.allow_auto_update = std::nullopt;
  item.option = std::move(opt);
  item.home = std::nullopt;
  item.updated = prfitem::now_timestamp();
  item.file_data = file_data.value_or(tmpl::kItemLocal);
  return Result<PrfItem>::ok(std::move(item));
}

// ---------- from_merge / from_script / from_rules / from_proxies ----------
PrfItem PrfItem::from_merge(std::optional<std::string> uid) {
  // ← prfitem.rs:614-630：uid 非空（全局保护件）用 ITEM_MERGE；
  //   新建用新 uid + ITEM_MERGE_EMPTY
  std::string id = uid.value_or(prfitem::gen_uid_prefixed("m"));
  PrfItem item;
  item.uid = id;
  item.set_type(PrfType::Merge);
  item.file = id + ".yaml";
  item.updated = prfitem::now_timestamp();
  item.file_data = uid ? std::string(tmpl::kItemMerge)
                       : std::string(tmpl::kItemMergeEmpty);
  return item;
}

PrfItem PrfItem::from_script(std::optional<std::string> uid) {
  std::string id = uid.value_or(prfitem::gen_uid_prefixed("s"));
  PrfItem item;
  item.uid = id;
  item.set_type(PrfType::Script);
  item.file = id + ".js";  // js 扩展名
  item.updated = prfitem::now_timestamp();
  item.file_data = std::string(tmpl::kItemScript);
  return item;
}

PrfItem PrfItem::from_rules() {
  std::string id = prfitem::gen_uid_prefixed("r");
  PrfItem item;
  item.uid = id;
  item.set_type(PrfType::Rules);
  item.file = id + ".yaml";
  item.updated = prfitem::now_timestamp();
  item.file_data = std::string(tmpl::kItemRules);
  return item;
}

PrfItem PrfItem::from_proxies() {
  std::string id = prfitem::gen_uid_prefixed("p");
  PrfItem item;
  item.uid = id;
  item.set_type(PrfType::Proxies);
  item.file = id + ".yaml";
  item.updated = prfitem::now_timestamp();
  item.file_data = std::string(tmpl::kItemProxies);
  return item;
}

// ---------- from_url（T03 占位：下载/SSRF/三级解析 → T06/T15） ----------
Result<PrfItem> PrfItem::from_url(const std::string& url,
                                  const std::optional<std::string>& name,
                                  const std::optional<std::string>& desc,
                                  const std::optional<PrfOption>& option) {
  (void)name;
  (void)desc;
  (void)option;
  Error e;
  e.code = 20;
  e.message = "from_url requires network layer (T06/T15), url: " + url;
  return Result<PrfItem>::err(std::move(e));
}

// ---------- read_file / save_file_data（← prfitem.rs:682-716） ----------
std::optional<std::wstring> PrfItem::file_path() const {
  if (!file) return std::nullopt;
  return prfitem::profiles_dir() + L"\\" + utf::utf8_to_utf16(*file);
}

Result<std::string> PrfItem::read_file() const {
  if (!file) {
    Error e;
    e.code = 12;
    e.message = "could not find the file";
    return Result<std::string>::err(std::move(e));
  }
  auto p = file_path();
  if (!p) {
    Error e;
    e.code = 12;
    e.message = "could not find the file";
    return Result<std::string>::err(std::move(e));
  }
  std::ifstream f(*p, std::ios::binary);
  if (!f) {
    Error e;
    e.code = 13;
    e.message = "failed to read the file \"" + utf::utf16_to_utf8(*p) + "\"";
    return Result<std::string>::err(std::move(e));
  }
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  return Result<std::string>::ok(std::move(content));
}

Result<void> PrfItem::save_file_data(const std::string& data) const {
  if (!file) {
    Error e;
    e.code = 12;
    e.message = "could not find the file";
    return Result<void>::err(std::move(e));
  }
  auto p = file_path();
  if (!p) {
    Error e;
    e.code = 12;
    e.message = "could not find the file";
    return Result<void>::err(std::move(e));
  }
  // ← prfitem.rs:700-709：内容（CRLF 归一后）一致则跳过写
  std::string existing;
  bool exists = false;
  {
    std::ifstream f(*p, std::ios::binary);
    if (f) {
      exists = true;
      existing.assign((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    }
  }
  auto normalize_crlf = [](std::string s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
      out += s[i];
    }
    return out;
  };
  if (exists && existing == data) return Result<void>::ok();
  if (exists && normalize_crlf(existing) == normalize_crlf(data))
    return Result<void>::ok();
  prfitem::ensure_parent_dir(*p);
  std::ofstream f(*p, std::ios::binary | std::ios::trunc);
  if (!f) {
    Error e;
    e.code = 14;
    e.message = "failed to save the file \"" + utf::utf16_to_utf8(*p) + "\"";
    return Result<void>::err(std::move(e));
  }
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!f) {
    Error e;
    e.code = 14;
    e.message = "failed to save the file \"" + utf::utf16_to_utf8(*p) + "\"";
    return Result<void>::err(std::move(e));
  }
  return Result<void>::ok();
}

// ---------- 序列化（字段顺序 = prfitem.rs 声明顺序；skip 语义见 models.h） ----------
namespace {

YAML::Node opt_str_node(const std::optional<std::string>& v) {
  return v ? serde::str_node(*v) : serde::null_node();
}

}  // namespace

YAML::Node PrfSelected::to_yaml() const {
  YAML::Node m(YAML::NodeType::Map);
  m["name"] = opt_str_node(name);
  m["now"] = opt_str_node(now);
  return m;
}

std::optional<PrfSelected> PrfSelected::from_yaml(const YAML::Node& m) {
  if (!m || !m.IsMap()) return std::nullopt;
  PrfSelected s;
  s.name = serde::opt_str(m, "name");
  s.now = serde::opt_str(m, "now");
  return s;
}

YAML::Node PrfExtra::to_yaml() const {
  YAML::Node m(YAML::NodeType::Map);
  serde::set_i64(m, "upload", static_cast<int64_t>(upload));
  serde::set_i64(m, "download", static_cast<int64_t>(download));
  serde::set_i64(m, "total", static_cast<int64_t>(total));
  serde::set_i64(m, "expire", static_cast<int64_t>(expire));
  return m;
}

std::optional<PrfExtra> PrfExtra::from_yaml(const YAML::Node& m) {
  if (!m || !m.IsMap()) return std::nullopt;
  PrfExtra e;
  e.upload = static_cast<uint64_t>(serde::opt_i64(m, "upload").value_or(0));
  e.download = static_cast<uint64_t>(serde::opt_i64(m, "download").value_or(0));
  e.total = static_cast<uint64_t>(serde::opt_i64(m, "total").value_or(0));
  e.expire = static_cast<uint64_t>(serde::opt_i64(m, "expire").value_or(0));
  return e;
}

std::optional<PrfOption> PrfOption::merge_opt(
    const std::optional<PrfOption>& one, const std::optional<PrfOption>& other) {
  // ← prfitem.rs:128-152
  if (one && other) {
    PrfOption r = *one;
    if (other->user_agent) r.user_agent = other->user_agent;
    if (other->with_proxy) r.with_proxy = other->with_proxy;
    if (other->self_proxy) r.self_proxy = other->self_proxy;
    if (other->danger_accept_invalid_certs)
      r.danger_accept_invalid_certs = other->danger_accept_invalid_certs;
    if (other->allow_auto_update) r.allow_auto_update = other->allow_auto_update;
    if (other->update_interval) r.update_interval = other->update_interval;
    if (other->merge) r.merge = other->merge;
    if (other->script) r.script = other->script;
    if (other->rules) r.rules = other->rules;
    if (other->proxies) r.proxies = other->proxies;
    if (other->groups) r.groups = other->groups;
    if (other->timeout_seconds) r.timeout_seconds = other->timeout_seconds;
    return r;
  }
  if (one) return one;
  if (other) return other;
  return std::nullopt;
}

YAML::Node PrfOption::to_yaml() const {
  YAML::Node m(YAML::NodeType::Map);
  // skip_serializing_if 字段：None 省键
  if (user_agent) m["user_agent"] = *user_agent;
  if (with_proxy) m["with_proxy"] = *with_proxy ? "true" : "false";
  if (self_proxy) m["self_proxy"] = *self_proxy ? "true" : "false";
  if (update_interval)
    serde::set_i64(m, "update_interval", static_cast<int64_t>(*update_interval));
  if (timeout_seconds)
    serde::set_i64(m, "timeout_seconds", static_cast<int64_t>(*timeout_seconds));
  if (danger_accept_invalid_certs)
    m["danger_accept_invalid_certs"] = *danger_accept_invalid_certs ? "true" : "false";
  if (allow_auto_update)
    m["allow_auto_update"] = *allow_auto_update ? "true" : "false";
  // merge/script/rules/proxies/groups 无 skip：None → null
  m["merge"] = opt_str_node(merge);
  m["script"] = opt_str_node(script);
  m["rules"] = opt_str_node(rules);
  m["proxies"] = opt_str_node(proxies);
  m["groups"] = opt_str_node(groups);
  return m;
}

std::optional<PrfOption> PrfOption::from_yaml(const YAML::Node& m) {
  if (!m || !m.IsMap()) return std::nullopt;
  PrfOption o;
  o.user_agent = serde::opt_str(m, "user_agent");
  o.with_proxy = serde::opt_bool(m, "with_proxy");
  o.self_proxy = serde::opt_bool(m, "self_proxy");
  o.update_interval = serde::opt_u64(m, "update_interval");
  o.timeout_seconds = serde::opt_u64(m, "timeout_seconds");
  o.danger_accept_invalid_certs =
      serde::opt_bool(m, "danger_accept_invalid_certs");
  o.allow_auto_update = serde::opt_bool(m, "allow_auto_update");
  o.merge = serde::opt_str(m, "merge");
  o.script = serde::opt_str(m, "script");
  o.rules = serde::opt_str(m, "rules");
  o.proxies = serde::opt_str(m, "proxies");
  o.groups = serde::opt_str(m, "groups");
  return o;
}

YAML::Node PrfItem::to_yaml() const {
  YAML::Node m(YAML::NodeType::Map);
  m["uid"] = opt_str_node(uid);
  m["type"] = opt_str_node(itype);
  m["name"] = opt_str_node(name);
  m["file"] = opt_str_node(file);
  if (desc) m["desc"] = *desc;
  if (url) m["url"] = *url;
  if (selected) {
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& s : *selected) seq.push_back(s.to_yaml());
    m["selected"] = seq;
  }
  if (extra) m["extra"] = extra->to_yaml();
  updated ? serde::set_i64(m, "updated", *updated)
          : serde::set_null(m, "updated");
  if (option) m["option"] = option->to_yaml();
  if (home) m["home"] = *home;
  // file_data：serde skip，永不落盘
  return m;
}

std::optional<PrfItem> PrfItem::from_yaml(const YAML::Node& m) {
  if (!m || !m.IsMap()) return std::nullopt;
  PrfItem item;
  item.uid = serde::opt_str(m, "uid");
  item.itype = serde::opt_str(m, "type");
  item.name = serde::opt_str(m, "name");
  item.file = serde::opt_str(m, "file");
  item.desc = serde::opt_str(m, "desc");
  item.url = serde::opt_str(m, "url");
  {
    const YAML::Node sel = m["selected"];
    if (sel && sel.IsSequence()) {
      std::vector<PrfSelected> vec;
      for (const auto& it : sel) {
        auto s = PrfSelected::from_yaml(it);
        if (s) vec.push_back(std::move(*s));
      }
      item.selected = std::move(vec);
    }
  }
  item.extra = PrfExtra::from_yaml(m["extra"]);
  item.updated = serde::opt_i64(m, "updated");
  item.option = PrfOption::from_yaml(m["option"]);
  item.home = serde::opt_str(m, "home");
  return item;
}

}  // namespace clashwin::config
