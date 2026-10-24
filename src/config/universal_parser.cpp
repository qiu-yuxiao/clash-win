#include "clashwin/config/universal_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>

namespace clashwin::config {

namespace {

// Base64 字符转换查找表
static const std::string BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline bool IsBase64Char(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/') || (c == '-') || (c == '_'));
}

inline int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 简易从 JSON 串中提取字符串或数值字段
std::string ExtractJsonField(std::string_view json, std::string_view key) {
    std::string needle = "\"" + std::string(key) + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string_view::npos) return "";

    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) {
        pos++;
    }
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        pos++;
        size_t end = json.find('"', pos);
        if (end == std::string_view::npos) return "";
        return std::string(json.substr(pos, end - pos));
    } else {
        // 数值或布尔值
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ' && json[end] != '\r' && json[end] != '\n') {
            end++;
        }
        return std::string(json.substr(pos, end - pos));
    }
}

} // namespace

std::optional<std::string> UniversalParser::DecodeBase64Robust(std::string_view input) {
    std::string clean;
    clean.reserve(input.size());

    // 剔除所有空白与换行字符，同时归一化 URL-safe base64 (- -> +, _ -> /)
    for (char c : input) {
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        if (c == '-') clean.push_back('+');
        else if (c == '_') clean.push_back('/');
        else if (c == '=' || IsBase64Char(static_cast<unsigned char>(c))) clean.push_back(c);
    }

    if (clean.empty()) return std::nullopt;

    // 自动补齐末尾缺失的 '='
    while (clean.size() % 4 != 0) {
        clean.push_back('=');
    }

    std::string out;
    out.reserve(clean.size() * 3 / 4);

    int val = 0;
    int valb = -8;
    for (char c : clean) {
        if (c == '=') break;
        size_t idx = BASE64_CHARS.find(c);
        if (idx == std::string::npos) {
            // 非法字符跳过或尝试继续
            continue;
        }
        val = (val << 6) + static_cast<int>(idx);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return out.empty() ? std::nullopt : std::optional<std::string>(out);
}

std::string UniversalParser::UrlDecode(std::string_view input) {
    std::string res;
    res.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            int h1 = HexValue(input[i + 1]);
            int h2 = HexValue(input[i + 2]);
            if (h1 != -1 && h2 != -1) {
                res.push_back(static_cast<char>((h1 << 4) | h2));
                i += 2;
                continue;
            }
        } else if (input[i] == '+') {
            res.push_back(' ');
            continue;
        }
        res.push_back(input[i]);
    }
    return res;
}

std::map<std::string, std::string> UniversalParser::ParseQueryParams(std::string_view query) {
    std::map<std::string, std::string> params;
    size_t start = 0;
    while (start < query.size()) {
        size_t amp = query.find('&', start);
        if (amp == std::string_view::npos) amp = query.size();
        std::string_view pair = query.substr(start, amp - start);
        start = amp + 1;

        if (pair.empty()) continue;
        size_t eq = pair.find('=');
        if (eq == std::string_view::npos) {
            std::string k(pair);
            std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            params[k] = "";
        } else {
            std::string k(pair.substr(0, eq));
            std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            params[k] = UrlDecode(pair.substr(eq + 1));
        }
    }
    return params;
}

std::optional<std::pair<std::string, uint16_t>> UniversalParser::ExtractHostPort(std::string_view host_port, uint16_t default_port) {
    while (!host_port.empty() && (host_port.front() == ' ' || host_port.front() == '\t')) host_port.remove_prefix(1);
    while (!host_port.empty() && (host_port.back() == ' ' || host_port.back() == '\t')) host_port.remove_suffix(1);
    if (host_port.empty()) return std::nullopt;

    if (host_port.front() == '[') {
        size_t close = host_port.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        std::string host(host_port.substr(1, close - 1));
        std::string_view rest = host_port.substr(close + 1);
        uint16_t port = default_port;
        if (rest.starts_with(':')) {
            try {
                port = static_cast<uint16_t>(std::stoi(std::string(rest.substr(1))));
            } catch (...) {}
        }
        return std::make_pair(host, port);
    } else {
        size_t colon = host_port.rfind(':');
        if (colon != std::string_view::npos) {
            std::string host(host_port.substr(0, colon));
            uint16_t port = default_port;
            try {
                port = static_cast<uint16_t>(std::stoi(std::string(host_port.substr(colon + 1))));
            } catch (...) {}
            return std::make_pair(host, port);
        } else {
            return std::make_pair(std::string(host_port), default_port);
        }
    }
}

