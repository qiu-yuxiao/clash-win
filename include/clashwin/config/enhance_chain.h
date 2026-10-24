#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace clashwin::config {

struct EnhanceRuleItem {
    std::string rule_type; // DOMAIN, DOMAIN-SUFFIX, IP-CIDR, MATCH, etc.
    std::string payload;   // e.g. "openai.com", "1.1.1.1/32"
    std::string target;    // DIRECT, PROXY, REJECT
};

class EnhanceChain {
public:
    static EnhanceChain& Instance();

    void Init(const std::filesystem::path& app_dir);

    // 加载与保存 Merge.yaml
    bool LoadMergeYaml();
    bool SaveMergeYaml();

    // 应用增强链对原始 Clash YAML 配置进行编译融合 (Prepend/Append Rules, Deep Merge)
    [[nodiscard]] std::string ApplyEnhance(std::string_view original_yaml) const;

    // 快捷注入规则 (用于 Slice 8 连接面板右键快速分流)
    // host_or_ip: 域名或 IP 地址；target: "DIRECT" / "PROXY" / "REJECT"
    bool InjectRule(const std::string& host_or_ip, const std::string& target, bool prepend = true);

    // 获取当前注入的所有规则列表
    [[nodiscard]] const std::vector<std::string>& GetPrependRules() const { return prepend_rules_; }
    [[nodiscard]] const std::vector<std::string>& GetAppendRules() const { return append_rules_; }

    // 清空或重置注入规则
    void ClearInjectedRules();

private:
    EnhanceChain() = default;
    ~EnhanceChain() = default;

    std::filesystem::path app_dir_;
    std::filesystem::path merge_yaml_path_;

    std::vector<std::string> prepend_rules_;
    std::vector<std::string> append_rules_;
    std::vector<std::string> prepend_proxies_;
    std::vector<std::string> append_proxies_;
};

} // namespace clashwin::config
