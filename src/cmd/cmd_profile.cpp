// T09 cmd_profile（← cmd/profile.rs + cmd/save_profile.rs 逐命令对照）
#include "cmd/cmd_support.h"

#include "core/switching_lock.h"
#include "core/auto_select.h"
#include "core/connection_stale.h"  // BUG-396
#include "core/monitor.h"
#include "core/timer.h"
#include "cmd/command_service.h"
#include "cmd/cmd_install.h"
#include "events/event_names.h"

#include "config/config.h"
#include "config/profiles.h"
#include "config/proxy_head_state.h"
#include "util/node.h"
#include "config/prfitem.h"
#include "core/validate.h"
#include "clashwin/system/diagnostic_bundle.h"
#include "util/log.h"
#include "enhance/enhance.h"
#include "util/dirs.h"
#include "util/utf.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace clashwin::cmd {

namespace {
int parse_2digits(const std::string& t, size_t pos, int* out) {
  if (pos + 2 > t.size() ||
      !std::isdigit(static_cast<unsigned char>(t[pos])) ||
      !std::isdigit(static_cast<unsigned char>(t[pos + 1]))) {
    return -1;
  }
  *out = (t[pos] - '0') * 10 + (t[pos + 1] - '0');
  return 0;
}

int64_t parse_rfc3339_epoch_sec(const std::string& t) {
  if (t.size() < 19) return -1;
  std::tm tm{};
  std::istringstream iss(t.substr(0, 19));
  iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  if (iss.fail()) return -1;
  int64_t sec = static_cast<int64_t>(_mkgmtime(&tm));
  if (sec < 0) return -1;

  size_t pos = 19;
  if (pos < t.size() && t[pos] == '.') {
    ++pos;
    while (pos < t.size() && std::isdigit(static_cast<unsigned char>(t[pos]))) {
      ++pos;
    }
  }
  int64_t tz_offset_sec = 0;
  if (pos < t.size()) {
    if (t[pos] == 'Z' || t[pos] == 'z') {
      tz_offset_sec = 0;
    } else if (t[pos] == '+' || t[pos] == '-') {
      const int sign = (t[pos] == '+') ? 1 : -1;
      ++pos;
      int hh = 0;
      int mm = 0;
      if (parse_2digits(t, pos, &hh) != 0) return -1;
      pos += 2;
      if (pos < t.size() && t[pos] == ':') ++pos;
      if (parse_2digits(t, pos, &mm) != 0) return -1;
      if (hh > 23 || mm > 59) return -1;
      tz_offset_sec = sign * (static_cast<int64_t>(hh) * 3600 +
                              static_cast<int64_t>(mm) * 60);
    }
  }
  return sec - tz_offset_sec;
}
// ← feat::CURRENT_SWITCHING_PROFILE：导入/更新/删除/切换/定时互斥。
// T15：锁实现上移 core/switching_lock.h（timer 同一把，跨层互斥）
std::atomic<bool>& switching_lock() { return core::profile_switching_lock(); }

struct SwitchGuard {
  std::atomic<bool>& lock;
  explicit SwitchGuard(std::atomic<bool>& l) : lock(l) { lock.store(true); }
  ~SwitchGuard() { lock.store(false); }
};


// 生成/校验/应用（30s 超时口径 ← cmd/profile.rs perform_config_update + BUG-407）
core::validate::ValidationOutcome update_config_forced() {
  auto outcome = run_enhance_pipeline();
  if (outcome.is_valid()) {
    if (is_core_running()) {
      // ← manager/config.rs apply_config：snapshot→reload(force)→restore（T07 + BUG-407）
      auto& mgr = core::CoreManager::instance();
      core::CoreApiClientAdapter api(&mihomo_unchecked());
      core::CoreOps ops;
      ops.start = [] {
        return core::CoreManager::instance().start_auto(
            util::dirs::clash_mini_path());
      };
      ops.stop = [] { return core::CoreManager::instance().stop(); };
      mgr.apply_config(api, utf::utf16_to_utf8(util::dirs::clash_mini_path()), ops);
    } else {
      events::EventBus::refresh_clash();
    }
  }
  return outcome;
}

// ← cmd/profile.rs discard_and_restore：切回 previous 并重载
void restore_previous_profile(const std::string& prev) {
  auto& profiles = config::Config::instance().profiles();
  profiles.edit_draft([&prev](config::IProfiles& d) { d.current = prev; });
  profiles.apply();
  if (auto r = profiles.data_arc()->save_file(); !r.is_ok()) {
    CWLOG_WARN("restore profiles save failed: " + r.error().message);
  }
  update_config_forced();
}

void notify_invalid(const core::validate::ValidationOutcome& o) {
  core::validate::handle_validation_notice(o, core::validate::ValidationNoticeTarget::Runtime,
                                          "运行时配置");
}

// ← feat/profile.rs UPDATE_FAILED_EVEN_WITH_CLASH_PREFIX（BUG-416 / BUG-417）
constexpr const char* kUpdateFailedEvenWithClashPrefix = "update_failed_even_with_clash:";

}  // namespace

// cmd_system.cpp
bool is_in_lightweight_mode();