std::string UniversalParser::NormalizeBandwidth(std::string_view value) {
    if (value.empty()) return "";
    bool all_digits = true;
    for (char c : value) {
        if (!isdigit(static_cast<unsigned char>(c))) {
            all_digits = false;
            break;
        }
    }
    if (all_digits) {
        return std::string(value) + " Mbps";
    }
    return std::string(value);
}

std::string ParsedProxyNode::ToYaml(int indent_spaces) const {
    std::string ind(indent_spaces, ' ');
    std::string ind2(indent_spaces + 2, ' ');
    std::ostringstream ss;
    ss << ind << "- name: \"" << name << "\"\n";
    ss << ind2 << "type: " << type << "\n";
    ss << ind2 << "server: " << server << "\n";
    ss << ind2 << "port: " << port << "\n";

    if (!uuid.empty()) ss << ind2 << "uuid: " << uuid << "\n";
    if (!password.empty()) ss << ind2 << "password: " << password << "\n";
    if (!cipher.empty()) ss << ind2 << "cipher: " << cipher << "\n";
    if (alter_id > 0) ss << ind2 << "alterId: " << alter_id << "\n";
    if (udp) ss << ind2 << "udp: true\n";
    if (tls) ss << ind2 << "tls: true\n";
    if (skip_cert_verify) ss << ind2 << "skip-cert-verify: true\n";
    if (!sni.empty()) ss << ind2 << "sni: " << sni << "\n";
    if (!flow.empty()) ss << ind2 << "flow: " << flow << "\n";
    if (!network.empty() && network != "tcp") {
        ss << ind2 << "network: " << network << "\n";
        if (network == "ws") {
            ss << ind2 << "ws-opts:\n";
            if (!path.empty()) ss << ind2 << "  path: " << path << "\n";
            if (!host_header.empty()) {
                ss << ind2 << "  headers:\n";
                ss << ind2 << "    Host: " << host_header << "\n";
            }
        } else if (network == "grpc") {
            ss << ind2 << "grpc-opts:\n";
            if (!path.empty()) ss << ind2 << "  grpc-service-name: " << path << "\n";
        }
    }

    if (!reality_public_key.empty()) {
        ss << ind2 << "reality-opts:\n";
        ss << ind2 << "  public-key: " << reality_public_key << "\n";
        if (!reality_short_id.empty()) ss << ind2 << "  short-id: " << reality_short_id << "\n";
    }

    if (!client_fingerprint.empty()) ss << ind2 << "client-fingerprint: " << client_fingerprint << "\n";
    if (!obfs.empty()) ss << ind2 << "obfs: " << obfs << "\n";
    if (!obfs_password.empty()) ss << ind2 << "obfs-password: " << obfs_password << "\n";
    if (!up.empty()) ss << ind2 << "up: " << up << "\n";
    if (!down.empty()) ss << ind2 << "down: " << down << "\n";
    if (!protocol.empty()) ss << ind2 << "protocol: " << protocol << "\n";
    if (!alpn.empty()) ss << ind2 << "alpn: [" << alpn << "]\n";
    if (!private_key.empty()) ss << ind2 << "private-key: " << private_key << "\n";
    if (!public_key.empty()) ss << ind2 << "public-key: " << public_key << "\n";
    if (!ip.empty()) ss << ind2 << "ip: " << ip << "\n";
    if (!ipv6.empty()) ss << ind2 << "ipv6: " << ipv6 << "\n";
    if (!reserved.empty()) ss << ind2 << "reserved: [" << reserved << "]\n";
    if (mtu > 0) ss << ind2 << "mtu: " << mtu << "\n";
    if (!preshared_key.empty()) ss << ind2 << "pre-shared-key: " << preshared_key << "\n";
    if (!username.empty()) ss << ind2 << "username: " << username << "\n";

    return ss.str();
}

