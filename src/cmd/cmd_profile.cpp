// T09 cmd_profile（← cmd/profile.rs + cmd/save_profile.rs 逐命令对照）
#include "cmd/cmd_support.h"

#include "core/switching_lock.h"
#include "core/auto_select.h"
#include "core/monitor.h"
#include "core/timer.h"
#include "cmd/command_service.h"
#include "cmd/cmd_install.h"

#include "config/config.h"
#include "config/profiles.h"
#include "config/proxy_head_state.h"
#include "util/node.h"
#include "config/prfitem.h"
#include "core/validate.h"
#include "enhance/enhance.h"
#include "util/dirs.h"
#include "util/utf.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>

namespace clashwin::cmd {

namespace {
// ← feat::CURRENT_SWITCHING_PROFILE：导入/更新/删除/切换/定时互斥。
// T15：锁实现上移 core/switching_lock.h（timer 同一把，跨层互斥）
std::atomic<bool>& switching_lock() { return core::profile_switching_lock(); }

struct SwitchGuard {
  std::atomic<bool>& lock;
  explicit SwitchGuard(std::atomic<bool>& l) : lock(l) { lock.store(true); }
  ~SwitchGuard() { lock.store(false); }
};


// 生成/校验/应用（30s 超时口径 ← cmd/profile.rs perform_config_update）
core::validate::ValidationOutcome update_config_forced() {
  auto outcome = run_enhance_pipeline();
  if (outcome.is_valid() && is_core_running()) {
    // ← manager/config.rs apply_config：snapshot→reload(force)→restore（T07）
    auto& mgr = core::CoreManager::instance();
    core::CoreApiClientAdapter api(&mihomo_unchecked());
    core::CoreOps ops;
    ops.start = [] { return Result<void>::err(Error{0, "restart not wired"}); };
    ops.stop = [] { return Result<void>::err(Error{0, "restart not wired"}); };
    mgr.apply_config(api, utf::utf16_to_utf8(util::dirs::clash_mini_path()), ops);
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

// ← save_profile.rs:88-105 profile_affects_runtime：
//   current 自身，或 current 的 merge/script/rules/proxies/groups 子项 → 影响运行时
bool profile_affects_runtime(const config::IProfiles& profiles, const std::string& index) {
  const auto& cur = profiles.current;
  if (!cur.has_value()) return false;
  if (*cur == index) return true;
  const config::PrfItem* item = profiles.get_item(*cur);
  if (!item || !item->option.has_value()) return false;
  const auto& o = *item->option;
  return (o.merge && *o.merge == index) || (o.script && *o.script == index) ||
         (o.rules && *o.rules == index) || (o.proxies && *o.proxies == index) ||
         (o.groups && *o.groups == index);
}
}  // namespace

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
  dh.group_probe = [get_client](const std::string& metrics_group)
      -> std::optional<std::map<std::string, uint32_t>> {
    auto* c = get_client();
    if (!c) return std::nullopt;
    std::string url = "https://cp.cloudflare.com/generate_204";
    try {
      const auto& verge = *config::Config::instance().verge().latest_arc();
      url = verge.default_latency_test.value_or(url);
    } catch (...) {}
    auto r = c->delay_group(metrics_group, url, core::kNodeDelayMaxMs);
    if (!r.is_ok() && metrics_group != "PROXY") {
      r = c->delay_group("PROXY", url, core::kNodeDelayMaxMs);
    }
    if (!r.is_ok()) return std::nullopt;
    return r.value();
  };
  dh.fresh_history = [get_client]() -> std::map<std::string, uint32_t> {
    std::map<std::string, uint32_t> out;
    auto* c = get_client();
    if (!c) return out;
    auto px = c->get_proxies();
    if (!px.is_ok()) return out;
    for (const auto& [name, p] : px.value().proxies) {
      if (!p.alive || p.history.empty()) continue;
      out[name] = p.history.back().delay;
    }
    return out;
  };
  dh.emit_results = [](const std::string& group,
                       const std::vector<std::pair<std::string, uint32_t>>& r) {
    events::EventBus::backend_delay_results(group, r);
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
  ah.is_dummy = [](const std::string& name) {
    return clashwin::util::is_dummy_node(name);
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
      for (auto& it : d.items.value_or(std::vector<config::PrfItem>{})) {
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
         if (outcome.is_valid()) {
           events::EventBus::refresh_clash();
         } else if (outcome.status != core::validate::ValidationStatus::Busy) {
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
         // ← profile.rs:132-146 + get_merged_proxies：导入新订阅后同步刷新运行时配置与节点
         {
           auto outcome = update_config_forced();
           if (outcome.is_valid()) events::EventBus::refresh_clash();
         }
         return CmdResult::ok(
             Json{{"uid", item_opt->uid.value_or(std::string())}});
       },
       consts::kTimeoutHeavy});

  // ---- update_profile（← profile.rs:198-212：from_url 覆写原 file）----
  svc.register_command(
      "update_profile",
      {[](const Json& args) -> CmdResult {
         if (switching_lock().exchange(true)) {
           return CmdResult::err({errs::kBadArgs, "当前正在切换或更新配置，请稍候再试"});
         }
         SwitchGuard guard(switching_lock());
         const Json* index = find_arg(args, {"index"});
         if (!index || !index->is_string()) return CmdResult::err({errs::kBadArgs, "index required"});
         auto latest = config::Config::instance().profiles().latest_arc();
         const config::PrfItem* old = latest->get_item(index->get<std::string>());
         if (!old || !old->url) {
           return CmdResult::err({errs::kBadArgs, "profile not found or has no url"});
         }
         PrfOption option;
         if (const Json* op = find_arg(args, {"option"}); op && op->is_object()) {
           option = prf_option_from_json(*op);
         }
         auto& profiles = config::Config::instance().profiles();
         std::optional<config::PrfItem> item_opt;
         bool touched_current = false;
         profiles.edit_draft([&](config::IProfiles& d) {
           touched_current =
               d.current.has_value() && old->uid && *d.current == *old->uid;
           config::profiles::set_profiles_instance(&d);
           auto item = config::PrfItem::from_url(*old->url, old->name,
                                                 old->desc, option);
           config::profiles::set_profiles_instance(nullptr);
           // ← 失败保留原文：from_url 失败时 append_item 不执行，
           //   profiles/原文件均不变（错误上抛通知）
           if (item.is_err()) return;
           item.value().uid = old->uid;  // 同 uid → 更新元数据 + 覆写文件
           auto r = d.append_item(item.value());
           if (r.is_ok()) item_opt = item.value();
         });
         if (!item_opt) {
           return CmdResult::err({errs::kIo, "更新订阅失败（原文已保留）"});
         }
         profiles.apply();
         if (auto save = profiles.data_arc()->save_file(); !save.is_ok()) {
           return CmdResult::err({errs::kIo, save.error().message});
         }
         {
           auto outcome = update_config_forced();
           if (outcome.is_valid()) {
             events::EventBus::refresh_clash();
             if (item_opt->uid)
               events::EventBus::notify_profile_changed(*item_opt->uid);
           } else {
             notify_invalid(outcome);
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
         if (should_update.value() || merged_affected) {
           auto outcome = update_config_forced();
           if (outcome.is_valid()) {
             events::EventBus::refresh_clash();
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
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutConfig});

  // ---- view_profile（← profile.rs:403-420：系统编辑器打开）----
  svc.register_command(
      "view_profile",
      {[](const Json& args) -> CmdResult {
         const Json* index = find_arg(args, {"index"});
         if (!index || !index->is_string()) return CmdResult::err({errs::kBadArgs, "index required"});
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
         // ← save_profile.rs:29-31：merge 按 itype，script 按 itype 或 .js 后缀
         const bool is_merge_file = item->itype.has_value() && *item->itype == "merge";
         const bool is_script_file = (item->itype.has_value() && *item->itype == "script") ||
                                     item->file->size() >= 3 &&
                                         item->file->compare(item->file->size() - 3, 3, ".js") == 0;
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
           // ← save_profile.rs:114-120：按文件种类选通知对象与文案
           const auto target = is_script_file
                                   ? core::validate::ValidationNoticeTarget::Script
                                   : (is_merge_file
                                          ? core::validate::ValidationNoticeTarget::Merge
                                          : core::validate::ValidationNoticeTarget::Runtime);
           const char* file_type =
               is_script_file ? "脚本文件" : (is_merge_file ? "合并配置文件" : "YAML配置文件");
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
           events::EventBus::refresh_clash();
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
         Json arr = Json::array();
         for (auto& [name, delay] : outcome.display) arr.push_back({name, delay});
         return CmdResult::ok(arr);
       },
       consts::kTimeoutHeavy});

  // ---- create_profile（← profile.rs:175-196：新建本地/merge/script 条目）----
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
           } else if (itype == "script") {
             item = config::PrfItem::from_script(std::nullopt);
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
