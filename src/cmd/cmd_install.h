#pragma once
// T09 九个命令文件的安装入口（command_service::install_all 依次调用）
namespace clashwin::cmd {

class CommandService;

void install_cmd_profile(CommandService& svc);
void install_cmd_clash(CommandService& svc);
void install_cmd_verge(CommandService& svc);
void install_cmd_system(CommandService& svc);
void install_cmd_service(CommandService& svc);
void install_cmd_lightweight(CommandService& svc);

// T16：DelayManager/AutoSelect 生产 hooks 绑定（cmd_profile 实现，
// cmd_clash 的 test_group_delay 复用）
void ensure_t16_hooks();
void install_cmd_network(CommandService& svc);
void install_cmd_proxy(CommandService& svc);
void install_cmd_app(CommandService& svc);

}  // namespace clashwin::cmd