std::optional<ParsedProxyNode> UniversalParser::ParseVmess(std::string_view link) {
    if (!link.starts_with("vmess://")) return std::nullopt;
    std::string_view payload = link.substr(8);
    auto decoded_opt = DecodeBase64Robust(payload);
    if (!decoded_opt) return std::nullopt;
    std::string_view json = *decoded_opt;

    ParsedProxyNode node;
    node.type = "vmess";
    node.name = ExtractJsonField(json, "ps");
    if (node.name.empty()) node.name = "VMess Node";
    node.server = ExtractJsonField(json, "add");
    std::string port_str = ExtractJsonField(json, "port");
    node.port = port_str.empty() ? 443 : static_cast<uint16_t>(std::atoi(port_str.c_str()));
    node.uuid = ExtractJsonField(json, "id");
    std::string aid_str = ExtractJsonField(json, "aid");
    node.alter_id = aid_str.empty() ? 0 : static_cast<uint32_t>(std::atoi(aid_str.c_str()));
    node.cipher = "auto";
    node.udp = true;

    std::string tls_str = ExtractJsonField(json, "tls");
    if (tls_str == "tls") node.tls = true;

    node.network = ExtractJsonField(json, "net");
    node.host_header = ExtractJsonField(json, "host");
    node.path = ExtractJsonField(json, "path");
    node.sni = ExtractJsonField(json, "sni");
    if (node.sni.empty()) node.sni = node.host_header;

    return node;
}

std::optional<ParsedProxyNode> UniversalParser::ParseVless(std::string_view link) {
    if (!link.starts_with("vless://")) return std::nullopt;
    std::string_view payload = link.substr(8);

    std::string remarks = "VLESS Node";
    size_t hash_pos = payload.find('#');
    if (hash_pos != std::string_view::npos) {
        remarks = UrlDecode(payload.substr(hash_pos + 1));
        payload = payload.substr(0, hash_pos);
    }

    size_t at_pos = payload.find('@');
    if (at_pos == std::string_view::npos) return std::nullopt;
    std::string uuid(payload.substr(0, at_pos));
    std::string_view rest = payload.substr(at_pos + 1);

    size_t q_pos = rest.find('?');
    std::string_view host_port = (q_pos != std::string_view::npos) ? rest.substr(0, q_pos) : rest;
    std::string_view query = (q_pos != std::string_view::npos) ? rest.substr(q_pos + 1) : "";

    auto hp = ExtractHostPort(host_port, 443);
    if (!hp) return std::nullopt;

    ParsedProxyNode node;
    node.type = "vless";
    node.name = remarks;
    node.server = hp->first;
    node.port = hp->second;
    node.uuid = uuid;
    node.cipher = "auto";
    node.udp = true;

    if (!query.empty()) {
        auto params = ParseQueryParams(query);
        if (params.contains("flow")) node.flow = params["flow"];
        if (params.contains("security")) {
            std::string sec = params["security"];
            if (sec == "tls") node.tls = true;
            else if (sec == "reality") {
                node.tls = true;
                node.reality_public_key = params["pbk"];
                node.reality_short_id = params["sid"];
                node.client_fingerprint = params.contains("fp") ? params["fp"] : "chrome";
            }
        }
        if (params.contains("sni")) node.sni = params["sni"];
        if (params.contains("type")) node.network = params["type"];
        if (params.contains("path")) node.path = params["path"];
        if (params.contains("host")) node.host_header = params["host"];
        if (params.contains("fp")) node.client_fingerprint = params["fp"];
    }

    return node;
}

std::optional<ParsedProxyNode> UniversalParser::ParseShadowsocks(std::string_view link) {
    if (!link.starts_with("ss://")) return std::nullopt;
    std::string_view payload = link.substr(5);

    std::string remarks = "SS Node";
    size_t hash_pos = payload.find('#');
    if (hash_pos != std::string_view::npos) {
        remarks = UrlDecode(payload.substr(hash_pos + 1));
        payload = payload.substr(0, hash_pos);
    }

    std::string method_pw;
    std::string host_port;

    if (payload.find('@') != std::string_view::npos) {
        size_t at_pos = payload.find('@');
        method_pw = std::string(payload.substr(0, at_pos));
        host_port = std::string(payload.substr(at_pos + 1));
    } else {
        auto decoded_opt = DecodeBase64Robust(payload);
        if (!decoded_opt) return std::nullopt;
        std::string_view dec = *decoded_opt;
        size_t at_pos = dec.find('@');
        if (at_pos == std::string_view::npos) return std::nullopt;
        method_pw = std::string(dec.substr(0, at_pos));
        host_port = std::string(dec.substr(at_pos + 1));
    }

    // method_pw 可能是 base64 编码的 cipher:password
    if (method_pw.find(':') == std::string::npos) {
        auto dec_mp = DecodeBase64Robust(method_pw);
        if (dec_mp) method_pw = *dec_mp;
    }

    size_t colon_mp = method_pw.find(':');
    if (colon_mp == std::string::npos) return std::nullopt;
    std::string cipher = method_pw.substr(0, colon_mp);
    std::string password = method_pw.substr(colon_mp + 1);

    // 剔除 host_port 后面的 ?plugin 参数
    size_t q_pos = host_port.find('?');
    if (q_pos != std::string::npos) {
        host_port = host_port.substr(0, q_pos);
    }

    auto hp = ExtractHostPort(host_port, 8388);
    if (!hp) return std::nullopt;

    ParsedProxyNode node;
    node.type = "ss";
    node.name = remarks;
    node.server = hp->first;
    node.port = hp->second;
    node.cipher = cipher;
    node.password = password;
    node.udp = true;

    return node;
}

