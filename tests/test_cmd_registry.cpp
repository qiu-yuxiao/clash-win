// T09 测试：命令注册表完整性（02§2 lib.rs 注册表 + 任务卡补齐项逐一存在性）
#include "test_util.h"

#include "cmd/command_service.h"

#include <string>
#include <vector>

using namespace clashwin::cmd;

namespace {
// ← lib.rs generate_handler!（裁剪零调用死命令后保留项）
const char* kLibRsCommands[] = {
    "app_is_admin", "is_port_in_use", "get_sys_proxy",
    "show_in_folder", "open_web_url", "export_diagnostic_bundle",
    "restart_core", "get_running_mode", "install_service", "uninstall_service",
    "is_service_available", "get_clash_info", "patch_clash_config", "patch_clash_mode",
    "get_runtime_config", "get_proxy_addr", "save_proxy_head_state", "get_proxy_head_state",
    "notify_node_switched", "get_confirmed_fake_proxy_names",
    "get_clash_logs", "close_all_connections", "get_verge_config", "patch_verge_config",
    "get_auto_launch_status", "trigger_auto_select", "get_profiles",
    "enhance_profiles", "patch_profiles_config", "view_profile", "patch_profile", "import_profile",
    "delete_profile", "read_profile_file", "save_profile_file", "update_profile",
    "check_core_update", "start_core_upgrade",
    "frontend_log",
};
// ← ClashWin 内部/UI 所需命令（lib.rs 未注册但前端/流程需要）
const char* kExtraCommands[] = {
    "start_core", "stop_core", "test_delay", "reinstall_service",
    "repair_service", "entry_lightweight_mode", "exit_lightweight_mode",
    "get_next_update_time",
};
}  // namespace

UT_TEST(cmd_registry_lib_rs_complete) {
  CommandService::instance().install_all();
  int missing = 0;
  for (const char* name : kLibRsCommands) {
    if (!CommandService::instance().exists(name)) {
      ++missing;
      std::printf("  MISSING lib.rs command: %s\n", name);
    }
  }
  UT_CHECK_EQ(missing, 0);
}

UT_TEST(cmd_registry_extras_complete) {
  int missing = 0;
  for (const char* name : kExtraCommands) {
    if (!CommandService::instance().exists(name)) {
      ++missing;
      std::printf("  MISSING extra command: %s\n", name);
    }
  }
  UT_CHECK_EQ(missing, 0);
}

UT_TEST(cmd_registry_counts_and_timeouts) {
  auto& svc = CommandService::instance();
  auto names = svc.command_names();
  std::printf("  registry size = %zu\n", names.size());
  UT_CHECK(names.size() >= 55);
  // 超时梯度抽测（01§5-A：探针 10/15s、配置 30s、内核/导入/测速 60s）
  UT_CHECK_EQ(svc.timeout_of("test_delay"), consts::kTimeoutHeavy);
  UT_CHECK_EQ(svc.timeout_of("import_profile"), consts::kTimeoutHeavy);
  UT_CHECK_EQ(svc.timeout_of("restart_core"), consts::kTimeoutHeavy);
  UT_CHECK_EQ(svc.timeout_of("patch_verge_config"), consts::kTimeoutConfig);
  UT_CHECK_EQ(svc.timeout_of("patch_clash_config"), consts::kTimeoutConfig);
  UT_CHECK_EQ(svc.timeout_of("get_profiles"), consts::kTimeoutProbe);
  UT_CHECK_EQ(svc.timeout_of("is_service_available"), consts::kTimeoutProbeLong);
}

UT_TEST(cmd_unknown_and_stub) {
  auto& svc = CommandService::instance();
  // 未注册命令 → 2901
  auto r = svc.invoke("no_such_cmd");
  UT_CHECK(r.is_err());
  UT_CHECK_EQ(r.error().code, errs::kNotFound);
  UT_CHECK_EQ(errs::kNotImplemented, 2903);
}
