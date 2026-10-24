#include "config.h"

#include <windows.h>

#include "clash.h"
#include "clashwin/config/constants.h"
#include "migrate.h"
#include "profiles.h"
#include "prfitem.h"
#include "util/dirs.h"
#include "util/log.h"
#include "verge.h"

namespace clashwin::config {

Config& Config::instance() {
  static Config g;
  return g;
}

Config::Config()
    // ← Config::global：Draft::new(IClashTemp::new()/IVerge::new()/IProfiles::new())
    : clash_(clash::load()),
      verge_(verge::load().is_ok() ? verge::load().value() : verge::make_template()),
      profiles_(profiles::load()) {}

bool Config::is_admin() const {
  // ← utils/sysinfo.rs::app_is_admin（管理员组判定，等价 IsUserAnAdmin）
  BOOL is_admin = FALSE;
  PSID admin_group = nullptr;
  SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
  if (AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                               DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                               &admin_group)) {
    CheckTokenMembership(nullptr, admin_group, &is_admin);
    FreeSid(admin_group);
  }
  return is_admin != 0;
}

bool Config::is_service_installed() const {
  // 服务就绪判定属 T07；此处仅探测管道存在（clash_verge_service）
  HANDLE h = CreateFileW(L"\\\\.\\pipe\\clash-verge-service", GENERIC_READ,
                         0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  CloseHandle(h);
  return true;
}

Result<void> Config::init(bool force_save) {
  // 从盘重载三份配置（构造期快照仅是兜底；init 才是真正的启动入口，
  // 语义对齐 Config::global 的 OnceCell 初始化发生在启动序列中的位置）
  {
    auto cv = verge::load();
    IVerge v = cv.is_ok() ? cv.value() : verge::make_template();
    verge_.edit_draft([&v](IVerge& d) { d = v; });
    verge_.apply();

    IClashTemp c = clash::load();
    clash_.edit_draft([&c](IClashTemp& d) { d = c; });
    clash_.apply();

    IProfiles p = profiles::load();
    profiles_.edit_draft([&p](IProfiles& d) { d = p; });
    profiles_.apply();
  }

  // 1. 确保 profiles.yaml + 全局 Merge/Script 保护件（config.rs:134-145）
  {
    auto& draft = profiles_;
    std::shared_ptr<const IProfiles> latest = draft.latest_arc();
    IProfiles profiles = *latest;
    bool needs_items = profiles.get_item(profiles::kGlobalMergeUid) == nullptr ||
                       profiles.get_item(profiles::kGlobalScriptUid) == nullptr;
    if (needs_items) {
      // 注册装配期实例（ensure_enhance_items 的子项追加落点）
      profiles::set_profiles_instance(&profiles);
      auto r = profiles::ensure_default_profile_items(profiles);
      profiles::set_profiles_instance(nullptr);
      if (!r.is_ok()) return r;
      draft.edit_draft([&profiles](IProfiles& d) { d = profiles; });
      draft.apply();
    }
  }

  // 2. 非 admin 且服务未装 → TUN 强制关（config.rs:98-113）
  {
    auto v = verge_.latest_arc();
    bool tun_on = v->enable_tun_mode.value_or(false);
    if (tun_on && !is_admin() && !is_service_installed()) {
      verge_.edit_draft([](IVerge& d) { d.enable_tun_mode = false; });
      verge_.apply();
      auto r = verge::save_verge(*verge_.latest_arc());
      if (!r.is_ok()) CWLOG_WARN("save verge after tun fix failed");
    }
  }

  // 3. 孤立 profile 文件清理（config.rs:122-128）
  {
    auto p = profiles_.latest_arc();
    auto r = p->cleanup_orphaned_files();
    if (!r.is_ok()) CWLOG_WARN("cleanup orphaned profile files failed");
  }

  // 4. 落盘（首启时三份文件必须产出；非首启仅在内容变化时写）
  if (force_save) {
    if (auto r = clash::save_clash(*clash_.latest_arc()); !r.is_ok()) return r;
    if (auto r = verge::save_verge(*verge_.latest_arc()); !r.is_ok()) return r;
    if (auto r = profiles::save_profiles(*profiles_.latest_arc()); !r.is_ok())
      return r;
  }
  return Result<void>::ok();
}

void Config::apply_all_and_save_file() {
  // ← config.rs:274-292：apply + save 三件套（退出序列）
  clash_.apply();
  if (auto r = clash::save_clash(*clash_.latest_arc()); !r.is_ok())
    CWLOG_WARN("save clash.yaml failed: " + r.error().message);

  verge_.apply();
  if (auto r = verge::save_verge(*verge_.latest_arc()); !r.is_ok())
    CWLOG_WARN("save verge.yaml failed: " + r.error().message);

  profiles_.apply();
  if (auto r = profiles::save_profiles(*profiles_.latest_arc()); !r.is_ok())
    CWLOG_WARN("save profiles.yaml failed: " + r.error().message);
}

}  // namespace clashwin::config