std::optional<ParsedProxyNode> UniversalParser::ParseShadowsocksR(std::string_view link) {
    if (!link.starts_with("ssr://")) return std::nullopt;
    std::string_view payload = link.substr(6);
    auto decoded_opt = DecodeBase64Robust(payload);
    if (!decoded_opt) return std::nullopt;
    std::string_view raw = *decoded_opt;

    // host:port:protocol:method:obfs:base64pass/?obfsparam=...&remarks=...
    size_t q_pos = raw.find("/?");
    std::string_view main_part = (q_pos != std::string_view::npos) ? raw.substr(0, q_pos) : raw;
    std::string_view query_part = (q_pos != std::string_view::npos) ? raw.substr(q_pos + 2) : "";

    std::vector<std::string> parts;
    size_t start = 0;
    while (start < main_part.size()) {
        size_t c = main_part.find(':', start);
        if (c == std::string_view::npos) c = main_part.size();
        parts.emplace_back(main_part.substr(start, c - start));
        start = c + 1;
    }
    if (parts.size() < 6) return std::nullopt;

    std::string host = parts[0];
    uint16_t port = static_cast<uint16_t>(std::atoi(parts[1].c_str()));
    std::string protocol = parts[2];
    std::string method = parts[3];
    std::string obfs = parts[4];
    std::string base64pass = parts[5];
    std::string password = base64pass;
    auto pass_dec = DecodeBase64Robust(base64pass);
    if (pass_dec) password = *pass_dec;

    std::string remarks = "SSR Node";
    if (!query_part.empty()) {
        auto params = ParseQueryParams(query_part);
        if (params.contains("remarks")) {
            auto rem_dec = DecodeBase64Robust(params["remarks"]);
            if (rem_dec) remarks = *rem_dec;
            else remarks = params["remarks"];
        }
    }

    ParsedProxyNode node;
    node.type = "ssr";
    node.name = remarks;
    node.server = host;
    node.port = port;
    node.cipher = method;
    node.password = password;
    node.protocol = protocol;
    node.obfs = obfs;
    node.udp = true;

    return node;
}

std::optional<ParsedProxyNode> UniversalParser::ParseTrojan(std::string_view link) {
    if (!link.starts_with("trojan://")) return std::nullopt;
    std::string_view payload = link.substr(9);

    std::string remarks = "Trojan Node";
    size_t hash_pos = payload.find('#');
    if (hash_pos != std::string_view::npos) {
        remarks = UrlDecode(payload.substr(hash_pos + 1));
        payload = payload.substr(0, hash_pos);
    }

    size_t q_pos = payload.find('?');
    std::string_view auth_host = (q_pos != std::string_view::npos) ? payload.substr(0, q_pos) : payload;
    std::string_view query = (q_pos != std::string_view::npos) ? payload.substr(q_pos + 1) : "";

    size_t at_pos = auth_host.rfind('@');
    if (at_pos == std::string_view::npos) return std::nullopt;

    std::string password = UrlDecode(auth_host.substr(0, at_pos));
    std::string_view host_port = auth_host.substr(at_pos + 1);

    auto hp = ExtractHostPort(host_port, 443);
    if (!hp) return std::nullopt;

    ParsedProxyNode node;
    node.type = "trojan";
    node.name = remarks;
    node.server = hp->first;
    node.port = hp->second;
    node.password = password;
    node.udp = true;

    if (!query.empty()) {
        auto params = ParseQueryParams(query);
        if (params.contains("sni")) node.sni = params["sni"];
        if (params.contains("peer")) node.sni = params["peer"];
        if (params.contains("allowinsecure") || params.contains("insecure")) {
            std::string v = params.contains("allowinsecure") ? params["allowinsecure"] : params["insecure"];
            node.skip_cert_verify = (v == "1" || v == "true");
        }
        if (params.contains("alpn")) node.alpn = params["alpn"];
    }

    return node;
}

