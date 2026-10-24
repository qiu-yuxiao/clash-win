#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include "clashwin/config/profile_manager.h"
#include "clashwin/config/node_filter.h"
#include "clashwin/config/tun_compiler.h"
#include "clashwin/config/universal_parser.h"
#include "clashwin/config/enhance_chain.h"
#include "clashwin/config/app_config.h"
#include "clashwin/common/constants.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>
#include <chrono>
#include <random>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

namespace clashwin::config {

static std::string Trim(std::string_view s) {
    size_t start = s.find_first_not_of(" \t\r\n\"'");
    if (start == std::string_view::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n\"'");
    return std::string(s.substr(start, end - start + 1));
}

ProfileManager& ProfileManager::Instance() {
    static ProfileManager instance;
    return instance;
}

std::string ProfileManager::GenerateUid() {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 rng(static_cast<unsigned int>(now));
    std::uniform_int_distribution<uint32_t> dist(100000, 999999);
    return "p_" + std::to_string(dist(rng));
}

void ProfileManager::Init(const std::filesystem::path& app_dir) {
    app_dir_ = app_dir;
    profiles_yaml_path_ = app_dir_ / "profiles.yaml";
    profiles_dir_ = app_dir_ / "profiles";

    std::filesystem::create_directories(app_dir_);
    std::filesystem::create_directories(profiles_dir_);

    std::filesystem::path flag_path = app_dir_ / ".first_run_completed";
    first_run_completed_ = std::filesystem::exists(flag_path);

    EnhanceChain::Instance().Init(app_dir_);

    LoadProfilesYaml();

    // 如果 profiles.yaml 中指定了当前激活项，则加载该配置
    if (!current_profile_uid_.empty()) {
        for (const auto& item : profile_items_) {
            if (item.uid == current_profile_uid_) {
                std::filesystem::path pFile = profiles_dir_ / item.file;
                if (std::filesystem::exists(pFile)) {
                    std::ifstream ifs(pFile);
                    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                    ParseSubscriptionContent(content, nodes_);
                    current_active_node_ = item.selected_proxy;
                    if (current_active_node_.empty() && !nodes_.empty()) {
                        current_active_node_ = nodes_[0].name;
                    }
                }
                break;
            }
        }
    }
}

bool ProfileManager::HasRealProfile() const {
    for (const auto& item : profile_items_) {
        if (item.type == "remote" || item.type == "local") {
            return true;
        }
    }
    return !nodes_.empty();
}

bool ProfileManager::IsFirstRunCompleted() const {
    return first_run_completed_;
}

void ProfileManager::SetFirstRunCompleted(bool completed) {
    first_run_completed_ = completed;
    std::filesystem::path flag_path = app_dir_ / ".first_run_completed";
    if (completed) {
        std::ofstream ofs(flag_path);
        ofs << "1";
    } else {
        std::filesystem::remove(flag_path);
    }
}

void ProfileManager::SetCurrentActiveNode(const std::string& node_name) {
    current_active_node_ = node_name;
    for (auto& item : profile_items_) {
        if (item.uid == current_profile_uid_) {
            item.selected_proxy = node_name;
            break;
        }
    }
    SaveProfilesYaml();
}

bool ProfileManager::LoadProfilesYaml() {
    profile_items_.clear();
    if (!std::filesystem::exists(profiles_yaml_path_)) {
        return false;
    }

    std::ifstream ifs(profiles_yaml_path_);
    if (!ifs.is_open()) return false;

    std::string line;
    ProfileItem current_item;
    bool in_item = false;

    while (std::getline(ifs, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.starts_with("current:")) {
            current_profile_uid_ = Trim(trimmed.substr(8));
        } else if (trimmed.starts_with("- uid:")) {
            if (in_item && !current_item.uid.empty()) {
                profile_items_.push_back(current_item);
                current_item = ProfileItem{};
            }
            in_item = true;
            current_item.uid = Trim(trimmed.substr(6));
        } else if (in_item) {
            auto colon = trimmed.find(':');
            if (colon != std::string::npos) {
                std::string k = Trim(trimmed.substr(0, colon));
                std::string v = Trim(trimmed.substr(colon + 1));
                if (k == "type") current_item.type = v;
                else if (k == "name") current_item.name = v;
                else if (k == "url") current_item.url = v;
                else if (k == "file") current_item.file = v;
                else if (k == "selected_proxy") current_item.selected_proxy = v;
                else if (k == "updated") {
                    try { current_item.updated_at = std::stoll(v); } catch (...) {}
                }
            }
        }
    }

    if (in_item && !current_item.uid.empty()) {
        profile_items_.push_back(current_item);
    }

    return true;
}

bool ProfileManager::SaveProfilesYaml() {
    std::ofstream ofs(profiles_yaml_path_);
    if (!ofs.is_open()) return false;

    ofs << "# ClashWin Profiles Management\n";
    ofs << "config_version: 1\n";
    ofs << "current: \"" << current_profile_uid_ << "\"\n";
    ofs << "items:\n";

    for (const auto& item : profile_items_) {
        ofs << "  - uid: \"" << item.uid << "\"\n";
        ofs << "    type: \"" << item.type << "\"\n";
        ofs << "    name: \"" << item.name << "\"\n";
        ofs << "    file: \"" << item.file << "\"\n";
        if (!item.url.empty()) ofs << "    url: \"" << item.url << "\"\n";
        if (!item.selected_proxy.empty()) ofs << "    selected_proxy: \"" << item.selected_proxy << "\"\n";
        ofs << "    updated: " << item.updated_at << "\n";
    }

    return true;
}

bool ProfileManager::DownloadSubscription(const std::string& url_str, std::string& out_content, std::string& out_error) {
    std::wstring wUrl(url_str.begin(), url_str.end());

    URL_COMPONENTS urlComp = {sizeof(urlComp)};
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        out_error = "订阅 URL 格式非法";
        return false;
    }

