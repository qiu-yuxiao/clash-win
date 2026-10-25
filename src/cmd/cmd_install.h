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
void install_cmd_network(CommandService& svc);
void install_cmd_proxy(CommandService& svc);
void install_cmd_app(CommandService& svc);

}  // namespace clashwin::cmd
