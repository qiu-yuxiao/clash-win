#pragma once
// T09 九个命令文件的安装入口（command_service::install_all 依次调用）
#include <optional>
#include <string>
#include <vector>

#include "util/result.h"

namespace clashwin::config {
struct IVerge;
}

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

// BUG-398：解析随包《轻量模式防误杀指南.txt》路径（exe 根目录 -> resources -> 开发仓库根目录）
std::vector<std::wstring> collect_antivirus_guide_candidates(
    const std::wstring& exe_dir, const std::wstring& resources_dir,
    const std::wstring& dev_workspace_dir);
std::optional<std::wstring> select_existing_guide_path(
    const std::vector<std::wstring>& candidates);
std::optional<std::wstring> resolve_antivirus_guide_path();

}  // namespace clashwin::cmd