    std::wstring wHost(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring wPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.dwExtraInfoLength > 0) {
        wPath.append(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }

    std::string host;
    host.reserve(wHost.length());
    for (wchar_t wc : wHost) {
        host.push_back(static_cast<char>(wc));
    }
    // SSRF 防御
    if (host == "localhost" || host.starts_with("127.") || host.starts_with("192.168.") || host.starts_with("10.")) {
        out_error = "安全拦截：禁止订阅指向本地或局域网私有地址 (SSRF)";
        return false;
    }

    HINTERNET hSession = WinHttpOpen(L"ClashWin/v1.0 (Windows NT 10.0; Win64; x64)",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        out_error = "初始化 WinHTTP 失败";
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        out_error = "连接订阅服务器失败: " + host;
        return false;
    }

    DWORD dwFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(),
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        out_error = "创建 HTTP 请求失败";
        return false;
    }

    WinHttpSetTimeouts(hRequest, 5000, 10000, 15000, 15000);

    DWORD opt = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &opt, sizeof(opt));

    BOOL bResults = WinHttpSendRequest(hRequest,
                                       WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       WINHTTP_NO_REQUEST_DATA, 0,
                                       0, 0);
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (!bResults) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        out_error = "请求订阅超时或无网络响应";
        return false;
    }

    std::string response_data;
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;

        std::vector<char> buffer(dwSize + 1, 0);
        if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
            response_data.append(buffer.data(), dwDownloaded);
        }
    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (response_data.empty()) {
        out_error = "订阅服务器返回内容为空";
        return false;
    }

    out_content = std::move(response_data);
    return true;
}