std::optional<ParsedProxyNode> UniversalParser::ParseHysteria2(std::string_view link) {
    std::string_view payload;
    if (link.starts_with("hysteria2://")) payload = link.substr(12);
    else if (link.starts_with("hy2://")) payload = link.substr(6);
    else return std::nullopt;

    std::string remarks = "Hysteria2 Node";
    size_t hash_pos = payload.find('#');
    if (hash_pos != std::string_view::npos) {
        remarks = UrlDecode(payload.substr(hash_pos + 1));
        payload = payload.substr(0, hash_pos);
    }

    size_t q_pos = payload.find('?');
    std::string_view auth_host = (q_pos != std::string_view::npos) ? payload.substr(0, q_pos) : payload;
    std::string_view query = (q_pos != std::string_view::npos) ? payload.substr(q_pos + 1) : "";

    size_t at_pos = auth_host.rfind('@');
    if (at_pos == std::string_view::npos) return std::nullopt;

    std::string password = UrlDecode(auth_host.substr(0, at_pos));
    std::string_view host_port = auth_host.substr(at_pos + 1);

    auto hp = ExtractHostPort(host_port, 443);
    if (!hp) return std::nullopt;

    ParsedProxyNode node;
    node.type = "hysteria2";
    node.name = remarks;
    node.server = hp->first;
    node.port = hp->second;
    node.password = password;
    node.udp = true;

    if (!query.empty()) {
        auto params = ParseQueryParams(query);
        if (params.contains("insecure")) {
            node.skip_cert_verify = (params["insecure"] == "1" || params["insecure"] == "true");
        }
        if (params.contains("sni")) node.sni = params["sni"];
        if (params.contains("obfs")) node.obfs = params["obfs"];
        if (params.contains("obfs-password")) node.obfs_password = params["obfs-password"];
    }

    return node;
}

std::optional<ParsedProxyNode> UniversalParser::ParseHysteria(std::string_view link) {
    if (!link.starts_with("hysteria://")) return std::nullopt;
    std::string_view payload = link.substr(11);

    std::string remarks = "Hysteria Node";
    size_t hash_pos = payload.find('#');
    if (hash_pos != std::string_view::npos) {
        remarks = UrlDecode(payload.substr(hash_pos + 1));
        payload = payload.substr(0, hash_pos);
    }

    size_t q_pos = payload.find('?');
    std::string_view auth_host = (q_pos != std::string_view::npos) ? payload.substr(0, q_pos) : payload;
    std::string_view query = (q_pos != std::string_view::npos) ? payload.substr(q_pos + 1) : "";

    std::string userinfo;
    std::string_view host_port;
    size_t at_pos = auth_host.rfind('@');
    if (at_pos != std::string_view::npos) {
        userinfo = UrlDecode(auth_host.substr(0, at_pos));
        host_port = auth_host.substr(at_pos + 1);
    } else {
        host_port = auth_host;
    }

    auto hp = ExtractHostPort(host_port, 443);
    if (!hp) return std::nullopt;

    ParsedProxyNode node;
    node.type = "hysteria";
    node.name = remarks;
    node.server = hp->first;
    node.port = hp->second;
    node.password = userinfo;
    node.udp = true;

    if (!query.empty()) {
        auto params = ParseQueryParams(query);
        if (params.contains("auth") || params.contains("auth_str") || params.contains("auth-str")) {
            std::string a = params.contains("auth") ? params["auth"] : (params.contains("auth_str") ? params["auth_str"] : params["auth-str"]);
            node.password = a;
        }
        if (params.contains("peer")) node.sni = params["peer"];
        if (params.contains("sni")) node.sni = params["sni"];
        if (params.contains("up") || params.contains("upmbps")) {
            std::string u = params.contains("up") ? params["up"] : params["upmbps"];
            node.up = NormalizeBandwidth(u);
        }
        if (params.contains("down") || params.contains("downmbps")) {
            std::string d = params.contains("down") ? params["down"] : params["downmbps"];
            node.down = NormalizeBandwidth(d);
        }
        if (params.contains("protocol")) node.protocol = params["protocol"];
        if (params.contains("insecure")) {
            node.skip_cert_verify = (params["insecure"] == "1" || params["insecure"] == "true");
        }
        if (params.contains("alpn")) node.alpn = params["alpn"];
        if (params.contains("obfs") || params.contains("obfsparam")) {
            node.obfs = params.contains("obfs") ? params["obfs"] : params["obfsparam"];
        }
    }

    return node;
}

