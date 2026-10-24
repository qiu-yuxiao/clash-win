#include "clashwin/config/enhance_chain.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace clashwin::config {

EnhanceChain& EnhanceChain::Instance() {
    static EnhanceChain instance;
    return instance;
}

void EnhanceChain::Init(const std::filesystem::path& app_dir) {
    app_dir_ = app_dir;
    std::filesystem::path prof_dir = app_dir / "profiles";
    std::filesystem::create_directories(prof_dir);
    merge_yaml_path_ = prof_dir / "Merge.yaml";

    if (!std::filesystem::exists(merge_yaml_path_)) {
        // 创建初始 Merge.yaml 模板
        SaveMergeYaml();
    } else {
        LoadMergeYaml();
    }
}

bool EnhanceChain::LoadMergeYaml() {
    if (!std::filesystem::exists(merge_yaml_path_)) {
        return false;
    }

    std::ifstream ifs(merge_yaml_path_);
    if (!ifs.is_open()) return false;

    prepend_rules_.clear();
    append_rules_.clear();

    std::string line;
    enum class Section { None, PrependRules, AppendRules } current_section = Section::None;

    while (std::getline(ifs, line)) {
        // 去除尾部换行
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();

        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        if (trimmed.empty() || trimmed.starts_with('#')) continue;

        if (line.starts_with("prepend-rules:")) {
            current_section = Section::PrependRules;
            continue;
        } else if (line.starts_with("append-rules:")) {
            current_section = Section::AppendRules;
            continue;
        } else if (!line.empty() && line[0] != ' ' && line[0] != '\t' && line.find(':') != std::string::npos) {
            current_section = Section::None;
        }

        if (trimmed.starts_with("- ")) {
            std::string item = trimmed.substr(2);
            item.erase(0, item.find_first_not_of(" \t\"'"));
            size_t last = item.find_last_not_of(" \t\"'");
            if (last != std::string::npos) item = item.substr(0, last + 1);

            if (!item.empty()) {
                if (current_section == Section::PrependRules) {
                    prepend_rules_.push_back(item);
                } else if (current_section == Section::AppendRules) {
                    append_rules_.push_back(item);
                }
            }
        }
    }
    return true;
}

bool EnhanceChain::SaveMergeYaml() {
    std::ofstream ofs(merge_yaml_path_);
    if (!ofs.is_open()) return false;

    ofs << "# ClashWin Enhance Merge.yaml\n";
    ofs << "# 用户自定义与右键分流注入规则\n\n";

    ofs << "prepend-rules:\n";
    for (const auto& r : prepend_rules_) {
        ofs << "  - " << r << "\n";
    }
    ofs << "\n";

    ofs << "append-rules:\n";
    for (const auto& r : append_rules_) {
        ofs << "  - " << r << "\n";
    }
    ofs << "\n";

    return true;
}

std::string EnhanceChain::ApplyEnhance(std::string_view original_yaml) const {
    if (prepend_rules_.empty() && append_rules_.empty()) {
        return std::string(original_yaml);
    }

    std::istringstream iss(std::string{original_yaml});
    std::ostringstream oss;
    std::string line;
    bool in_rules = false;
    bool prepend_done = false;

    while (std::getline(iss, line)) {
        std::string raw_line = line;
        while (!raw_line.empty() && (raw_line.back() == '\r' || raw_line.back() == '\n')) raw_line.pop_back();

        if (raw_line.starts_with("rules:")) {
            oss << raw_line << "\n";
            in_rules = true;
            if (!prepend_done) {
                for (const auto& r : prepend_rules_) {
                    oss << "  - " << r << "\n";
                }
                prepend_done = true;
            }
            continue;
        }

        if (in_rules) {
            // 如果遇到下一个顶层非缩进配置字段
            if (!raw_line.empty() && raw_line[0] != ' ' && raw_line[0] != '\t' && raw_line.find(':') != std::string::npos) {
                // 在 rules 块结束前写入 append_rules_
                for (const auto& r : append_rules_) {
                    oss << "  - " << r << "\n";
                }
                in_rules = false;
            }
        }

        oss << raw_line << "\n";
    }

    // 如果文件末尾仍然在 rules 块中
    if (in_rules) {
        for (const auto& r : append_rules_) {
            oss << "  - " << r << "\n";
        }
    } else if (!prepend_done && (!prepend_rules_.empty() || !append_rules_.empty())) {
        // 如果原配置中没有 rules: 字段，直接追加
        oss << "\nrules:\n";
        for (const auto& r : prepend_rules_) {
            oss << "  - " << r << "\n";
        }
        for (const auto& r : append_rules_) {
            oss << "  - " << r << "\n";
        }
    }

    return oss.str();
}

bool EnhanceChain::InjectRule(const std::string& host_or_ip, const std::string& target, bool prepend) {
    if (host_or_ip.empty() || target.empty()) return false;

    std::string rule_line;

    // 判断是否为 IP 地址 (纯数字、点、冒号或斜杠)
    bool is_ip = true;
    for (char c : host_or_ip) {
        if (!isdigit(static_cast<unsigned char>(c)) && c != '.' && c != ':' && c != '/') {
            is_ip = false;
            break;
        }
    }

    if (is_ip) {
        if (host_or_ip.find('/') != std::string::npos) {
            rule_line = "IP-CIDR," + host_or_ip + "," + target;
        } else {
            rule_line = "IP-CIDR," + host_or_ip + "/32," + target;
        }
    } else {
        std::string domain = host_or_ip;
        if (domain.starts_with("*.")) domain = domain.substr(2);
        else if (domain.starts_with(".")) domain = domain.substr(1);
        rule_line = "DOMAIN-SUFFIX," + domain + "," + target;
    }

    auto& target_list = prepend ? prepend_rules_ : append_rules_;

    // 避免重复规则
    auto it = std::find(target_list.begin(), target_list.end(), rule_line);
    if (it != target_list.end()) {
        target_list.erase(it);
    }

    if (prepend) {
        target_list.insert(target_list.begin(), rule_line);
    } else {
        target_list.push_back(rule_line);
    }

    return SaveMergeYaml();
}

void EnhanceChain::ClearInjectedRules() {
    prepend_rules_.clear();
    append_rules_.clear();
    SaveMergeYaml();
}

} // namespace clashwin::config