bool ProfileManager::ParseSubscriptionContent(const std::string& content, std::vector<ProxyNode>& out_nodes) {
    std::vector<ProxyNode> raw_nodes;

    if (content.find("proxies:") != std::string::npos) {
        std::istringstream iss(content);
        std::string line;
        bool in_proxies = false;
        while (std::getline(iss, line)) {
            if (line.find("proxies:") != std::string::npos) {
                in_proxies = true;
                continue;
            }
            if (in_proxies && line.find("proxy-groups:") != std::string::npos) {
                break;
            }
            if (in_proxies && line.find("- name:") != std::string::npos) {
                auto pos = line.find("- name:");
                std::string name = line.substr(pos + 7);
                name.erase(0, name.find_first_not_of(" \""));
                name.erase(name.find_last_not_of(" \"") + 1);
                if (!name.empty()) {
                    ProxyNode n;
                    n.name = name;
                    n.type = "yaml";
                    raw_nodes.push_back(n);
                }
            }
        }
    } else {
        std::vector<ParsedProxyNode> parsed_nodes = UniversalParser::ParseUriListToNodes(content);
        for (const auto& pn : parsed_nodes) {
            ProxyNode n;
            n.name = pn.name;
            n.type = pn.type;
            raw_nodes.push_back(n);
        }
    }

    // 过滤废弃与广告节点 (严格遵循规范：英文匹配，中文仅前缀匹配)
    out_nodes.clear();
    for (const auto& node : raw_nodes) {
        if (!NodeFilter::IsDummyNode(node.name)) {
            out_nodes.push_back(node);
        }
    }

    return !out_nodes.empty();
}

bool ProfileManager::GenerateMihomoConfig(const std::vector<ProxyNode>& nodes, const std::filesystem::path& output_path) {
    std::ostringstream base;
    base << "mixed-port: 10801\n";
    base << "allow-lan: false\n";
    base << "mode: rule\n";
    base << "log-level: info\n";
    base << "ipv6: false\n";
    base << "external-controller: 127.0.0.1:9097\n";
    base << "secret: \"\"\n\n";

    base << "proxies:\n";
    for (const auto& node : nodes) {
        base << "  - name: \"" << node.name << "\"\n";
        base << "    type: " << (node.type.empty() ? "vmess" : node.type) << "\n";
        base << "    server: 1.1.1.1\n";
        base << "    port: 443\n";
        base << "    uuid: b831381d-6324-4d53-ad4f-8cda48b30811\n";
        base << "    alterId: 0\n";
        base << "    cipher: auto\n";
        base << "    udp: true\n";
    }
    base << "\n";
    base << "proxy-groups:\n";
    base << "  - name: PROXY\n";
    base << "    type: select\n";
    base << "    proxies:\n";
    for (const auto& node : nodes) {
        base << "      - \"" << node.name << "\"\n";
    }
    base << "\n";
    base << "rules:\n";
    base << "  - DOMAIN-SUFFIX,local,DIRECT\n";
    base << "  - IP-CIDR,127.0.0.0/8,DIRECT\n";
    base << "  - IP-CIDR,172.16.0.0/12,DIRECT\n";
    base << "  - IP-CIDR,192.168.0.0/16,DIRECT\n";
    base << "  - IP-CIDR,10.0.0.0/8,DIRECT\n";
    base << "  - MATCH,PROXY\n";

    // 增强链融合 Merge.yaml (包含自定义 rules、prepend-rules、append-rules)
    std::string enhanced = EnhanceChain::Instance().ApplyEnhance(base.str());

    TunCompilerOptions opts;
    opts.enable_tun = AppConfig::Instance().IsTunModeEnabled();
    opts.user_ipv6 = AppConfig::Instance().IsIpv6Enabled();
    opts.rule_fallback = AppConfig::Instance().GetRuleFallback();

    std::string compiled = TunCompiler::CompileConfig(enhanced, opts);
    std::ofstream ofs(output_path);
    if (!ofs.is_open()) return false;
    ofs << compiled;
    return true;
}