std::optional<ParsedProxyNode> UniversalParser::ParseAnyTls(std::string_view link) {
    if (!link.starts_with("anytls://")) return std::nullopt;
    std::string_view payload = link.substr(9);

    std::string remarks = "AnyTLS Node";
    size_t hash_pos = payload.find('#');
    if (hash_pos != std::string_view::npos) {
        remarks = UrlDecode(payload.substr(hash_pos + 1));
        payload = payload.substr(0, hash_pos);
    }

    size_t q_pos = payload.find('?');
    std::string_view auth_host = (q_pos != std::string_view::npos) ? payload.substr(0, q_pos) : payload;
    std::string_view query = (q_pos != std::string_view::npos) ? payload.substr(q_pos + 1) : "";

    size_t at_pos = auth_host.rfind('@');
    if (at_pos == std::string_view::npos) return std::nullopt;

    std::string password = UrlDecode(auth_host.substr(0, at_pos));
    std::string_view host_port = auth_host.substr(at_pos + 1);

    auto hp = ExtractHostPort(host_port, 443);
    if (!hp) return std::nullopt;

    ParsedProxyNode node;
    node.type = "anytls";
    node.name = remarks;
    node.server = hp->first;
    node.port = hp->second;
    node.password = password;
    node.udp = true;

    if (!query.empty()) {
        auto params = ParseQueryParams(query);
        if (params.contains("sni")) node.sni = params["sni"];
        if (params.contains("insecure") || params.contains("allowinsecure")) {
            std::string v = params.contains("insecure") ? params["insecure"] : params["allowinsecure"];
            node.skip_cert_verify = (v == "1" || v == "true");
        }
        if (params.contains("alpn")) node.alpn = params["alpn"];
    }

    return node;
}

std::optional<ParsedProxyNode> UniversalParser::ParseTuic(std::string_view link) {
    if (!link.starts_with("tuic://")) return std::nullopt;
    std::string_view payload = link.substr(7);

    std::string remarks = "TUIC Node";
    size_t hash_pos = payload.find('#');
    if (hash_pos != std::string_view::npos) {
        remarks = UrlDecode(payload.substr(hash_pos + 1));
        payload = payload.substr(0, hash_pos);
    }

    size_t q_pos = payload.find('?');
    std::string_view auth_host = (q_pos != std::string_view::npos) ? payload.substr(0, q_pos) : payload;
    std::string_view query = (q_pos != std::string_view::npos) ? payload.substr(q_pos + 1) : "";

    size_t at_pos = auth_host.rfind('@');
    if (at_pos == std::string_view::npos) return std::nullopt;

    std::string userinfo = UrlDecode(auth_host.substr(0, at_pos));
    std::string_view host_port = auth_host.substr(at_pos + 1);

    auto hp = ExtractHostPort(host_port, 443);
    if (!hp) return std::nullopt;

    ParsedProxyNode node;
    node.type = "tuic";
    node.name = remarks;
    node.server = hp->first;
    node.port = hp->second;
    node.udp = true;

    size_t col = userinfo.find(':');
    if (col != std::string::npos) {
        node.uuid = userinfo.substr(0, col);
        node.password = userinfo.substr(col + 1);
    } else {
        node.password = userinfo; // token
    }

    if (!query.empty()) {
        auto params = ParseQueryParams(query);
        if (params.contains("sni")) node.sni = params["sni"];
        if (params.contains("alpn")) node.alpn = params["alpn"];
        if (params.contains("insecure") || params.contains("allow_insecure")) {
            std::string v = params.contains("insecure") ? params["insecure"] : params["allow_insecure"];
            node.skip_cert_verify = (v == "1" || v == "true");
        }
    }

    return node;
}