// ← save_profile.rs:88-105 + BUG-409:
//   current 自身、全局 "Merge"，或 current 的 merge/script/rules/proxies/groups 子项（缺省按默认名归一化）→ 影响运行时
bool profile_affects_runtime(const config::IProfiles& profiles, const std::string& index) {
  const auto& cur = profiles.current;
  if (!cur.has_value() || cur->empty()) return false;
  if (*cur == index || index == "Merge") return true;
  const config::PrfItem* item = profiles.get_item(*cur);
  if (!item) return false;
  const config::PrfOption empty_opt{};
  const config::PrfOption& o = item->option.has_value() ? *item->option : empty_opt;
  auto trim_or = [](const std::optional<std::string>& v, const char* def) {
    if (!v.has_value()) return std::string(def);
    size_t b = 0, e = v->size();
    while (b < e && std::isspace(static_cast<unsigned char>((*v)[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>((*v)[e - 1]))) --e;
    return b < e ? v->substr(b, e - b) : std::string(def);
  };
  return trim_or(o.merge, "Merge") == index ||
         trim_or(o.script, "Script") == index ||
         trim_or(o.rules, "Rules") == index ||
         trim_or(o.proxies, "Proxies") == index ||
         trim_or(o.groups, "Groups") == index;
}

// T16：DelayManager/AutoSelect 生产 hooks（首次调用绑定；mock 测试自注入；
// cmd_clash test_group_delay 复用。声明在 cmd_install.h）
void ensure_t16_hooks() {
  static bool done = false;
  if (done) return;
  done = true;
  auto get_client = []() -> mihomo::MihomoClient* {
    auto r = mihomo();
    return r.is_ok() ? r.value() : nullptr;
  };

  core::DelayProbeHooks dh;
  dh.group_probe_ex = [get_client](const std::string&,
                                   const std::vector<std::string>& expected_nodes,
                                   bool allow_coalesce,
                                   bool allow_single_target_skip)
      -> std::optional<std::map<std::string, uint32_t>> {
    auto* c = get_client();
    if (!c) return std::nullopt;
    std::lock_guard<std::mutex> probe_lock(
        core::CoreMonitor::metrics_probe_mutex());
    if (allow_coalesce) {
      const int64_t probe_now = static_cast<int64_t>(GetTickCount64());
      if (auto reused = core::CoreMonitor::try_reuse_metrics_probe(
              probe_now, core::CoreMonitor::kProbeCoalesceWindowMs,
              expected_nodes, true)) {
        return reused;
      }
    }
    std::string url = "https://cp.cloudflare.com/generate_204";
    try {
      const auto& verge = *config::Config::instance().verge().latest_arc();
      url = verge.default_latency_test.value_or(url);
    } catch (...) {}
    static constexpr const char* kPrimaryTestUrl =
        "https://cp.cloudflare.com/generate_204";
    static constexpr const char* kFallbackTestUrl =
        "https://www.gstatic.com/generate_204";
    const std::string sec_url =
        (url == kFallbackTestUrl) ? kPrimaryTestUrl : kFallbackTestUrl;

    auto r = c->delay_group("PROXY__METRICS", url, core::kNodeDelayMaxMs);
    if (r.is_ok()) {
      std::map<std::string, uint32_t> merged = std::move(r.value());
      if (core::CoreMonitor::should_recheck_primary_with_fallback(
              merged, expected_nodes, allow_single_target_skip)) {
        auto rfb =
            c->delay_group("PROXY__METRICS", sec_url, core::kNodeDelayMaxMs);
        if (rfb.is_ok()) {
          merged = core::CoreMonitor::merge_probe_delays(std::move(merged),
                                                         rfb.value());
        } else if (merged.empty()) {
          core::CoreMonitor::invalidate_metrics_probe();
          return std::nullopt;
        }
      }
      core::CoreMonitor::record_metrics_probe(
          static_cast<int64_t>(GetTickCount64()), merged, false);
      return merged;
    }

    auto r2 = c->delay_group("PROXY__METRICS", sec_url, core::kNodeDelayMaxMs);
    if (!r2.is_ok() ||
        !core::CoreMonitor::has_any_valid_real_probe_delay(r2.value())) {
      core::CoreMonitor::invalidate_metrics_probe();
      return std::nullopt;
    }
    core::CoreMonitor::record_metrics_probe(
        static_cast<int64_t>(GetTickCount64()), r2.value(), false);
    return r2.value();
  };
  dh.group_probe = [ex = dh.group_probe_ex](const std::string& g) {
    return ex(g, {}, true, false);
  };
  dh.emit_results = [](const std::string& group,
                       const std::vector<std::pair<std::string, uint32_t>>& r,
                       bool from_probe) {
    events::EventBus::backend_delay_results(group, r, from_probe);
    if (!core::AutoSelect::instance().is_running()) {
      for (const auto& [name, d] : r) {
        (void)name;
        if (d > 0 && d < core::kNodeDelayMaxMs) {
          core::CoreMonitor::instance().emit_network_health_notice(
              "network_health::ok");
          break;
        }
      }
    }
  };
  core::DelayManager::instance().set_hooks(std::move(dh));

  core::AutoSelectHooks ah;
  ah.current_uid = []() -> std::optional<std::string> {
    auto latest = config::Config::instance().profiles().latest_arc();
    if (!latest) return std::nullopt;
    return latest->current;
  };
  ah.filter_text = [] {
    std::string uid;
    try {
      auto latest = config::Config::instance().profiles().latest_arc();
      if (latest && latest->current) uid = *latest->current;
    } catch (...) {}
    if (uid.empty()) return std::string();
    auto st = config::proxy_head_state::load();
    auto gs = config::proxy_head_state::get_group_state(st, uid, "PROXY");
    if (gs) return gs->filter_text;
    return std::string();
  };
  ah.saved_sort_type = [](const std::string& uid) -> std::optional<int> {
    if (uid.empty()) return std::nullopt;
    auto st = config::proxy_head_state::load();
    if (!st.is_object()) return std::nullopt;
    auto uit = st.find(uid);
    if (uit == st.end() || !uit->is_object()) return std::nullopt;
    auto git = uit->find("PROXY");
    if (git == uit->end() || !git->is_object()) return std::nullopt;
    auto sit = git->find("sortType");
    if (sit == git->end() || !sit->is_number_integer()) return std::nullopt;
    return sit->get<int>();
  };
  ah.fresh_metrics_subset = [get_client](const std::vector<std::string>& nodes)
      -> std::optional<std::vector<std::pair<std::string, uint32_t>>> {
    if (nodes.empty()) return std::nullopt;
    auto* c = get_client();
    if (!c) return std::nullopt;
    auto px = c->get_proxies();
    if (!px.is_ok()) return std::nullopt;
    const int64_t now_sec = static_cast<int64_t>(std::time(nullptr));
    static constexpr int64_t kMaxMetricsReuseAgeSecs = 30;
    std::vector<std::pair<std::string, uint32_t>> cached;
    cached.reserve(nodes.size());
    for (const auto& name : nodes) {
      auto it = px.value().proxies.find(name);
      if (it == px.value().proxies.end() || it->second.history.empty()) {
        return std::nullopt;
      }
      const auto& latest_h = it->second.history.back();
      const int64_t rec_sec = parse_rfc3339_epoch_sec(latest_h.time);
      if (rec_sec < 0 || (now_sec - rec_sec) >= kMaxMetricsReuseAgeSecs) {
        return std::nullopt;
      }
      const uint32_t d = (it->second.alive && latest_h.delay > 0 &&
                          latest_h.delay < core::kNodeDelayMaxMs)
                             ? latest_h.delay
                             : 0;
      cached.emplace_back(name, d);
    }
    return cached;
  };
  ah.is_dummy = [](const std::string& name) {
    return clashwin::util::is_dummy_node(name);
  };
  ah.is_confirmed_fake = [](const std::string& name) {
    return clashwin::util::is_confirmed_fake_node(name);
  };
  ah.select_node = [get_client](const std::string& group,
                                const std::string& node) {
    auto* c = get_client();
    return c && c->select_node_for_group(group, node).is_ok();
  };
  ah.write_selected = [](const std::string& uid, const std::string& node) {
    auto& profiles = config::Config::instance().profiles();
    bool ok = false;
    profiles.edit_draft([&](config::IProfiles& d) {
      if (!d.items) return;
      for (auto& it : *d.items) {
        if (it.uid && *it.uid == uid) {
          // 单组架构：selected 只含 PROXY 一项（← monitor.rs:1080 注释）
          it.selected = std::vector<config::PrfSelected>{
              config::PrfSelected{"PROXY", node}};
          ok = true;
          break;
        }
      }
    });
    if (ok) {
      profiles.apply();
      (void)profiles.data_arc()->save_file();
    }
    return ok;
  };
  ah.read_selected = [](const std::string& uid) -> std::optional<std::string> {
    auto latest = config::Config::instance().profiles().latest_arc();
    if (!latest) return std::nullopt;
    const config::PrfItem* it = latest->get_item(uid);
    if (!it || !it->selected || it->selected->empty()) return std::nullopt;
    for (const auto& sel : *it->selected)
      if (sel.name && *sel.name == "PROXY") return sel.now;
    return std::nullopt;
  };
  ah.failed_servers = [] {
    return core::CoreMonitor::instance().snapshot_failed_servers();
  };
  ah.node_server_map = [] {
    return core::CoreMonitor::instance().load_node_server_map();
  };
  ah.group_nodes = [get_client](const std::string& group) {
    auto* c = get_client();
    if (!c) return std::vector<std::string>{};
    auto g = c->get_group_by_name(group);
    if (!g.is_ok() || !g.value().all) return std::vector<std::string>{};
    return *g.value().all;
  };
  ah.is_verified_true = [](const std::string& name) {
    return util::is_verified_true_node(name);
  };
  ah.current_proxy_now = [get_client](const std::string& group)
      -> std::optional<std::string> {
    auto* c = get_client();
    if (!c) return std::nullopt;
    auto g = c->get_group_by_name(group);
    if (!g.is_ok()) return std::nullopt;
    return g.value().now;
  };
  ah.run_https_verify = [get_client](const std::vector<std::string>& candidates) {
    if (!util::has_pending_unverified_nodes(candidates)) return;
    auto* c = get_client();
    if (!c) return;
    if (!core::CoreMonitor::is_recent_flowing_traffic_active(
            static_cast<int64_t>(GetTickCount64())) &&
        !core::CoreMonitor::is_physical_network_online(
            2, "https://cp.cloudflare.com/generate_204")) {
      return;
    }
    std::map<std::string, uint32_t> delays;
    {
      std::lock_guard<std::mutex> lock(
          core::CoreMonitor::metrics_probe_mutex());
      auto r1 = c->delay_group(
          "PROXY__METRICS", core::CoreMonitor::kVerifyHttpsUrlPrimary,
          core::kNodeDelayMaxMs, 5000);
      if (r1.is_ok() && !r1.value().empty()) {
        delays = std::move(r1.value());
      } else {
        auto r2 = c->delay_group(
            "PROXY__METRICS", core::CoreMonitor::kVerifyHttpsUrlFallback,
            core::kNodeDelayMaxMs, 5000);
        if (r2.is_err()) return;
        delays = std::move(r2.value());
      }
    }
    std::vector<std::string> passed;
    for (const auto& [name, d] : delays) {
      if (d >= core::kNodeDelayMinMs && d < core::kNodeDelayMaxMs) {
        passed.push_back(name);
      }
    }
    if (passed.empty()) return;
    std::vector<std::string> failed;
    for (const auto& cand : candidates) {
      if (std::find(passed.begin(), passed.end(), cand) == passed.end() &&
          !util::is_dummy_node(cand) && !util::is_confirmed_fake_node(cand)) {
        failed.push_back(cand);
      }
    }
    (void)util::record_https_verify_results(passed, failed);
  };
  ah.wait_ready = [get_client]() -> bool {
    core::CoreMonitor::ReadyHooks rh;
    rh.api_alive = [get_client]() {
      auto* c = get_client();
      return c && c->get_base_config().is_ok();
    };
    rh.group_ready = [get_client]() {
      auto* c = get_client();
      if (!c) return false;
      auto g = c->get_group_by_name("PROXY");
      if (!g.is_ok() || !g.value().all) return false;
      for (const auto& n : *g.value().all) {
        if (!n.empty() && !util::is_dummy_node(n)) return true;
      }
      return false;
    };
    rh.network_online = []() {
      std::string url = "https://cp.cloudflare.com/generate_204";
      try {
        auto v = config::Config::instance().verge().latest_arc();
        if (v && v->default_latency_test && !v->default_latency_test->empty()) {
          url = *v->default_latency_test;
        }
      } catch (...) {}
      return core::CoreMonitor::is_physical_network_online(2, url);
    };
    rh.dns_ready = [get_client]() {
      auto* c = get_client();
      if (!c) return true;
      auto dr = c->dns_query_ready("www.baidu.com");
      if (dr.is_err()) return true;  // 端点不可用时放行由下游兜底（← monitor.rs:1120）
      return dr.value();
    };
    rh.now_ms = []() { return static_cast<int64_t>(GetTickCount64()); };
    return core::CoreMonitor::wait_for_clash_ready(rh);
  };
  core::AutoSelect::instance().set_hooks(std::move(ah));
}

void install_cmd_profile(CommandService& svc) {
  // ---- get_profiles（← profile.rs:43-49）----
  svc.register_command(
      "get_profiles",
      {[](const Json&) -> CmdResult {
         auto p = config::Config::instance().profiles().latest_arc();
         return CmdResult::ok(yaml_to_json(p->to_yaml()));
       },
       consts::kTimeoutProbe});

  // ---- enhance_profiles（← profile.rs:52-85：强制重新生成+校验）----
  svc.register_command(
      "enhance_profiles",
      {[](const Json&) -> CmdResult {
         auto outcome = update_config_forced();
         if (!outcome.is_valid() && outcome.status != core::validate::ValidationStatus::Busy) {
           CWLOG_WARN("enhance_profiles failed validation: " + outcome.message);
           notify_invalid(outcome);
         }
         return CmdResult::ok(validation_outcome_to_json(outcome));
       },
       consts::kTimeoutHeavy});

  // ---- import_profile（← profile.rs:88-150）----
  svc.register_command(
      "import_profile",
      {[](const Json& args) -> CmdResult {
         if (switching_lock().exchange(true)) {
           return CmdResult::err({errs::kBadArgs, "当前正在切换或更新配置，请稍候再试"});
         }
         SwitchGuard guard(switching_lock());
         const Json* url = find_arg(args, {"url"});
         if (!url || !url->is_string()) return CmdResult::err({errs::kBadArgs, "url required"});
         PrfOption option;
         if (const Json* op = find_arg(args, {"option"}); op && op->is_object()) {
           option = prf_option_from_json(*op);
         }
         std::optional<std::string> name_opt;
         if (const Json* nm = find_arg(args, {"name"});
             nm && nm->is_string() && !nm->get<std::string>().empty()) {
           name_opt = nm->get<std::string>();
         }
         auto& profiles = config::Config::instance().profiles();
         std::optional<config::PrfItem> item_opt;
         profiles.edit_draft([&](config::IProfiles& d) {
           config::profiles::set_profiles_instance(&d);
           auto item = config::PrfItem::from_url(url->get<std::string>(),
                                                 name_opt, std::nullopt,
                                                 option);
           config::profiles::set_profiles_instance(nullptr);
           if (item.is_ok()) {
             auto r = d.append_item(item.value());
             (void)r;
             item_opt = item.value();
           }
         });
         if (!item_opt) {
           return CmdResult::err({errs::kIo, "导入订阅失败（下载/解析失败或被拒）"});
         }
         profiles.apply();
         auto save = profiles.data_arc()->save_file();
         if (!save.is_ok()) {
           return CmdResult::err({errs::kIo, "导入订阅失败: " + save.error().message});
         }
         if (item_opt->uid)
           events::EventBus::notify_profile_changed(*item_opt->uid);
         core::ProfileTimer::instance().refresh();
         events::EventBus::instance().publish(
             events::names::kTimerUpdated,
             item_opt->uid.value_or(std::string()));
         // ← profile.rs:132-146 + get_merged_proxies：导入新订阅后同步刷新运行时配置与节点
         (void)update_config_forced();
         return CmdResult::ok(
             Json{{"uid", item_opt->uid.value_or(std::string())}});
       },
       consts::kTimeoutHeavy});

  // ---- update_profile（← profile.rs:198-212 + feat/profile.rs:69-133 三级回退与通知）----
  svc.register_command(
      "update_profile",
      {[](const Json& args) -> CmdResult {
         if (switching_lock().exchange(true)) {
           return CmdResult::err({errs::kBadArgs, "当前正在切换或更新配置，请稍候再试"});
         }
         SwitchGuard guard(switching_lock());
         const Json* index = find_arg(args, {"index"});
         if (!index || !index->is_string()) return CmdResult::err({errs::kBadArgs, "index required"});
         const Json* auto_arg = find_arg(args, {"auto_trigger", "auto"});
         const bool is_manual_trigger =
             !(auto_arg && auto_arg->is_boolean() && auto_arg->get<bool>());
         auto latest = config::Config::instance().profiles().latest_arc();
         const config::PrfItem* old = latest->get_item(index->get<std::string>());
         if (!old || !old->url) {
           return CmdResult::err({errs::kBadArgs, "profile not found or has no url"});
         }
         const std::string profile_name =
             old->name.value_or(std::string("UnKnown Profile"));
         std::optional<PrfOption> override_opt;
         if (const Json* op = find_arg(args, {"option"}); op && op->is_object()) {
           override_opt = prf_option_from_json(*op);
         }
         PrfOption option =
             config::PrfOption::merge_opt(old->option, override_opt)
                 .value_or(PrfOption{});
         auto& profiles = config::Config::instance().profiles();
         std::optional<config::PrfItem> item_opt;
         bool used_proxy_fallback = false;
         std::string last_err;
         profiles.edit_draft([&](config::IProfiles& d) {
           auto try_fetch = [&](const PrfOption& opt) -> Result<config::PrfItem> {
             config::profiles::set_profiles_instance(&d);
             auto res = config::PrfItem::from_url(*old->url, old->name, old->desc, opt);
             config::profiles::set_profiles_instance(nullptr);
             return res;
           };
           // 1) 正常更新（← feat/profile.rs:69）
           auto item = try_fetch(option);
           if (item.is_err()) {
             last_err = item.error().message;
             // 2) Clash 代理回退（← feat/profile.rs:86-95）
             PrfOption clash_opt = option;
             clash_opt.self_proxy = true;
             clash_opt.with_proxy = false;
             item = try_fetch(clash_opt);
             if (item.is_ok()) {
               used_proxy_fallback = true;
             } else {
               last_err = item.error().message;
               // 3) 系统代理回退（← feat/profile.rs:108-117）
               PrfOption sys_opt = option;
               sys_opt.self_proxy = false;
               sys_opt.with_proxy = true;
               item = try_fetch(sys_opt);
               if (item.is_ok()) {
                 used_proxy_fallback = true;
               } else {
                 last_err = item.error().message;
                 return;  // 失败保留原文
               }
             }
           }
           item.value().uid = old->uid;  // 同 uid → 更新元数据 + 覆写文件
           auto r = d.append_item(item.value());
           if (r.is_ok()) item_opt = item.value();
         });
         if (!item_opt) {
           if (is_manual_trigger && util::clear_unverified_https_failures() > 0) {
             events::EventBus::refresh_proxies();
           }
           // ← feat/profile.rs perform_profile_update（BUG-416 / BUG-417）：
           //   错误原文先脱敏；手动触发返回带前缀的 Err 交 UI 本地化提示（不重复通知），
           //   自动触发非轻量模式才发通知，并统一返回 Err 让定时器按失败 2 分钟快速重试。
           const std::string masked_err =
               last_err.empty() ? std::string("更新订阅失败（原文已保留）")
                                : system::DiagnosticBundle::redact_log_text(last_err);
           const std::string detail = profile_name + " - " + masked_err;
           if (is_manual_trigger) {
             return CmdResult::err(
                 {errs::kIo, std::string(kUpdateFailedEvenWithClashPrefix) + detail});
           }
           if (!is_in_lightweight_mode()) {
             events::EventBus::notice_message("update_failed_even_with_clash", detail);
           }
           return CmdResult::err({errs::kIo, detail});
         }
         if (used_proxy_fallback) {
           events::EventBus::notice_message("update_with_clash_proxy", profile_name);
         }
         profiles.apply();
         if (auto save = profiles.data_arc()->save_file(); !save.is_ok()) {
           return CmdResult::err({errs::kIo, save.error().message});
         }
         core::ProfileTimer::instance().refresh();
         events::EventBus::instance().publish(events::names::kTimerUpdated,
                                              index->get<std::string>());
         const size_t cleared_quarantine =
             is_manual_trigger ? util::clear_unverified_https_failures() : 0;
         {
           auto outcome = update_config_forced();
           if (outcome.is_valid()) {
             if (item_opt->uid)
               events::EventBus::notify_profile_changed(*item_opt->uid);
           } else {
             if (cleared_quarantine > 0) events::EventBus::refresh_proxies();
             CWLOG_ERROR("[订阅更新] 更新失败: " + outcome.message);
             // BUG-416：仅自动触发且非轻量模式发通知；统一返回 Err（手动由 UI 提示）
             if (!is_manual_trigger && !is_in_lightweight_mode()) {
               notify_invalid(outcome);
             }
             return CmdResult::err({errs::kIo, outcome.message});
             }
         }
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutHeavy});

  // ---- delete_profile（← profile.rs:215-247）----
  svc.register_command(
      "delete_profile",
      {[](const Json& args) -> CmdResult {
         if (switching_lock().exchange(true)) {
           return CmdResult::err({errs::kBadArgs, "当前正在切换或更新配置，请稍候再试"});
         }
         SwitchGuard guard(switching_lock());
         const Json* index = find_arg(args, {"index"});
         if (!index || !index->is_string()) return CmdResult::err({errs::kBadArgs, "index required"});
         const Json* auto_arg = find_arg(args, {"auto_trigger", "auto"});
         const bool is_manual_trigger =
             !(auto_arg && auto_arg->is_boolean() && auto_arg->get<bool>());
         auto& profiles = config::Config::instance().profiles();
         // 运行时配置合并全部 remote/local 订阅节点（get_merged_proxies），
         // 删除任一 remote/local 档案都会改变产物 → 必须重建并重载内核
         bool merged_affected = false;
         if (const config::PrfItem* it =
                 profiles.latest_arc()->get_item(index->get<std::string>())) {
           merged_affected = it->itype &&
                             (*it->itype == "remote" || *it->itype == "local");
         }
         Result<bool> should_update = Result<bool>::ok(false);
         profiles.edit_draft([&](config::IProfiles& d) {
           should_update = d.delete_item(index->get<std::string>());
         });
         if (should_update.is_err()) return CmdResult::err(should_update.error());
         profiles.apply();
         if (auto save = profiles.data_arc()->save_file(); !save.is_ok()) {
           return CmdResult::err({errs::kIo, save.error().message});
         }
         core::ProfileTimer::instance().refresh();
         events::EventBus::instance().publish(events::names::kTimerUpdated,
                                              index->get<std::string>());
         if (should_update.value() || merged_affected) {
           auto outcome = update_config_forced();
           if (outcome.is_valid()) {
             auto latest = profiles.latest_arc();
             events::EventBus::notify_profile_changed(latest->current.value_or(""));
           } else {
             notify_invalid(outcome);
             return CmdResult::err({errs::kIo, outcome.message});
           }
         }
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutHeavy});

  // ---- patch_profile（← profile.rs:368-400；定时器刷新留 T15）----
  svc.register_command(
      "patch_profile",
      {[](const Json& args) -> CmdResult {
         const Json* index = find_arg(args, {"index"});
         const Json* profile = find_arg(args, {"profile"});
         if (!index || !index->is_string() || !profile || !profile->is_object()) {
           return CmdResult::err({errs::kBadArgs, "index/profile required"});
         }
         config::PrfItem item = prfitem_from_json(*profile);
         auto& profiles = config::Config::instance().profiles();
         Result<void> r = Result<void>::ok();
         profiles.edit_draft([&](config::IProfiles& d) {
           r = d.patch_item(index->get<std::string>(), item);
         });
         if (r.is_err()) return CmdResult::err(r.error());
         profiles.apply();
         if (auto save = profiles.data_arc()->save_file(); !save.is_ok()) {
           return CmdResult::err({errs::kIo, save.error().message});
         }
         core::ProfileTimer::instance().refresh();
         events::EventBus::instance().publish(events::names::kTimerUpdated,
                                              index->get<std::string>());
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutConfig});

  // ---- view_profile（← profile.rs:403-420：系统编辑器打开）----
  svc.register_command(
      "view_profile",
      {[](const Json& args) -> CmdResult {
         const Json* index = find_arg(args, {"index"});
         if (!index || !index->is_string()) return CmdResult::err({errs::kBadArgs, "index required"});
         const Json* auto_arg = find_arg(args, {"auto_trigger", "auto"});
         const bool is_manual_trigger =
             !(auto_arg && auto_arg->is_boolean() && auto_arg->get<bool>());
         auto latest = config::Config::instance().profiles().latest_arc();
         const config::PrfItem* item = latest->get_item(index->get<std::string>());
         if (!item || !item->file) return CmdResult::err({errs::kBadArgs, "the file field is null"});
         std::wstring path = util::dirs::app_profiles_dir() + L"\\" + utf::utf8_to_utf16(*item->file);
         if (!std::filesystem::exists(path)) {
           return CmdResult::err({errs::kIo, "file not found \"" + utf::utf16_to_utf8(path) + "\""});
         }
         auto opened = shell_open(path);
         return opened.is_ok() ? CmdResult::ok(Json::object()) : CmdResult::err(opened.error());
       },
       consts::kTimeoutProbe});

  // ---- read_profile_file（← profile.rs:423-435）----
  svc.register_command(
      "read_profile_file",
      {[](const Json& args) -> CmdResult {
         const Json* index = find_arg(args, {"index"});
         if (!index || !index->is_string()) return CmdResult::err({errs::kBadArgs, "index required"});
         const Json* auto_arg = find_arg(args, {"auto_trigger", "auto"});
         const bool is_manual_trigger =
             !(auto_arg && auto_arg->is_boolean() && auto_arg->get<bool>());
         auto latest = config::Config::instance().profiles().latest_arc();
         const config::PrfItem* item = latest->get_item(index->get<std::string>());
         if (!item) return CmdResult::err({errs::kBadArgs, "profile not found"});
         config::PrfItem probe;
         probe.file = item->file;
         auto data = probe.read_file();
         if (data.is_err()) return CmdResult::err(data.error());
         return CmdResult::ok(Json(data.value()));
       },
       consts::kTimeoutProbe});

  // ---- save_profile_file（← cmd/save_profile.rs：写→校验→失败回写原文）----
  svc.register_command(
      "save_profile_file",
      {[](const Json& args) -> CmdResult {
         const Json* index = find_arg(args, {"index"});
         const Json* data = find_arg(args, {"data"});
         if (!index || !index->is_string() || !data || !data->is_string()) {
           return CmdResult::err({errs::kBadArgs, "index/data required"});
         }
         auto latest = config::Config::instance().profiles().latest_arc();
         const std::string index_str = index->get<std::string>();
         const config::PrfItem* item = latest->get_item(index_str);
         if (!item || !item->file) return CmdResult::err({errs::kBadArgs, "the file field is null"});
         const bool is_merge_file = item->itype.has_value() && *item->itype == "merge";
         const bool is_script_file = (item->itype.has_value() && *item->itype == "script") ||
                                     (item->file->size() >= 3 &&
                                      item->file->compare(item->file->size() - 3, 3, ".js") == 0);
         std::wstring path = util::dirs::app_profiles_dir() + L"\\" + utf::utf8_to_utf16(*item->file);
         std::string original;
         {
           std::ifstream f(path, std::ios::binary);
           if (f.is_open()) {
             std::ostringstream oss;
             oss << f.rdbuf();
             original = oss.str();
           }
         }
         const std::string& content = data->get<std::string>();
         // ← save_profile.rs:46-56：内容相同（CRLF 归一后）直接返回 Valid，不写盘不校验
         if (original == content) {
           return CmdResult::ok(validation_outcome_to_json(core::validate::ValidationOutcome{}));
         }
         {
           std::string a = original;
           std::string b = content;
           std::replace(a.begin(), a.end(), '\r', '\n');
           std::replace(b.begin(), b.end(), '\r', '\n');
           if (a == b) {
             return CmdResult::ok(validation_outcome_to_json(core::validate::ValidationOutcome{}));
           }
         }
         {
           std::ofstream f(path, std::ios::binary);
           if (!f.is_open()) return CmdResult::err({errs::kIo, "open for write failed"});
           f << content;
         }
         // 历史遗留 script 文件：JS 增强引擎已退役，落盘后直接放行无操作
         if (is_script_file) {
           return CmdResult::ok(validation_outcome_to_json(core::validate::ValidationOutcome{}));
         }
         // 独立校验（-t 内核校验）
         auto check = core::validate::make_check_copy(path);
         core::validate::ValidationOutcome outcome;
         if (check.is_ok()) {
           core::validate::ValidateOptions opt;
           opt.core_path = core::locate_core_path();
           opt.app_dir = util::dirs::app_home_dir();
           if (opt.core_path.empty()) {
             outcome = core::validate::ValidationOutcome{};
           } else {
             auto vr = core::validate::validate_config_file(opt, utf::utf16_to_utf8(check.value()));
             outcome = vr.is_err() ? core::validate::ValidationOutcome::invalid_from_message(
                                         vr.error().message)
                                   : vr.value();
           }
         } else {
           outcome = core::validate::ValidationOutcome::invalid(
               core::validate::ValidationErrorKind::FileRead, check.error().message);
         }
         if (!outcome.is_valid()) {
           // ← 失败回写原文
           std::ofstream f(path, std::ios::binary);
           f << original;
           const auto target = is_merge_file
                                   ? core::validate::ValidationNoticeTarget::Merge
                                   : core::validate::ValidationNoticeTarget::Runtime;
           const char* file_type =
               is_merge_file ? "合并配置文件" : "YAML配置文件";
           core::validate::handle_validation_notice(outcome, target, file_type);
           return CmdResult::ok(validation_outcome_to_json(outcome));
         }
         // ← 影响运行时则 update_config_forced（save_profile.rs:88-105）
         if (profile_affects_runtime(*latest, index_str)) {
           auto refresh = update_config_forced();
           if (!refresh.is_valid()) notify_invalid(refresh);
         }
         return CmdResult::ok(validation_outcome_to_json(outcome));
       },
       consts::kTimeoutConfig});

  // ---- patch_profiles_config（← profile.rs:349-365：切换 current，失败回滚）----
  svc.register_command(
      "patch_profiles_config",
      {[](const Json& args) -> CmdResult {
         if (switching_lock().exchange(true)) {
           return CmdResult::ok(Json{{"status", "busy"}});
         }
         SwitchGuard guard(switching_lock());
         config::IProfiles patch = iprofiles_patch_from_json(args);
         if (!patch.current.has_value()) {
           return CmdResult::err({errs::kBadArgs, "current required"});
         }
         auto& profiles = config::Config::instance().profiles();
         std::optional<std::string> previous = profiles.latest_arc()->current;
         profiles.edit_draft([&patch](config::IProfiles& d) { d.patch_config(patch); });

         // 30s 预算内的增强+校验（← perform_config_update）
         auto outcome = update_config_forced();
         if (outcome.is_valid()) {
           profiles.apply();
           if (auto save = profiles.data_arc()->save_file(); !save.is_ok()) {
             CWLOG_WARN("save profiles failed: " + save.error().message);
           }
           if (profiles.latest_arc()->current) {
             events::EventBus::notify_profile_changed(*profiles.latest_arc()->current);
           }
           // T16：激活校准防线（← use-profiles activateSelected：持久
           // selected 越界过滤词子集 → 不写回内核、回写内核实际节点 now）
           if (auto client = mihomo(); client.is_ok()) {
             auto g = client.value()->get_group_by_name("PROXY");
             if (g.is_ok() && g.value().now.has_value() &&
                 profiles.latest_arc()->current) {
               auto st = config::proxy_head_state::load();
               // 头部状态结构是 {uid:{PROXY:{filterText:...}}}（← Mini cmd/proxy.rs:98-116；
               //   Win 侧 config/proxy_head_state.h:3-4 同构），顶层无 filter_text 键，
               //   旧写法 st.value("filter_text", ...) 恒取空串 → 激活校准拿不到过滤词。
               std::string filter_text;
               if (auto gs = config::proxy_head_state::get_group_state(
                       st, *profiles.latest_arc()->current, "PROXY")) {
                 filter_text = gs->filter_text;
               }
               // ★ calibrate_selected 形参契约是 filter_text_lower（auto_select.cpp:78-90，
               //   函数内部不再小写化）：落盘是用户输入原文（node_list.cpp set_filter，
               //   ← Mini use-head-state.ts:101-110 存受控原文），传参前必须 trim+tolower，
               //   口径同 core::extract_filter_text（core_manager.cpp:352-362 ← lifecycle.rs:437-440）。
               {
                 auto is_space = [](unsigned char c) {
                   return c == ' ' || c == '\t' || c == '\r' || c == '\n';
                 };
                 size_t b = 0, e = filter_text.size();
                 while (b < e && is_space(static_cast<unsigned char>(filter_text[b]))) ++b;
                 while (e > b && is_space(static_cast<unsigned char>(filter_text[e - 1]))) --e;
                 filter_text = filter_text.substr(b, e - b);
                 std::transform(filter_text.begin(), filter_text.end(), filter_text.begin(),
                                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
               }
               core::AutoSelect::calibrate_selected(
                   *profiles.latest_arc()->current,
                   filter_text,
                   *g.value().now,
                   [](const std::string& uid) -> std::optional<std::string> {
                     auto l = config::Config::instance().profiles().latest_arc();
                     if (!l) return std::nullopt;
                     const config::PrfItem* it = l->get_item(uid);
                     if (!it || !it->selected) return std::nullopt;
                     for (const auto& sel : *it->selected)
                       if (sel.name && *sel.name == "PROXY") return sel.now;
                     return std::nullopt;
                   },
                   [](const std::string& uid, const std::string& node) {
                     auto& profiles = config::Config::instance().profiles();
                     bool ok = false;
                     profiles.edit_draft([&](config::IProfiles& d) {
                       if (config::PrfItem* it2 = d.get_item_mut(uid)) {
                         it2->selected = std::vector<config::PrfSelected>{
                             config::PrfSelected{"PROXY", node}};
                         ok = true;
                       }
                     });
                     if (ok) {
                       profiles.apply();
                       (void)profiles.data_arc()->save_file();
                     }
                     return ok;
                   });
             }
           }
           return CmdResult::ok(validation_outcome_to_json(outcome));
         }
         if (outcome.status == core::validate::ValidationStatus::Busy) {
           profiles.discard();
           return CmdResult::ok(validation_outcome_to_json(outcome));
         }
         // ← discard_and_restore
         profiles.discard();
         if (previous) {
           restore_previous_profile(*previous);
           events::EventBus::notify_profile_changed(*previous);
         }
         notify_invalid(outcome);
         return CmdResult::ok(validation_outcome_to_json(outcome));
       },
       consts::kTimeoutConfig});

  // ---- trigger_auto_select（← cmd/proxy.rs:13-25 + monitor.rs:745-1090；
  //      T16 转正：AutoSelect 全流程——testing 中间态/择优/避让/双重校验/
  //      回写 selected 子集校验/AUTO_SELECT_BUSY）----
  ensure_t16_hooks();
  svc.register_command(
      "trigger_auto_select",
      {[](const Json& args) -> CmdResult {
         const Json* uid = find_arg(args, {"profile_uid"});
         if (!uid || !uid->is_string())
           return CmdResult::err({errs::kBadArgs, "profile_uid required"});
         bool select = arg_or(args, {"select"}, Json(false)) == true;
         int sort_type = 1;
         if (const Json* st = find_arg(args, {"sort_type"}); st && st->is_number())
           sort_type = st->get<int>();
         std::optional<std::vector<std::string>> nodes;
         if (const Json* nn = find_arg(args, {"node_names"});
             nn && nn->is_array() && !nn->empty()) {
           nodes = std::vector<std::string>();
           for (const auto& n : *nn) nodes->push_back(n.get<std::string>());
         }
         auto outcome = core::AutoSelect::instance().trigger(
             uid->get<std::string>(), nodes, sort_type, select);
         if (outcome.busy)
           return CmdResult::err({errs::kBadArgs, "AUTO_SELECT_BUSY"});
         if (outcome.preempted)
           return CmdResult::err({errs::kBadArgs, "AUTO_SELECT_MANUAL_PREEMPTED"});
         // BUG-396（cmd/proxy.rs:42-48）：仅前台用户显式操作传 true，
         // 选中成功后精准清理非目标节点的 PROXY 存量连接；后台自愈路径不传此参数。
         const bool clean_stale =
             arg_or(args, {"clean_stale_connections"}, Json(false)) == true;
         if (select && clean_stale && !outcome.selected_node.empty()) {
           (void)core::close_stale_proxy_connections(
               cmd::mihomo_unchecked(), outcome.selected_node);
         }
         Json arr = Json::array();
         for (auto& [name, delay] : outcome.display) arr.push_back({name, delay});
         return CmdResult::ok(arr);
       },
       consts::kTimeoutHeavy});

  // ---- create_profile（← profile.rs：新建本地/merge 条目）----
  svc.register_command(
      "create_profile",
      {[](const Json& args) -> CmdResult {
         if (switching_lock().exchange(true)) {
           return CmdResult::err({errs::kBadArgs, "当前正在切换或更新配置，请稍候再试"});
         }
         SwitchGuard guard(switching_lock());
         const Json* item_j = find_arg(args, {"item"});
         if (!item_j || !item_j->is_object())
           return CmdResult::err({errs::kBadArgs, "item required"});
         // ← PrfItem 前端只传 itype/name/desc/url 等；构造走对应 from_*
         std::string itype = item_j->value("type", item_j->value("itype", "local"));
         std::string name = item_j->value("name", "");
         std::string desc = item_j->value("desc", "");
         std::optional<std::string> file_data;
         if (const Json* fd = find_arg(args, {"file_data"}); fd && fd->is_string())
           file_data = fd->get<std::string>();

         auto& profiles = config::Config::instance().profiles();
         config::PrfItem item;
         // from_local/from_url 内部 ensure_enhance_items 挂子项经
         // append_item_safe → 必须在装配期实例注册区内构造（config.cpp
         // needs_items 分支同款）
         profiles.edit_draft([&](config::IProfiles& d) {
           config::profiles::set_profiles_instance(&d);
           if (itype == "local") {
             auto r = config::PrfItem::from_local(name, desc, file_data, std::nullopt);
             if (r.is_ok()) item = r.value();
           } else if (itype == "merge") {
             item = config::PrfItem::from_merge(std::nullopt);
           }
           if (!item.uid) {
             config::profiles::set_profiles_instance(nullptr);
             return;
           }
           if (!name.empty()) item.name = name;
           if (!desc.empty()) item.desc = desc;
           if (file_data && itype != "local") item.file_data = file_data;
           auto r = d.append_item(item);
           config::profiles::set_profiles_instance(nullptr);
           (void)r;
         });
         if (!item.uid) {
           return CmdResult::err({errs::kBadArgs, "unsupported itype: " + itype});
         }
         profiles.apply();
         if (auto save = profiles.data_arc()->save_file(); !save.is_ok())
           return CmdResult::err({errs::kIo, "add profile error: " + save.error().message});
         if (item.uid) events::EventBus::notify_profile_changed(*item.uid);
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutHeavy});

  // ---- reorder_profile（← profile.rs:154-170：拖拽排序 active→over 位置）----
  svc.register_command(
      "reorder_profile",
      {[](const Json& args) -> CmdResult {
         if (switching_lock().exchange(true)) {
           return CmdResult::err({errs::kBadArgs, "当前正在切换或更新配置，请稍候再试"});
         }
         SwitchGuard guard(switching_lock());
         const Json* active = find_arg(args, {"active_id"});
         const Json* over = find_arg(args, {"over_id"});
         if (!active || !over || !active->is_string() || !over->is_string())
           return CmdResult::err({errs::kBadArgs, "active_id/over_id required"});
         auto& profiles = config::Config::instance().profiles();
         bool reorder_ok = true;
         std::string reorder_err;
         profiles.edit_draft([&](config::IProfiles& d) {
           auto r = d.reorder(active->get<std::string>(), over->get<std::string>());
           reorder_ok = r.is_ok();
           if (!r.is_ok()) reorder_err = r.error().message;
         });
         if (!reorder_ok)
           return CmdResult::err({errs::kIo, "重新排序配置文件失败: " + reorder_err});
         profiles.apply();
         if (auto save = profiles.data_arc()->save_file(); !save.is_ok())
           return CmdResult::err({errs::kIo, save.error().message});
         events::EventBus::refresh_verge();  // 顺序落盘后通知（列表重渲染）
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutConfig});

  // ---- get_next_update_time（← profile.rs:438-443；T15 Timer 转正）----
  svc.register_command(
      "get_next_update_time",
      {[](const Json& args) -> CmdResult {
         const Json* index = find_arg(args, {"index"});
         if (!index || !index->is_string())
           return CmdResult::err({errs::kBadArgs, "index required"});
         auto t = core::ProfileTimer::instance().get_next_update_time(
             index->get<std::string>());
         if (!t) return CmdResult::err({errs::kBadArgs, "no timer task"});
         return CmdResult::ok(Json(*t));
       },
       consts::kTimeoutProbe});
}

}  // namespace clashwin::cmd
