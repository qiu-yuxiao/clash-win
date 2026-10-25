#include "prfitem.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>

#include "clashwin/config/constants.h"
#include "clash.h"
#include "config/ssrf.h"
#include "enhance/uri_list.h"
#include "profiles.h"
#include "system/net_http.h"
#include "util/base64.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "util/uuid.h"
#include "verge.h"
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

// ---------- from_url / from_url_direct / parse_subscription_content（T06） ----------
namespace {

using clashwin::enhance::parse_uri_list;
using clashwin::system::HttpOptions;
using clashwin::system::ProxyType;
using clashwin::system::parse_str_u64;

// ← serde_yaml Value::as_u64().unwrap_or(0)：非数字/负数/浮点 → 0
uint64_t yaml_port_u64(const YAML::Node& proxy) {
  const YAML::Node v = proxy["port"];
  if (!v || !v.IsScalar()) return 0;
  const std::string& s = v.Scalar();
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos) {
    return 0;
  }
  uint64_t val = 0;
  bool overflow = false;
  for (char c : s) {
    uint64_t d = static_cast<uint64_t>(c - '0');
    if (val > (UINT64_MAX - d) / 10) {
      overflow = true;
      break;
    }
    val = val * 10 + d;
  }
  return overflow ? 0 : val;
}

// ← p.get("name").and_then(|v| v.as_str()).unwrap_or("")
std::string yaml_str_or_empty(const YAML::Node& proxy, const char* key) {
  const YAML::Node v = proxy[key];
  if (v && v.IsScalar()) return v.Scalar();
  return "";
}

// ← chrono::Local::now().timestamp()
// （now_timestamp 已提供；此处的写文件工具 ↓）
Result<void> plain_write_file(const std::wstring& path,
                              const std::string& content) {
  // ← std::fs::write（无条件覆盖；父目录就近保证存在）
  size_t slash = path.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    std::wstring parent = path.substr(0, slash);
    // 逐级创建（CreateDirectoryW 已存在无害）
    for (size_t i = 0; i < parent.size(); ++i) {
      if (parent[i] == L'\\' || parent[i] == L'/') {
        CreateDirectoryW(parent.substr(0, i).c_str(), nullptr);
      }
    }
    CreateDirectoryW(parent.c_str(), nullptr);
  }
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    Error e;
    e.code = 10;
    e.message =
        "failed to write to file \"" + utf::utf16_to_utf8(path) + "\"";
    return Result<void>::err(std::move(e));
  }
  f.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!f) {
    Error e;
    e.code = 10;
    e.message =
        "failed to write to file \"" + utf::utf16_to_utf8(path) + "\"";
    return Result<void>::err(std::move(e));
  }
  return Result<void>::ok();
}

// ← PrfOption { update_interval/merge/script/rules/proxies/groups/
//   allow_auto_update, ..Default::default() }（危险选项不落盘）
PrfOption clean_download_option(PrfOption opt) {
  opt.user_agent = std::nullopt;
  opt.with_proxy = std::nullopt;
  opt.self_proxy = std::nullopt;
  opt.timeout_seconds = std::nullopt;
  opt.danger_accept_invalid_certs = std::nullopt;
  return opt;
}

// ← ProxyType::Localhost 端口回退链：verge_mixed_port → clash mixed-port
std::optional<uint16_t> resolve_localhost_mixed_port() {
  auto v = verge::load();
  if (v.is_ok() && v.value().verge_mixed_port) {
    return *v.value().verge_mixed_port;
  }
  auto c = clash::load();
  return c.get_mixed_port();
}

}  // namespace