std::optional<ParsedProxyNode> UniversalParser::ParseWireGuard(std::string_view link) {
    if (!link.starts_with("wireguard://")) return std::nullopt;
    std::string_view payload = link.substr(12);

    std::string remarks = "WireGuard Node";
    size_t hash_pos = payload.find('#');
    if (hash_pos != std::string_view::npos) {
        remarks = UrlDecode(payload.substr(hash_pos + 1));
        payload = payload.substr(0, hash_pos);
    }

    size_t at_pos = payload.find('@');
    if (at_pos == std::string_view::npos) return std::nullopt;

    std::string private_key_raw(payload.substr(0, at_pos));
    std::string_view rest = payload.substr(at_pos + 1);

    size_t q_pos = rest.find('?');
    std::string_view host_port = (q_pos != std::string_view::npos) ? rest.substr(0, q_pos) : rest;
    std::string_view query = (q_pos != std::string_view::npos) ? rest.substr(q_pos + 1) : "";

    auto hp = ExtractHostPort(host_port, 51820);
    if (!hp) return std::nullopt;

    ParsedProxyNode node;
    node.type = "wireguard";
    node.name = remarks;
    node.server = hp->first;
    node.port = hp->second;
    node.private_key = private_key_raw;
    node.udp = true;

    if (!query.empty()) {
        auto params = ParseQueryParams(query);
        if (params.contains("public-key")) node.public_key = params["public-key"];
        else if (params.contains("publickey")) node.public_key = params["publickey"];
        else if (params.contains("pk")) node.public_key = params["pk"];

        if (params.contains("address") || params.contains("ip")) {
            std::string addrs = params.contains("address") ? params["address"] : params["ip"];
            // 分割 ipv4 与 ipv6
            size_t c = addrs.find(',');
            if (c != std::string::npos) {
                node.ip = addrs.substr(0, c);
                node.ipv6 = addrs.substr(c + 1);
            } else {
                if (addrs.find(':') != std::string::npos) node.ipv6 = addrs;
                else node.ip = addrs;
            }
        }

        if (params.contains("reserved")) {
            node.reserved = params["reserved"];
        }

        if (params.contains("mtu")) {
            try {
                node.mtu = static_cast<uint16_t>(std::stoi(params["mtu"]));
            } catch (...) {}
        }

        if (params.contains("preshared-key")) node.preshared_key = params["preshared-key"];
        else if (params.contains("psk")) node.preshared_key = params["psk"];
    }

    return node;
}

std::optional<ParsedProxyNode> UniversalParser::ParseSocks5(std::string_view link) {
    std::string_view payload;
    if (link.starts_with("socks5://")) payload = link.substr(9);
    else if (link.starts_with("socks://")) payload = link.substr(8);
    else return std::nullopt;

    std::string remarks = "SOCKS5 Node";
    size_t hash_pos = payload.find('#');
    if (hash_pos != std::string_view::npos) {
        remarks = UrlDecode(payload.substr(hash_pos + 1));
        payload = payload.substr(0, hash_pos);
    }

    std::string userinfo;
    std::string_view host_port;
    size_t at_pos = payload.rfind('@');
    if (at_pos != std::string_view::npos) {
        userinfo = UrlDecode(payload.substr(0, at_pos));
        host_port = payload.substr(at_pos + 1);
    } else {
        host_port = payload;
    }

    auto hp = ExtractHostPort(host_port, 1080);
    if (!hp) return std::nullopt;

    ParsedProxyNode node;
    node.type = "socks5";
    node.name = remarks;
    node.server = hp->first;
    node.port = hp->second;
    node.udp = true;

    if (!userinfo.empty()) {
        size_t c = userinfo.find(':');
        if (c != std::string::npos) {
            node.username = userinfo.substr(0, c);
            node.password = userinfo.substr(c + 1);
        } else {
            node.username = userinfo;
        }
    }

    return node;
}

