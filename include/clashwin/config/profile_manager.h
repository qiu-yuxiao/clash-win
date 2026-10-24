#pragma once

#include "clashwin/common/types.h"
#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace clashwin::config {

class ProfileManager {
public:
    static ProfileManager& Instance();

    // 初始化配置目录与加载 profiles.yaml
    void Init(const std::filesystem::path& app_dir);

    // 检查是否存在真实订阅项（remote 或 local）
    [[nodiscard]] bool HasRealProfile() const;

    // 检查首次运行标志是否已完成
    [[nodiscard]] bool IsFirstRunCompleted() const;
    void SetFirstRunCompleted(bool completed);

    // 从远程 URL 下载订阅内容 (WinHTTP 异步或同步)
    bool DownloadSubscription(const std::string& url, std::string& out_content, std::string& out_error);

    // 导入并激活新订阅（生成 profiles/{uid}.yaml，写入 profiles.yaml，并生成 config.yaml）
    bool ImportAndActivate(const std::string& url_or_content, std::string& out_error);

    // 切换当前激活的 Profile
    bool SwitchProfile(const std::string& uid, std::string& out_error);

    // 更新指定的订阅
    bool UpdateProfile(const std::string& uid, std::string& out_error);

    // 删除指定的订阅
    bool DeleteProfile(const std::string& uid);

    // 获取多订阅列表与当前激活项
    [[nodiscard]] const std::vector<ProfileItem>& GetProfileItems() const { return profile_items_; }
    [[nodiscard]] const std::string& GetCurrentProfileUid() const { return current_profile_uid_; }

    // 获取当前生成的 config.yaml 所在路径
    [[nodiscard]] std::filesystem::path GetCurrentConfigPath() const;
    [[nodiscard]] std::filesystem::path GetConfigDir() const { return app_dir_; }

    // 获取当前 Profile 的代理节点列表
    [[nodiscard]] const std::vector<ProxyNode>& GetNodes() const { return nodes_; }
    [[nodiscard]] const std::string& GetCurrentActiveNode() const { return current_active_node_; }
    void SetCurrentActiveNode(const std::string& node_name);

    // profiles.yaml 持久化
    bool LoadProfilesYaml();
    bool SaveProfilesYaml();

private:
    ProfileManager() = default;
    ~ProfileManager() = default;

    std::filesystem::path app_dir_;
    std::filesystem::path profiles_yaml_path_;
    std::filesystem::path profiles_dir_;

    std::vector<ProfileItem> profile_items_;
    std::string current_profile_uid_;
    std::vector<ProxyNode> nodes_;
    std::string current_active_node_;
    bool first_run_completed_{false};

    bool ParseSubscriptionContent(const std::string& content, std::vector<ProxyNode>& out_nodes);
    bool GenerateMihomoConfig(const std::vector<ProxyNode>& nodes, const std::filesystem::path& output_path);
    std::string GenerateUid();
};

} // namespace clashwin::config
