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
  std::string_view query = s.substr(qpos + 1);

  // 提取 name 参数（可选）
  std::optional<std::string> name_opt;
  size_t name_pos = std::string_view::npos;
  if (query.starts_with("name=")) {
    name_pos = 0;
  } else {
    name_pos = query.find("&name=");
    if (name_pos != std::string_view::npos) {
      name_pos += 1;  // 跳过 '&'
    }
  }

  if (name_pos != std::string_view::npos) {
    size_t val_start = name_pos + 5;  // 跳过 "name="
    size_t val_end = query.find('&', val_start);
    std::string_view raw_name;
    if (val_end == std::string_view::npos) {
      raw_name = query.substr(val_start);
    } else {
      raw_name = query.substr(val_start, val_end - val_start);
    }
    if (!raw_name.empty()) {
      name_opt = util::percent_decode(raw_name);
    }
  }

  // 提取 url 参数（必选）
  size_t url_pos = std::string_view::npos;
  if (query.starts_with("url=")) {
    url_pos = 0;
  } else {
    url_pos = query.find("&url=");
    if (url_pos != std::string_view::npos) {
      url_pos += 1;  // 跳过 '&'
    }
  }

  if (url_pos == std::string_view::npos) {
    return Result<DeepLinkInfo>::err(Error{2904, "Missing url parameter in deep link: " + std::string(s)});
  }

  size_t url_val_start = url_pos + 4;  // 跳过 "url="
  std::string_view raw_url;
  // 如果 url 在 name 之前，截断到 &name=
  if (name_pos != std::string_view::npos && name_pos > url_pos) {
    // name 在 url 之后：找到 &name=
    size_t and_name = query.find("&name=", url_val_start);
    if (and_name != std::string_view::npos) {
      raw_url = query.substr(url_val_start, and_name - url_val_start);
    } else {
      raw_url = query.substr(url_val_start);
    }
  } else {
    raw_url = query.substr(url_val_start);
  }

  if (raw_url.empty()) {
    return Result<DeepLinkInfo>::err(Error{2904, "Empty url parameter in deep link: " + std::string(s)});
  }

  std::string decoded_url = util::decode_subscription_url(raw_url);
  return Result<DeepLinkInfo>::ok(DeepLinkInfo{std::move(decoded_url), std::move(name_opt)});
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