bool ProfileManager::ImportAndActivate(const std::string& input, std::string& out_error) {
    std::string content;
    std::string item_url;
    std::string item_type = "local";

    if (input.starts_with("http://") || input.starts_with("https://")) {
        item_type = "remote";
        item_url = input;
        if (!DownloadSubscription(input, content, out_error)) {
            return false;
        }
    } else {
        content = input;
    }

    std::vector<ProxyNode> parsed;
    if (!ParseSubscriptionContent(content, parsed)) {
        out_error = "无法从订阅中解析出任何有效代理节点（可能均为广告/到期通知节点）";
        return false;
    }

    std::string uid = GenerateUid();
    std::string fileName = uid + ".yaml";
    std::filesystem::path profileFilePath = profiles_dir_ / fileName;

    // 保存独立 Profile 文件
    std::ofstream pofs(profileFilePath);
    if (!pofs.is_open()) {
        out_error = "创建 Profile 独立文件失败: " + profileFilePath.string();
        return false;
    }
    pofs << content;
    pofs.close();

    // 生成内核主配置 config.yaml
    std::filesystem::path cfg_file = GetCurrentConfigPath();
    if (!GenerateMihomoConfig(parsed, cfg_file)) {
        out_error = "写入内核配置文件 config.yaml 失败";
        return false;
    }

    // 记录 ProfileItem
    ProfileItem item;
    item.uid = uid;
    item.type = item_type;
    item.name = (item_type == "remote") ? "订阅 " + uid : "本地配置 " + uid;
    item.url = item_url;
    item.file = fileName;
    item.updated_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!parsed.empty()) {
        item.selected_proxy = parsed[0].name;
    }

    profile_items_.push_back(item);
    current_profile_uid_ = uid;
    nodes_ = std::move(parsed);
    if (!nodes_.empty()) {
        current_active_node_ = nodes_[0].name;
    }

    SaveProfilesYaml();
    SetFirstRunCompleted(true);
    return true;
}

bool ProfileManager::SwitchProfile(const std::string& uid, std::string& out_error) {
    for (auto& item : profile_items_) {
        if (item.uid == uid) {
            std::filesystem::path pFile = profiles_dir_ / item.file;
            if (!std::filesystem::exists(pFile)) {
                out_error = "Profile 目标文件不存在: " + pFile.string();
                return false;
            }

            std::ifstream ifs(pFile);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

            std::vector<ProxyNode> parsed;
            if (!ParseSubscriptionContent(content, parsed)) {
                out_error = "解析 Profile 文件失败";
                return false;
            }

            // 重新生成 config.yaml
            if (!GenerateMihomoConfig(parsed, GetCurrentConfigPath())) {
                out_error = "生成内核配置失败";
                return false;
            }

            current_profile_uid_ = uid;
            nodes_ = std::move(parsed);

            // 恢复快照
            if (!item.selected_proxy.empty()) {
                current_active_node_ = item.selected_proxy;
            } else if (!nodes_.empty()) {
                current_active_node_ = nodes_[0].name;
                item.selected_proxy = current_active_node_;
            }

            SaveProfilesYaml();
            return true;
        }
    }
    out_error = "未找到指定的 Profile: " + uid;
    return false;
}

bool ProfileManager::UpdateProfile(const std::string& uid, std::string& out_error) {
    for (auto& item : profile_items_) {
        if (item.uid == uid) {
            if (item.type != "remote" || item.url.empty()) {
                out_error = "非远程订阅无法更新";
                return false;
            }

            std::string content;
            if (!DownloadSubscription(item.url, content, out_error)) {
                return false;
            }

            std::vector<ProxyNode> parsed;
            if (!ParseSubscriptionContent(content, parsed)) {
                out_error = "更新后解析节点为空";
                return false;
            }

            std::filesystem::path pFile = profiles_dir_ / item.file;
            std::ofstream pofs(pFile);
            pofs << content;
            pofs.close();

            item.updated_at = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // 若当前激活正是该 Profile，同步更新内核
            if (current_profile_uid_ == uid) {
                GenerateMihomoConfig(parsed, GetCurrentConfigPath());
                nodes_ = std::move(parsed);
            }

            SaveProfilesYaml();
            return true;
        }
    }
    out_error = "找不到指定的 Profile: " + uid;
    return false;
}

bool ProfileManager::DeleteProfile(const std::string& uid) {
    auto it = std::find_if(profile_items_.begin(), profile_items_.end(), [&](const ProfileItem& item) {
        return item.uid == uid;
    });

    if (it != profile_items_.end()) {
        std::filesystem::remove(profiles_dir_ / it->file);
        profile_items_.erase(it);

        if (current_profile_uid_ == uid) {
            if (!profile_items_.empty()) {
                std::string err;
                SwitchProfile(profile_items_[0].uid, err);
            } else {
                current_profile_uid_ = "";
                nodes_.clear();
                current_active_node_ = "";
            }
        }
        SaveProfilesYaml();
        return true;
    }
    return false;
}

std::filesystem::path ProfileManager::GetCurrentConfigPath() const {
    return app_dir_ / "config.yaml";
}

} // namespace clashwin::config