std::optional<ParsedProxyNode> UniversalParser::ParseHttpFallback(std::string_view line) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '#' || line.front() == '\t')) line.remove_prefix(1);
    if (!line.starts_with("[HTTP]")) return std::nullopt;
    std::string_view payload = line.substr(6);
    while (!payload.empty() && (payload.front() == ' ' || payload.front() == '\t')) payload.remove_prefix(1);

    size_t lp = payload.find(" (");
    size_t rp = payload.find(')', lp);
    if (lp == std::string_view::npos || rp == std::string_view::npos) return std::nullopt;

    std::string name(payload.substr(0, lp));
    std::string_view host_port = payload.substr(lp + 2, rp - lp - 2);

    auto hp = ExtractHostPort(host_port, 80);
    if (!hp) return std::nullopt;

    ParsedProxyNode node;
    node.type = "http";
    node.name = name;
    node.server = hp->first;
    node.port = hp->second;
    return node;
}

std::optional<ParsedProxyNode> UniversalParser::ParseNodeLink(std::string_view line) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r')) line.remove_prefix(1);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.remove_suffix(1);
    if (line.empty()) return std::nullopt;

    if (auto http = ParseHttpFallback(line)) return http;
    if (line.starts_with('#') || line.starts_with("//")) return std::nullopt;

    if (line.starts_with("vmess://")) return ParseVmess(line);
    if (line.starts_with("vless://")) return ParseVless(line);
    if (line.starts_with("ss://")) return ParseShadowsocks(line);
    if (line.starts_with("ssr://")) return ParseShadowsocksR(line);
    if (line.starts_with("trojan://")) return ParseTrojan(line);
    if (line.starts_with("hysteria2://") || line.starts_with("hy2://")) return ParseHysteria2(line);
    if (line.starts_with("hysteria://")) return ParseHysteria(line);
    if (line.starts_with("anytls://")) return ParseAnyTls(line);
    if (line.starts_with("tuic://")) return ParseTuic(line);
    if (line.starts_with("wireguard://")) return ParseWireGuard(line);
    if (line.starts_with("socks5://") || line.starts_with("socks://")) return ParseSocks5(line);

    return std::nullopt;
}

std::vector<ParsedProxyNode> UniversalParser::ParseUriListToNodes(std::string_view content) {
    std::vector<ParsedProxyNode> nodes;

    // 检查是否整体被 Base64 编码
    std::string text_to_process;
    auto decoded_opt = DecodeBase64Robust(content);
    if (decoded_opt && decoded_opt->find("://") != std::string::npos) {
        text_to_process = *decoded_opt;
    } else {
        text_to_process = std::string(content);
    }

    std::istringstream iss(text_to_process);
    std::string line;
    while (std::getline(iss, line)) {
        if (auto n = ParseNodeLink(line)) {
            nodes.push_back(*n);
        }
    }
    return nodes;
}

std::optional<std::string> UniversalParser::ParseUriListToYaml(std::string_view content) {
    // 若输入内容本身就是标准 Clash YAML 配置 (含 proxies: 和 rules:)，直接返回原内容
    if (content.find("proxies:") != std::string_view::npos && content.find("rules:") != std::string_view::npos) {
        return std::string(content);
    }

    std::vector<ParsedProxyNode> nodes = ParseUriListToNodes(content);
    if (nodes.empty()) return std::nullopt;

    std::ostringstream ss;
    ss << "mixed-port: 10801\n";
    ss << "allow-lan: false\n";
    ss << "mode: rule\n";
    ss << "log-level: info\n";
    ss << "ipv6: false\n";
    ss << "external-controller: 127.0.0.1:9097\n";
    ss << "secret: \"\"\n\n";

    ss << "proxies:\n";
    for (const auto& node : nodes) {
        ss << node.ToYaml(2);
    }
    ss << "\n";

    ss << "proxy-groups:\n";
    ss << "  - name: PROXY\n";
    ss << "    type: select\n";
    ss << "    proxies:\n";
    for (const auto& node : nodes) {
        ss << "      - \"" << node.name << "\"\n";
    }
    ss << "\n";

    ss << "rules:\n";
    ss << "  - DOMAIN-SUFFIX,local,DIRECT\n";
    ss << "  - IP-CIDR,127.0.0.0/8,DIRECT\n";
    ss << "  - IP-CIDR,172.16.0.0/12,DIRECT\n";
    ss << "  - IP-CIDR,192.168.0.0/16,DIRECT\n";
    ss << "  - IP-CIDR,10.0.0.0/8,DIRECT\n";
    ss << "  - MATCH,PROXY\n";

    return ss.str();
}

} // namespace clashwin::config
