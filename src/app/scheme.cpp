#include "app/scheme.h"
#include "cmd/command_service.h"
#include "config/config.h"
#include "config/profiles.h"
#include "config/prfitem.h"
#include "core/switching_lock.h"
#include "events/event_bus.h"
#include "events/event_names.h"
#include "util/log.h"
#include "util/str_utils.h"

namespace clashwin::app {

namespace {

std::string_view strip_outer(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
    s.remove_suffix(1);
  }
  if (s.size() >= 4 && s.starts_with("[\"") && s.ends_with("\"]")) {
    s = s.substr(2, s.size() - 4);
  } else if (s.size() >= 2 && s.starts_with('[') && s.ends_with(']')) {
    s = s.substr(1, s.size() - 2);
  }
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

// ← scheme.rs:query_pairs().find(|(k, _)| k == key)
std::optional<std::string> extract_query_param(std::string_view query,
                                               std::string_view key) {
  size_t start = 0;
  while (start <= query.size()) {
    size_t amp = query.find('&', start);
    std::string_view seg = (amp == std::string_view::npos)
                               ? query.substr(start)
                               : query.substr(start, amp - start);
    size_t eq = seg.find('=');
    if (eq != std::string_view::npos) {
      std::string_view k = seg.substr(0, eq);
      std::string_view v = seg.substr(eq + 1);
      if (k == key) {
        return util::percent_decode(v);
      }
    }
    if (amp == std::string_view::npos) break;
    start = amp + 1;
  }
  return std::nullopt;
}

// ← utils/resolve/scheme.rs:54-88 extract_subscription_url（BUG-323）：
//   1) 标准/百分号编码 url 参数按 query_pairs 精确停在下一个 '&'（避免将 &name=、&silent= 等吞入订阅 URL）；
//   2) 未编码嵌套查询串（如 url=https://a.com/sub?token=1&flag=meta&name=Sub）保留嵌套参数并剥离外层 &name=/&silent=。
std::optional<std::string> extract_subscription_url(std::string_view query) {
  size_t pos = std::string_view::npos;
  if (const size_t amp = query.find("&url="); amp != std::string_view::npos) {
    pos = amp + 1;
  } else if (query.starts_with("url=")) {
    pos = 0;
  } else {
    return std::nullopt;
  }

  std::string after_prefix = util::trim(query.substr(pos + 4));
  if (after_prefix.empty()) return std::nullopt;

  const std::string lower = util::to_lower(after_prefix);
  const size_t first_amp = after_prefix.find('&');
  const std::string_view first_seg =
      (first_amp == std::string::npos)
          ? std::string_view(after_prefix)
          : std::string_view(after_prefix).substr(0, first_amp);
  const bool is_unencoded_nested_query =
      (lower.starts_with("http://") || lower.starts_with("https://")) &&
      (first_seg.find('?') != std::string_view::npos);

  if (!is_unencoded_nested_query) {
    auto val = extract_query_param(query, "url");
    if (!val) return std::nullopt;
    std::string trimmed = util::trim(*val);
    if (trimmed.empty()) return std::nullopt;
    return util::decode_subscription_url(trimmed);
  }

  std::string_view raw_url(after_prefix);
  for (std::string_view marker : {"&name=", "&silent="}) {
    if (const size_t idx = raw_url.find(marker); idx != std::string_view::npos) {
      raw_url = raw_url.substr(0, idx);
    }
  }
  std::string trimmed = util::trim(raw_url);
  if (trimmed.empty()) return std::nullopt;
  return util::decode_subscription_url(trimmed);
}

}  // namespace

Result<DeepLinkInfo> parse_deep_link(std::string_view link) {
  std::string_view s = strip_outer(link);

  std::string lower_s = util::to_lower(s);
  bool valid_scheme = false;
  if (lower_s.starts_with("clash://") || lower_s.starts_with("clash:") ||
      lower_s.starts_with("clash-verge://") || lower_s.starts_with("clash-verge:") ||
      lower_s.starts_with("clash-mini://") || lower_s.starts_with("clash-mini:")) {
    valid_scheme = true;
  }

  if (!valid_scheme) {
    return Result<DeepLinkInfo>::err(Error{2904, "Invalid deep link scheme: " + std::string(s)});
  }

  size_t qpos = s.find('?');
  if (qpos == std::string_view::npos) {
    return Result<DeepLinkInfo>::err(Error{2904, "Missing query string in deep link: " + std::string(s)});
  }
  const size_t fpos = s.find('#', qpos + 1);
  std::string_view query = (fpos == std::string_view::npos)
                               ? s.substr(qpos + 1)
                               : s.substr(qpos + 1, fpos - (qpos + 1));

  // 提取 name 参数（可选；忽略空值以保持与既有接口一致）
  std::optional<std::string> name_opt = extract_query_param(query, "name");
  if (name_opt && name_opt->empty()) {
    name_opt = std::nullopt;
  }

  // 提取 url 参数（必选）
  auto decoded_url = extract_subscription_url(query);
  if (!decoded_url || decoded_url->empty()) {
    return Result<DeepLinkInfo>::err(Error{2904, "Missing or empty url parameter in deep link: " + std::string(s)});
  }

  return Result<DeepLinkInfo>::ok(DeepLinkInfo{std::move(*decoded_url), std::move(name_opt)});
}

Result<void> resolve_scheme(std::string_view link) {
  auto parsed = parse_deep_link(link);
  if (!parsed.is_ok()) {
    events::EventBus::notice_message("import_sub_url::error", parsed.error().message);
    return Result<void>::err(parsed.error());
  }

  const auto& info = parsed.value();
  bool had_current = false;
  {
    auto latest = config::Config::instance().profiles().latest_arc();
    had_current = latest && latest->current.has_value() && !latest->current->empty();
  }

  if (core::profile_switching_lock().exchange(true)) {
    events::EventBus::notice_message("import_sub_url::error", "当前正在切换或更新配置，请稍候再试");
    return Result<void>::err(Error{2904, "switching_lock busy"});
  }
  core::ProfileSwitchGuard guard(core::profile_switching_lock());

  auto& profiles = config::Config::instance().profiles();
  std::optional<config::PrfItem> item_opt;

  profiles.edit_draft([&](config::IProfiles& d) {
    config::profiles::set_profiles_instance(&d);
    auto item = config::PrfItem::from_url(info.url, info.name, std::nullopt, std::nullopt);
    config::profiles::set_profiles_instance(nullptr);
    if (item.is_ok()) {
      (void)d.append_item(item.value());
      item_opt = item.value();
    }
  });

  if (!item_opt) {
    events::EventBus::notice_message("import_sub_url::error", "导入订阅失败（下载/解析失败或被拒）");
    return Result<void>::err(Error{2906, "from_url failed"});
  }

  profiles.apply();
  auto save = profiles.data_arc()->save_file();
  if (!save.is_ok()) {
    events::EventBus::notice_message("import_sub_url::error", "导入订阅保存失败: " + save.error().message);
    return Result<void>::err(save.error());
  }

  events::EventBus::notice_message("import_sub_url::ok", "");
  events::EventBus::refresh_verge();
  if (item_opt->uid) {
    events::EventBus::notify_profile_changed(*item_opt->uid);
  }

  // 核心重载规则：仅当此前无激活 profile，且新导入项成为当前 profile 时触发
  auto latest = profiles.latest_arc();
  bool is_current = latest && latest->current.has_value() && item_opt->uid &&
                    (*latest->current == *item_opt->uid);
  if (!had_current && is_current) {
    CWLOG_INFO("resolve_scheme: no prior active profile, refreshing core config");
    cmd::CommandService::instance().invoke("enhance_profiles");
  }

  return Result<void>::ok();
}

}  // namespace clashwin::app
