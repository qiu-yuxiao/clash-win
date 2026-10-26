// T09 cmd_profile（← cmd/profile.rs + cmd/save_profile.rs 逐命令对照）
#include "cmd/cmd_support.h"

#include "core/switching_lock.h"
#include "core/timer.h"
#include "cmd/command_service.h"
#include "cmd/cmd_install.h"

#include "config/config.h"
#include "config/profiles.h"
#include "config/prfitem.h"
#include "core/validate.h"
#include "enhance/enhance.h"
#include "util/dirs.h"
#include "util/utf.h"

#include <atomic>
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
  events::EventBus::notice_message("config_validate::error", o.message);
}
}  // namespace

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
         auto& profiles = config::Config::instance().profiles();
         std::optional<config::PrfItem> item_opt;
         profiles.edit_draft([&](config::IProfiles& d) {
           config::profiles::set_profiles_instance(&d);
           auto item = config::PrfItem::from_url(url->get<std::string>(),
                                                 std::nullopt, std::nullopt,
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
         auto save = profiles.data_arc()->save_file();
         if (!save.is_ok()) {
           return CmdResult::err({errs::kIo, "导入订阅失败: " + save.error().message});
         }
         auto latest = profiles.latest_arc();
         bool became_current =
             latest->current.has_value() && item_opt->uid &&
             *latest->current == *item_opt->uid;
         if (item_opt->uid)
           events::EventBus::notify_profile_changed(*item_opt->uid);
         if (became_current) {
           // ← profile.rs:132-146：首个导入自动激活
           auto outcome = update_config_forced();
           if (outcome.is_valid()) events::EventBus::refresh_clash();
         }
         return CmdResult::ok(Json::object());
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
         if (auto save = profiles.data_arc()->save_file(); !save.is_ok()) {
           return CmdResult::err({errs::kIo, save.error().message});
         }
         if (touched_current) {
           auto outcome = update_config_forced();
           if (!outcome.is_valid()) notify_invalid(outcome);
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
         Result<bool> should_update = Result<bool>::ok(false);
         profiles.edit_draft([&](config::IProfiles& d) {
           should_update = d.delete_item(index->get<std::string>());
         });
         if (should_update.is_err()) return CmdResult::err(should_update.error());
         if (auto save = profiles.data_arc()->save_file(); !save.is_ok()) {
           return CmdResult::err({errs::kIo, save.error().message});
         }
         if (should_update.value()) {
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
         const config::PrfItem* item = latest->get_item(index->get<std::string>());
         if (!item || !item->file) return CmdResult::err({errs::kBadArgs, "the file field is null"});
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
           notify_invalid(outcome);
           return CmdResult::ok(validation_outcome_to_json(outcome));
         }
         // ← 影响运行时则 update_config_forced
         if (latest->current.has_value() && *latest->current == *item->file) {
           // current 存的是 uid；仅在保存的是当前 profile 文件时刷新
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

  // ---- trigger_auto_select（← cmd/proxy.rs:13-25；后端群发测速+择优。
  //      T09 简版：delay_group 全组测速→择优→可选切换；T22 接 monitor 状态机）----
  svc.register_command(
      "trigger_auto_select",
      {[](const Json& args) -> CmdResult {
         auto client = mihomo();
         if (client.is_err()) return CmdResult::err(client.error());
         bool select = arg_or(args, {"select"}, Json(false)) == true;
         std::string url;
         try {
           const auto& verge = *config::Config::instance().verge().latest_arc();
           url = verge.default_latency_test.value_or("https://cp.cloudflare.com/generate_204");
         } catch (...) {
           url = "https://cp.cloudflare.com/generate_204";
         }
         auto group = client.value()->get_group_by_name("PROXY");
         if (group.is_err()) return CmdResult::err(group.error());
         if (!group.value().all.has_value()) {
           return CmdResult::err({errs::kCoreDown, "PROXY group missing"});
         }
         auto delays = client.value()->delay_group("PROXY", url, 2000, 5000);
         std::vector<std::pair<std::string, uint32_t>> results;
         std::pair<std::string, uint32_t> best{"", UINT32_MAX};
         if (delays.is_ok()) {
           for (const auto& [name, delay] : delays.value()) {
             // 判死口径（附录常量：<30ms 或 ≥2000ms）不参与择优
             bool alive = delay >= 30 && delay < 2000;
             results.push_back({name, delay});
             if (alive && delay < best.second) best = {name, delay};
           }
         }
         if (select && !best.first.empty()) {
           client.value()->select_node_for_group("PROXY", best.first);
         }
         events::EventBus::backend_delay_results("PROXY", results);
         Json arr = Json::array();
         for (auto& [name, delay] : results) arr.push_back({name, delay});
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
