#include "clashwin/core/connection_manager.h"
#include "clashwin/core/mihomo_client.h"
#include "clashwin/config/enhance_chain.h"
#include <sstream>
#include <algorithm>
#include <iostream>

namespace clashwin::core {

namespace {

std::string FindJsonString(std::string_view json, std::string_view key) {
    std::string needle = "\"" + std::string(key) + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string_view::npos) return "";

    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) {
        pos++;
    }
    if (pos >= json.size() || json[pos] != '"') return "";

    pos++;
    size_t end = json.find('"', pos);
    if (end == std::string_view::npos) return "";
    return std::string(json.substr(pos, end - pos));
}

uint64_t FindJsonNumber(std::string_view json, std::string_view key) {
    std::string needle = "\"" + std::string(key) + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string_view::npos) return 0;

    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) {
        pos++;
    }
    if (pos >= json.size()) return 0;

    // 数值可能带引号也可能不带
    if (json[pos] == '"') {
        pos++;
        size_t end = json.find('"', pos);
        if (end == std::string_view::npos) return 0;
        try {
            return std::stoull(std::string(json.substr(pos, end - pos)));
        } catch (...) {
            return 0;
        }
    } else {
        size_t end = pos;
        while (end < json.size() && (isdigit(static_cast<unsigned char>(json[end])))) {
            end++;
        }
        try {
            return std::stoull(std::string(json.substr(pos, end - pos)));
        } catch (...) {
            return 0;
        }
    }
}

} // namespace

ConnectionManager& ConnectionManager::Instance() {
    static ConnectionManager instance;
    return instance;
}

bool ConnectionManager::Refresh(uint16_t port) {
    std::string resp;
    if (!MihomoClient::Instance().GetConnectionsRaw(resp, port)) {
        return false;
    }
    ParseConnectionsJson(resp);
    return true;
}

std::vector<ConnectionItem> ConnectionManager::GetConnections() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_;
}

void ConnectionManager::ParseConnectionsJson(const std::string& json_str) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.clear();

    download_total_ = FindJsonNumber(json_str, "downloadTotal");
    upload_total_ = FindJsonNumber(json_str, "uploadTotal");

    size_t conns_start = json_str.find("\"connections\":");
    if (conns_start == std::string_view::npos) return;

    size_t arr_start = json_str.find('[', conns_start);
    if (arr_start == std::string_view::npos) return;

    size_t pos = arr_start + 1;
    uint64_t total_down_speed = 0;
    uint64_t total_up_speed = 0;

    while (pos < json_str.size()) {
        size_t obj_start = json_str.find('{', pos);
        if (obj_start == std::string::npos) break;

        // 寻找匹配的花括号
        int depth = 1;
        size_t obj_end = obj_start + 1;
        while (obj_end < json_str.size() && depth > 0) {
            if (json_str[obj_end] == '{') depth++;
            else if (json_str[obj_end] == '}') depth--;
            obj_end++;
        }

        std::string_view item_json(json_str.data() + obj_start, obj_end - obj_start);
        pos = obj_end;

        ConnectionItem item;
        item.id = FindJsonString(item_json, "id");
        if (item.id.empty()) continue;

        item.upload = FindJsonNumber(item_json, "upload");
        item.download = FindJsonNumber(item_json, "download");
        item.upload_speed = FindJsonNumber(item_json, "curUpload");
        item.download_speed = FindJsonNumber(item_json, "curDownload");
        item.start_time = FindJsonString(item_json, "start");
        item.rule = FindJsonString(item_json, "rule");
        item.rule_payload = FindJsonString(item_json, "rulePayload");

        total_down_speed += item.download_speed;
        total_up_speed += item.upload_speed;

        // metadata block
        size_t meta_pos = item_json.find("\"metadata\":");
        if (meta_pos != std::string_view::npos) {
            size_t meta_start = item_json.find('{', meta_pos);
            if (meta_start != std::string_view::npos) {
                size_t meta_end = item_json.find('}', meta_start);
                if (meta_end != std::string_view::npos) {
                    std::string_view meta_json = item_json.substr(meta_start, meta_end - meta_start + 1);
                    item.network = FindJsonString(meta_json, "network");
                    item.type = FindJsonString(meta_json, "type");
                    item.source_ip = FindJsonString(meta_json, "sourceIP");
                    item.source_port = static_cast<uint16_t>(FindJsonNumber(meta_json, "sourcePort"));
                    item.dest_ip = FindJsonString(meta_json, "destinationIP");
                    item.dest_port = static_cast<uint16_t>(FindJsonNumber(meta_json, "destinationPort"));
                    item.host = FindJsonString(meta_json, "host");
                    item.process = FindJsonString(meta_json, "process");
                    item.process_path = FindJsonString(meta_json, "processPath");
                }
            }
        }

        // chains
        size_t chains_pos = item_json.find("\"chains\":");
        if (chains_pos != std::string_view::npos) {
            size_t c_start = item_json.find('[', chains_pos);
            size_t c_end = item_json.find(']', c_start);
            if (c_start != std::string_view::npos && c_end != std::string_view::npos) {
                std::string_view c_str = item_json.substr(c_start + 1, c_end - c_start - 1);
                size_t q1 = 0;
                while ((q1 = c_str.find('"', q1)) != std::string_view::npos) {
                    size_t q2 = c_str.find('"', q1 + 1);
                    if (q2 == std::string_view::npos) break;
                    item.chains.emplace_back(c_str.substr(q1 + 1, q2 - q1 - 1));
                    q1 = q2 + 1;
                }
            }
        }

        connections_.push_back(std::move(item));
    }

    cur_download_speed_ = total_down_speed;
    cur_upload_speed_ = total_up_speed;
}

bool ConnectionManager::CloseConnection(const std::string& id, uint16_t port) {
    bool ok = MihomoClient::Instance().CloseConnection(id, port);
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(connections_.begin(), connections_.end(), [&](const ConnectionItem& item) {
        return item.id == id;
    });
    if (it != connections_.end()) {
        connections_.erase(it);
    }
    return ok;
}

bool ConnectionManager::CloseAllConnections(uint16_t port) {
    bool ok = MihomoClient::Instance().CloseAllConnections(port);
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.clear();
    cur_download_speed_ = 0;
    cur_upload_speed_ = 0;
    return ok;
}

bool ConnectionManager::RouteConnection(const std::string& conn_id, const std::string& target, bool use_domain, uint16_t port) {
    std::string identifier;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(connections_.begin(), connections_.end(), [&](const ConnectionItem& item) {
            return item.id == conn_id;
        });
        if (it == connections_.end()) return false;

        if (use_domain && !it->host.empty()) {
            identifier = it->host;
        } else {
            identifier = it->dest_ip;
        }
    }

    if (identifier.empty()) return false;

    // 注入最高优先级分流规则并回写 Merge.yaml
    bool injected = config::EnhanceChain::Instance().InjectRule(identifier, target, true);
    if (!injected) return false;

    // 关闭该连接，促使进程下一次握手即刻走新注入的分流规则
    CloseConnection(conn_id, port);

    return true;
}

} // namespace clashwin::core