// ← prfitem.rs::from_url_direct（281-422）
Result<PrfItem> PrfItem::from_url_direct(
    const std::string& url, const std::optional<std::string>& name,
    const std::optional<PrfOption>& option) {
  namespace enhance = clashwin::enhance;
  const std::string url_trimmed = util::trim(url);
  const std::string uid_str = "L_Direct_Imports";
  const std::string file_name = "L_Direct_Imports.yaml";
  const std::wstring path =
      prfitem::profiles_dir() + L"\\" + utf::utf8_to_utf16(file_name);

  YAML::Node final_mapping(YAML::NodeType::Map);
  final_mapping["proxies"] = YAML::Node(YAML::NodeType::Sequence);

  auto enhanced = prfitem::ensure_enhance_items(option);
  if (!enhanced.is_ok()) return Result<PrfItem>::err(enhanced.error());
  PrfOption opt = clean_download_option(enhanced.value());

  // clear / clean：无条件清空文件（← std::fs::write）
  if (util::to_lower(url_trimmed) == "clear" ||
      util::to_lower(url_trimmed) == "clean") {
    std::string serialized = serde::dump(final_mapping);
    auto w = plain_write_file(path, serialized);
    if (!w.is_ok()) return Result<PrfItem>::err(w.error());

    PrfItem item;
    item.uid = uid_str;
    item.set_type(PrfType::Local);
    item.name = name.value_or("本地导入节点");
    item.desc = "0";
    item.file = file_name;
    item.updated = prfitem::now_timestamp();
    item.option = opt;
    item.file_data = serialized;
    return Result<PrfItem>::ok(std::move(item));
  }

  auto parsed = parse_uri_list(url);
  if (!parsed) {
    Error e;
    e.code = 41;
    e.message = "无法解析直接输入的节点配置，解析结果为空。请检查输入格式。";
    return Result<PrfItem>::err(std::move(e));
  }
  final_mapping = *parsed;

  // 与已有 L_Direct_Imports.yaml 按 name+server+port 去重合并
  if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
    std::ifstream f(path, std::ios::binary);
    if (f) {
      std::string content((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
      YAML::Node existing_mapping;
      try {
        existing_mapping = YAML::Load(content);
      } catch (const YAML::Exception&) {
        existing_mapping = YAML::Node();
      }
      if (existing_mapping && existing_mapping.IsMap()) {
        YAML::Node existing_proxies = existing_mapping["proxies"];
        if (existing_proxies && existing_proxies.IsSequence()) {
          YAML::Node new_proxies = final_mapping["proxies"];
          if (new_proxies && new_proxies.IsSequence()) {
            for (const auto& new_p : new_proxies) {
              const std::string new_name = yaml_str_or_empty(new_p, "name");
              const std::string new_server =
                  yaml_str_or_empty(new_p, "server");
              const uint64_t new_port = yaml_port_u64(new_p);

              std::optional<size_t> existing_pos;
              for (size_t i = 0; i < existing_proxies.size(); ++i) {
                const YAML::Node& p = existing_proxies[i];
                const std::string p_name = yaml_str_or_empty(p, "name");
                const std::string p_server = yaml_str_or_empty(p, "server");
                const uint64_t p_port = yaml_port_u64(p);
                if (p_name == new_name && p_server == new_server &&
                    p_port == new_port) {
                  existing_pos = i;
                  break;
                }
              }
              if (existing_pos) {
                existing_proxies[*existing_pos] = YAML::Node(new_p);
              } else {
                existing_proxies.push_back(YAML::Node(new_p));
              }
            }
            final_mapping = existing_mapping;
          }
        }
      }
    }
  }

  std::string serialized = serde::dump(final_mapping);
  uint64_t count = 0;
  if (final_mapping["proxies"] && final_mapping["proxies"].IsSequence()) {
    count = final_mapping["proxies"].size();
  }

  PrfItem item;
  item.uid = uid_str;
  item.set_type(PrfType::Local);
  item.name = name.value_or("本地导入节点");
  item.desc = std::to_string(count);
  item.file = file_name;
  item.updated = prfitem::now_timestamp();
  item.option = opt;
  item.file_data = serialized;
  return Result<PrfItem>::ok(std::move(item));
}

// ← prfitem.rs::from_url（427-610）
Result<PrfItem> PrfItem::from_url(const std::string& url,
                                  const std::optional<std::string>& name,
                                  const std::optional<std::string>& desc,
                                  const std::optional<PrfOption>& option) {
  // 直连节点输入（非 HTTP(S) URL）→ 委托给 from_url_direct
  std::string url_lower =
      util::to_lower(util::trim(url));
  if (!util::starts_with(url_lower, "http://") &&
      !util::starts_with(url_lower, "https://")) {
    return from_url_direct(url, name, option);
  }

  const bool with_proxy =
      option && option->with_proxy.value_or(false);
  const bool self_proxy =
      option && option->self_proxy.value_or(false);
  const bool accept_invalid_certs =
      option && option->danger_accept_invalid_certs.value_or(false);
  const std::optional<bool> allow_auto_update =
      option ? option->allow_auto_update : std::nullopt;
  const std::optional<std::string> user_agent =
      option ? option->user_agent : std::nullopt;
  std::optional<uint64_t> update_interval =
      option ? option->update_interval : std::nullopt;
  const uint64_t timeout =
      (option && option->timeout_seconds) ? *option->timeout_seconds : 20;

  auto enhanced = prfitem::ensure_enhance_items(option);
  if (!enhanced.is_ok()) return Result<PrfItem>::err(enhanced.error());
  PrfOption opt = enhanced.value();
  opt.allow_auto_update = allow_auto_update;
  opt = clean_download_option(std::move(opt));

  // 选择代理类型（← network.rs ProxyType）
  ProxyType proxy_type = self_proxy ? ProxyType::Localhost
                         : with_proxy ? ProxyType::System
                                      : ProxyType::None;

  // 修复脏 URL（query 参数误挂 path）
  auto fixed = util::fix_dirty_url(url);
  if (!fixed.is_ok()) return Result<PrfItem>::err(fixed.error());
  const std::string fixed_url = fixed.value();

  // SSRF 防护：禁止访问内网地址
  auto parts = clashwin::system::parse_http_url(fixed_url);
  if (!parts.is_ok()) return Result<PrfItem>::err(parts.error());
  auto ssrf = clashwin::config::ssrf::validate_url_no_ssrf(parts.value());
  if (!ssrf.is_ok()) return Result<PrfItem>::err(ssrf.error());

  // 记录危险选项日志
  if (accept_invalid_certs) {
    CWLOG_WARN(
        "⚠️ 订阅使用了危险选项 `danger_accept_invalid_certs=true`，TLS 证书验证"
        "被跳过！");
  }

  HttpOptions http_opts;
  http_opts.proxy_type = proxy_type;
  http_opts.timeout_seconds = timeout;
  if (user_agent) http_opts.user_agent = user_agent;
  http_opts.accept_invalid_certs = accept_invalid_certs;
  http_opts.localhost_mixed_port = resolve_localhost_mixed_port;

  auto resp = clashwin::system::http_get(fixed_url, http_opts);
  if (!resp.is_ok()) {
    Sleep(100);  // ← tokio::time::sleep(Duration::from_millis(100))
    Error e;
    e.code = 42;
    e.message = "failed to fetch remote profile: " + resp.error().message;
    return Result<PrfItem>::err(std::move(e));
  }
  const clashwin::system::HttpResponse& r = resp.value();
  if (!r.is_success()) {
    Error e;
    e.code = 43;
    e.message = "failed to fetch remote profile with status " +
                std::to_string(r.status);
    return Result<PrfItem>::err(std::move(e));
  }

  // parse the Subscription UserInfo（兼容 x-amz-meta- 等前缀）
  std::optional<PrfExtra> extra;
  for (const auto& h : r.headers) {
    std::string key_lower = util::to_lower(h.name);
    if (!clashwin::system::is_subscription_userinfo_header(key_lower)) {
      continue;
    }
    PrfExtra ex;
    ex.upload = parse_str_u64(h.value, "upload").value_or(0);
    ex.download = parse_str_u64(h.value, "download").value_or(0);
    ex.total = parse_str_u64(h.value, "total").value_or(0);
    ex.expire = parse_str_u64(h.value, "expire").value_or(0);
    extra = ex;
    break;
  }

  // parse the Content-Disposition
  std::optional<std::string> filename;
  if (const std::string* cd = r.header("Content-Disposition")) {
    filename =
        clashwin::system::parse_content_disposition_filename(*cd);
  } else {
    // ← 无 Content-Disposition：URL 末段解码（help.rs::get_last_part_and_decode
    //   恒返回 Some，可能为空串）
    filename = clashwin::system::get_last_part_and_decode(fixed_url);
  }

  // profile-update-interval：时 → 分（option 值优先）
  if (!update_interval) {
    if (const std::string* v = r.header("profile-update-interval")) {
      update_interval =
          clashwin::system::parse_profile_update_interval(*v);
    }
  }
  opt.update_interval = update_interval;

  std::optional<std::string> home;
  if (const std::string* v = r.header("profile-web-page-url")) {
    home = *v;
  }

  const std::string uid = prfitem::gen_uid_prefixed("R");
  const std::string file = uid + ".yaml";
  const std::string final_name =
      name ? *name
           : (filename ? *filename : std::string("Remote File"));

  // process the charset "UTF-8 with BOM"（反复剥前导 U+FEFF）
  std::string data = r.body;
  while (data.size() >= 3 &&
         static_cast<unsigned char>(data[0]) == 0xEF &&
         static_cast<unsigned char>(data[1]) == 0xBB &&
         static_cast<unsigned char>(data[2]) == 0xBF) {
    data.erase(0, 3);
  }

  // YAML 大小限制防止 DoS
  if (data.size() > consts::kSubscriptionMaxBytes) {
    Error e;
    e.code = 44;
    e.message = "subscription content exceeds maximum allowed size (50MB)";
    return Result<PrfItem>::err(std::move(e));
  }

  auto sub = parse_subscription_content(data);
  if (!sub.is_ok()) return Result<PrfItem>::err(sub.error());

  // groups is skipped to prevent creating dead g*.yaml files
  PrfItem item;
  item.uid = uid;
  item.set_type(PrfType::Remote);
  item.name = final_name;
  item.desc = desc;
  item.file = file;
  item.url = fixed_url;
  item.extra = extra;
  item.updated = prfitem::now_timestamp();
  item.option = opt;
  item.home = home;
  item.file_data = sub.value().second;
  return Result<PrfItem>::ok(std::move(item));
}

// ← prfitem.rs::parse_subscription_content（752-785）
Result<std::pair<YAML::Node, std::string>> parse_subscription_content(
    const std::string& data) {
  namespace enhance = clashwin::enhance;
  auto contains_proxies_key = [](const YAML::Node& y) {
    return y && y.IsMap() &&
           (static_cast<bool>(y["proxies"]) ||
            static_cast<bool>(y["proxy-providers"]));
  };

  // 整体先做 Base64 解码（UTF-8 合法才可用）
  std::optional<std::string> decoded_str_opt;
  if (auto decoded = clashwin::util::decode_base64_robust(data)) {
    std::string s(reinterpret_cast<const char*>(decoded->data()),
                  decoded->size());
    if (clashwin::util::is_valid_utf8(s)) decoded_str_opt = std::move(s);
  }

  // ① data 本身是含 proxies/proxy-providers 的 YAML mapping
  {
    YAML::Node y;
    try {
      y = YAML::Load(data);
    } catch (const YAML::Exception&) {
      y = YAML::Node();
    }
    if (contains_proxies_key(y)) {
      return Result<std::pair<YAML::Node, std::string>>::ok({y, data});
    }
  }

  // ② Base64 解码后为该形态 YAML
  if (decoded_str_opt) {
    YAML::Node y;
    try {
      y = YAML::Load(*decoded_str_opt);
    } catch (const YAML::Exception&) {
      y = YAML::Node();
    }
    if (contains_proxies_key(y)) {
      return Result<std::pair<YAML::Node, std::string>>::ok(
          {y, *decoded_str_opt});
    }
  }

  // ③ URI-list
  const std::string& parse_content =
      decoded_str_opt ? *decoded_str_opt : data;
  if (auto parsed = enhance::parse_uri_list(parse_content)) {
    std::string serialized = serde::dump(*parsed);
    return Result<std::pair<YAML::Node, std::string>>::ok(
        {*parsed, std::move(serialized)});
  }

  Error e;
  e.code = 45;
  e.message =
      "订阅链接内容格式错误，既不是合法的 YAML 配置文件，也无法解析为节点链接列表";
  return Result<std::pair<YAML::Node, std::string>>::err(std::move(e));
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
