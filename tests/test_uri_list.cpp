// T06：URI 解析器协议对照测试
// 样例来源：universal_parser.rs #[cfg(test)] mod tests 全量抄录（Rust 单测）；
// Rust 单测未覆盖的协议（vmess/ss/socks 别名/整体 Base64/三级回退）为构造样例。
#include <windows.h>

#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "clashwin/config/constants.h"
#include "config/prfitem.h"
#include "config/profiles.h"
#include "enhance/uri_list.h"
#include "test_util.h"
#include "util/base64.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "config/yaml_serde.h"

using namespace clashwin;
using namespace clashwin::config;
using namespace clashwin::enhance;
using clashwin::utf::utf8_to_utf16;
using clashwin::utf::utf16_to_utf8;

// ---------- YAML 断言助手 ----------

namespace {

std::string S(const YAML::Node& m, const char* k) {
  if (!m || !m.IsMap()) return "<no-map>";
  const YAML::Node v = m[k];
  return (v && v.IsScalar()) ? v.Scalar() : "<missing>";
}

long long I(const YAML::Node& m, const char* k) {
  if (!m || !m.IsMap()) return -99999;
  const YAML::Node v = m[k];
  if (!v || !v.IsScalar()) return -99999;
  try {
    return std::stoll(v.Scalar());
  } catch (...) {
    return -99999;
  }
}

bool B(const YAML::Node& m, const char* k) {
  const YAML::Node v = m[k];
  return v && v.IsScalar() && v.Scalar() == "true";
}

bool HAS(const YAML::Node& m, const char* k) {
  if (!m || !m.IsMap()) return false;
  const YAML::Node v = m[k];
  return v && v.IsDefined() && !v.IsNull();
}

std::vector<std::string> SEQ(const YAML::Node& m, const char* k) {
  std::vector<std::string> out;
  const YAML::Node v = m[k];
  if (v && v.IsSequence()) {
    for (const auto& it : v) out.push_back(it.Scalar());
  }
  return out;
}

YAML::Node SUB(const YAML::Node& m, const char* k) {
  static const YAML::Node kEmpty;
  if (!m || !m.IsMap()) return kEmpty;
  const YAML::Node v = m[k];
  return (v && v.IsMap()) ? v : kEmpty;
}

std::string b64(std::string_view s) {
  return util::encode_base64(s);
}

std::string read_text_file(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return "<open-failed>";
  return std::string((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
}

std::wstring write_text_file(const std::wstring& path,
                             const std::string& content) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(content.data(), (std::streamsize)content.size());
  return path;
}

// 测试家目录（绝不触真实 %APPDATA%，← T03 测试同款）
std::wstring fresh_home(const char* tag) {
  wchar_t buf[MAX_PATH];
  GetTempPathW(MAX_PATH, buf);
  std::wstring root =
      std::wstring(buf) + L"clashwin_t06\\" + utf::utf8_to_utf16(tag);
  CreateDirectoryW((std::wstring(buf) + L"clashwin_t06").c_str(), nullptr);
  CreateDirectoryW(root.c_str(), nullptr);
  CreateDirectoryW((root + L"\\profiles").c_str(), nullptr);
  DeleteFileW((root + L"\\profiles.yaml").c_str());
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", root.c_str());
  _wputenv_s(L"CLASHWIN_PORTABLE_OVERRIDE", L"0");
  return root;
}

std::wstring direct_imports_path() {
  return clashwin::config::prfitem::profiles_dir() + L"\\L_Direct_Imports.yaml";
}

size_t proxies_count_of_file(const std::wstring& path) {
  std::string content = read_text_file(path);
  if (content == "<open-failed>") return (size_t)-1;
  YAML::Node y;
  try {
    y = YAML::Load(content);
  } catch (...) {
    return (size_t)-2;
  }
  if (!y["proxies"] || !y["proxies"].IsSequence()) return (size_t)-3;
  return y["proxies"].size();
}

}  // namespace

// ================= extract_host_port（Rust 单测） =================

UT_TEST(t06_extract_host_port_ipv4) {
  auto r = extract_host_port("example.com:8443", 443);
  UT_CHECK(r.has_value());
  UT_CHECK_EQ(r->first, "example.com");
  UT_CHECK_EQ(r->second, 8443);
}

UT_TEST(t06_extract_host_port_ipv6) {
  auto r = extract_host_port("[2401:c080:1000:29ac:5400:6ff:fe43:9d48]:443", 443);
  UT_CHECK(r.has_value());
  UT_CHECK_EQ(r->first, "2401:c080:1000:29ac:5400:6ff:fe43:9d48");
  UT_CHECK_EQ(r->second, 443);
}

UT_TEST(t06_extract_host_port_ipv6_custom_port) {
  auto r = extract_host_port("[::1]:8443", 443);
  UT_CHECK(r.has_value());
  UT_CHECK_EQ(r->first, "::1");
  UT_CHECK_EQ(r->second, 8443);
}

UT_TEST(t06_extract_host_port_defaults) {
  // 构造样例：缺端口/非法端口/越界端口 → 默认端口
  auto r1 = extract_host_port("example.com", 1080);
  UT_CHECK(r1.has_value() && r1->second == 1080);
  auto r2 = extract_host_port("example.com:abc", 1080);
  UT_CHECK(r2.has_value() && r2->second == 1080);
  auto r3 = extract_host_port("example.com:99999", 1080);
  UT_CHECK(r3.has_value() && r3->second == 1080);
}

// ================= Base64（Rust 单测 + 构造样例） =================

UT_TEST(t06_decode_base64_robust_newlines) {
  // Rust: test_decode_base64_robust_newlines
  auto d = util::decode_base64_robust("aGVsbG8gd29ybGQKaGVs\nbG8gd29ybGQ=");
  UT_CHECK(d.has_value());
  std::string s((const char*)d->data(), d->size());
  UT_CHECK_EQ(s, "hello world\nhello world");
}

UT_TEST(t06_decode_base64_robust_three_modes) {
  // 构造样例：standard / urlsafe / 缺 padding
  // "hello" standard = aGVsbG8= ; urlsafe 变体（- _）；无 padding = aGVsbG8
  auto a = util::decode_base64_robust("aGVsbG8=");
  UT_CHECK(a.has_value() && std::string((const char*)a->data(), a->size()) == "hello");
  auto b = util::decode_base64_robust("aGVsbG8");
  UT_CHECK(b.has_value() && std::string((const char*)b->data(), b->size()) == "hello");
  // urlsafe：字节 0xFB 0xEF 0xBE（-/_ 可互换）
  auto c = util::decode_base64_robust("-_-_");
  UT_CHECK(c.has_value());
  auto d = util::decode_base64_robust("+/+/");
  UT_CHECK(d.has_value());
  UT_CHECK(*c == *d);
  UT_CHECK_EQ(std::string((const char*)c->data(), c->size()),
              std::string("\xfb\xff\xbf", 3));
  UT_CHECK(!util::decode_base64_robust("").has_value());
  UT_CHECK(!util::decode_base64_robust("   ").has_value());
  UT_CHECK(!util::decode_base64_robust("!!!!").has_value());
}

// ================= vmess（构造样例：Rust 单测无 vmess 用例） =================

UT_TEST(t06_parse_vmess_basic_ws_tls) {
  // 构造样例：标准 v2rayN 分享链接（Base64 JSON 全字段）
  std::string json =
      R"({"v":"2","ps":"香港 01","add":"hk.example.com","port":"443",)"
      R"("id":"b831381d-6324-4d53-ad4f-8cda48b30811","aid":"0","net":"ws",)"
      R"("host":"cdn.example.com","path":"/ws","tls":"tls"})";
  auto m = parse_vmess("vmess://" + b64(json));
  UT_CHECK(m.has_value());
  const YAML::Node& n = *m;
  UT_CHECK_EQ(S(n, "type"), "vmess");
  UT_CHECK_EQ(S(n, "name"), "香港 01");
  UT_CHECK_EQ(S(n, "server"), "hk.example.com");
  UT_CHECK_EQ(I(n, "port"), 443);
  UT_CHECK_EQ(S(n, "uuid"), "b831381d-6324-4d53-ad4f-8cda48b30811");
  UT_CHECK_EQ(I(n, "alterId"), 0);
  UT_CHECK_EQ(S(n, "cipher"), "auto");
  UT_CHECK(B(n, "udp"));
  UT_CHECK(B(n, "tls"));
  UT_CHECK_EQ(S(n, "network"), "ws");
  YAML::Node ws = SUB(n, "ws-opts");
  UT_CHECK_EQ(S(ws, "path"), "/ws");
  YAML::Node headers = SUB(ws, "headers");
  UT_CHECK_EQ(S(headers, "Host"), "cdn.example.com");
}

UT_TEST(t06_parse_vmess_port_number_and_grpc) {
  // 构造样例：port 数字形态、aid 数字形态、net=grpc
  std::string json =
      R"({"ps":"grpc-node","add":"g.example.com","port":8443,)"
      R"("id":"u1","aid":7,"net":"grpc","path":"Svc"})";
  auto m = parse_vmess("vmess://" + b64(json));
  UT_CHECK(m.has_value());
  const YAML::Node& n = *m;
  UT_CHECK_EQ(I(n, "port"), 8443);
  UT_CHECK_EQ(I(n, "alterId"), 7);
  UT_CHECK_EQ(S(n, "network"), "grpc");
  YAML::Node grpc = SUB(n, "grpc-opts");
  UT_CHECK_EQ(S(grpc, "grpc-service-name"), "Svc");
  UT_CHECK(!HAS(n, "tls"));
}

UT_TEST(t06_parse_vmess_defaults_and_invalid) {
  // 构造样例：空 JSON → 默认值；非法 port → 443；非法输入 → nullopt
  auto m = parse_vmess("vmess://" + b64("{}"));
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "name"), "VMess Node");
  UT_CHECK_EQ(S(*m, "server"), "");
  UT_CHECK_EQ(I(*m, "port"), 443);
  UT_CHECK_EQ(S(*m, "uuid"), "");
  UT_CHECK_EQ(I(*m, "alterId"), 0);
  UT_CHECK(!HAS(*m, "network"));

  auto m2 = parse_vmess("vmess://" + b64(R"({"port":"abc","add":"a.com"})"));
  UT_CHECK(m2.has_value());
  UT_CHECK_EQ(I(*m2, "port"), 443);

  // 非法 Base64
  UT_CHECK(!parse_vmess("vmess://!!!!").has_value());
  // Base64 合法但 JSON 非对象
  UT_CHECK(!parse_vmess("vmess://" + b64("[1,2]")).has_value());
  // ps 类型错误（serde Option<String> 反序列化失败语义）
  UT_CHECK(!parse_vmess("vmess://" + b64(R"({"ps":123})")).has_value());
  // 前缀不匹配
  UT_CHECK(!parse_vmess("vmessX://abc").has_value());
}

UT_TEST(t06_parse_vmess_tcp_no_network_key) {
  // 构造样例：net=tcp → 不写 network 键（← Rust `network != "tcp"` 分支）
  std::string json = R"({"ps":"t","add":"a.com","port":80,"net":"tcp"})";
  auto m = parse_vmess("vmess://" + b64(json));
  UT_CHECK(m.has_value());
  UT_CHECK(!HAS(*m, "network"));
  UT_CHECK(!HAS(*m, "ws-opts"));
}

// ================= ss（构造样例：Rust 单测无 ss 用例） =================

UT_TEST(t06_parse_ss_plain_and_b64_credentials) {
  // 构造样例：明文 method:password
  auto m = parse_ss("ss://aes-256-gcm:password@example.com:8388#SS2");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "type"), "ss");
  UT_CHECK_EQ(S(*m, "name"), "SS2");
  UT_CHECK_EQ(S(*m, "server"), "example.com");
  UT_CHECK_EQ(I(*m, "port"), 8388);
  UT_CHECK_EQ(S(*m, "cipher"), "aes-256-gcm");
  UT_CHECK_EQ(S(*m, "password"), "password");
  UT_CHECK(B(*m, "udp"));

  // 构造样例：凭据整体 Base64
  auto m2 = parse_ss("ss://" + b64("aes-256-gcm:password") + "@example.com:8388#B64CRED");
  UT_CHECK(m2.has_value());
  UT_CHECK_EQ(S(*m2, "cipher"), "aes-256-gcm");
  UT_CHECK_EQ(S(*m2, "password"), "password");
  UT_CHECK_EQ(I(*m2, "port"), 8388);

  // 构造样例：#备注 percent 编码 + 默认名
  auto m3 = parse_ss("ss://aes-256-gcm:pw@example.com:8388#%E6%B5%8B%E8%AF%95");
  UT_CHECK(m3.has_value());
  UT_CHECK_EQ(S(*m3, "name"), "测试");
  auto m4 = parse_ss("ss://aes-256-gcm:pw@example.com:8388");
  UT_CHECK(m4.has_value());
  UT_CHECK_EQ(S(*m4, "name"), "SS Node");
}

UT_TEST(t06_parse_ss_2022_and_plugin) {
  // 构造样例：2022-blake3 系密码（含 '=' 等特殊字符，首个 ':' 切分）
  auto m = parse_ss(
      "ss://2022-blake3-aes-256-gcm:8Jk8fF9v3Q2R5mN7bC1dE4gH6jK0lM2nP3qR5sT7uV9w"
      "=@example.com:8388#ss2022");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "cipher"), "2022-blake3-aes-256-gcm");
  UT_CHECK_EQ(S(*m, "password"),
              "8Jk8fF9v3Q2R5mN7bC1dE4gH6jK0lM2nP3qR5sT7uV9w=");

  // 构造样例：plugin query 参数按原版忽略（不破坏解析）
  auto m2 = parse_ss(
      "ss://aes-256-gcm:pw@example.com:8388?plugin=obfs-local%3Bobfs%3Dhttp#p");
  UT_CHECK(m2.has_value());
  UT_CHECK_EQ(S(*m2, "server"), "example.com");
  UT_CHECK_EQ(I(*m2, "port"), 8388);
  UT_CHECK_EQ(S(*m2, "password"), "pw");
}

UT_TEST(t06_parse_ss_invalid) {
  // 构造样例：method:password 无冒号 → nullopt（Rust splitn 第二段缺失）
  auto m = parse_ss("ss://" + b64("aes256gcm") + "@example.com:8388#nc");
  UT_CHECK(!m.has_value());
  // 非 Base64 且无 @ → nullopt
  UT_CHECK(!parse_ss("ss://not-base64!!").has_value());
  // 整体 Base64 形态（无 @ 分隔则解码后需含 @）
  auto m2 = parse_ss("ss://" + b64("aes-256-gcm:pw@example.com:8388"));
  UT_CHECK(m2.has_value());
  UT_CHECK_EQ(S(*m2, "server"), "example.com");
  UT_CHECK_EQ(S(*m2, "name"), "SS Node");
}

// ================= trojan（Rust 单测） =================

UT_TEST(t06_parse_trojan_ipv6) {
  // Rust: test_parse_trojan_ipv6
  auto m = parse_trojan("trojan://password@[2001:db8::1]:443?sni=example.com#test");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "server"), "2001:db8::1");
  UT_CHECK_EQ(I(*m, "port"), 443);
}

UT_TEST(t06_parse_trojan_password_with_at_and_sni_decode) {
  // Rust: test_parse_trojan_password_with_at_and_sni_decode
  auto m = parse_trojan(
      "trojan://a@b@example.com:443?sni=cdn%20edge.example.com#t-at");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "password"), "a@b");
  UT_CHECK_EQ(S(*m, "server"), "example.com");
  UT_CHECK_EQ(S(*m, "sni"), "cdn edge.example.com");
}

UT_TEST(t06_parse_trojan_full_fields) {
  // 构造样例：allowinsecure 两个形态 + 默认名
  auto m = parse_trojan(
      "trojan://pw@example.com:443?allowinsecure=1&sni=s.com#tk");
  UT_CHECK(m.has_value());
  UT_CHECK(B(*m, "skip-cert-verify"));
  UT_CHECK_EQ(S(*m, "sni"), "s.com");
  auto m2 = parse_trojan("trojan://pw@example.com?allowInsecure=true");
  UT_CHECK(m2.has_value() && B(*m2, "skip-cert-verify"));
  auto m3 = parse_trojan("trojan://pw@example.com");
  UT_CHECK(m3.has_value() && S(*m3, "name") == "Trojan Node" &&
           I(*m3, "port") == 443 && !HAS(*m3, "skip-cert-verify"));
}

// ================= vless（Rust 单测） =================

UT_TEST(t06_parse_vless_reality) {
  // Rust: test_parse_vless_reality
  auto m = parse_vless(
      "vless://2513e61a-6982-39cd-9f0a-8f6fbc3fc5f0@awsjfde1.fatdns.net:443"
      "?encryption=none&flow=xtls-rprx-vision&security=reality&type=tcp"
      "&sni=s0.awsstatic.com&pbk=QzUWMFvn-J14NCZGsn5pz30inE7VT9auYKRwaRr5T20"
      "&sid=00fd4e9ac051c144&fp=chrome#aws%E5%BE%B7%E5%9B%BD");
  UT_CHECK(m.has_value());
  const YAML::Node& n = *m;
  UT_CHECK_EQ(S(n, "type"), "vless");
  UT_CHECK_EQ(S(n, "name"), "aws德国");
  UT_CHECK_EQ(S(n, "server"), "awsjfde1.fatdns.net");
  UT_CHECK_EQ(I(n, "port"), 443);
  UT_CHECK_EQ(S(n, "uuid"), "2513e61a-6982-39cd-9f0a-8f6fbc3fc5f0");
  UT_CHECK_EQ(S(n, "flow"), "xtls-rprx-vision");
  UT_CHECK(B(n, "tls"));
  UT_CHECK_EQ(S(n, "servername"), "s0.awsstatic.com");
  UT_CHECK_EQ(S(n, "client-fingerprint"), "chrome");
  YAML::Node reality = SUB(n, "reality-opts");
  UT_CHECK_EQ(S(reality, "public-key"),
              "QzUWMFvn-J14NCZGsn5pz30inE7VT9auYKRwaRr5T20");
  UT_CHECK_EQ(S(reality, "short-id"), "00fd4e9ac051c144");
}

UT_TEST(t06_parse_vless_ipv6) {
  // Rust: test_parse_vless_ipv6
  auto m = parse_vless(
      "vless://28beee5e-40ab-3c36-b797-1e4ecf27d0a0@[2401:c080:1000:29ac:"
      "5400:6ff:fe43:9d48]:443?encryption=none&type=tcp&fp=ios"
      "&host=s3611.wagahaha.xyz&flow=xtls-rprx-vision&security=tls"
      "&sni=u729792us3611.wagahaha.xyz&alpn=h2,http/1.1"
      "#%E6%9C%AA%E7%9F%A5%20VLESS-160");
  UT_CHECK(m.has_value());
  const YAML::Node& n = *m;
  UT_CHECK_EQ(S(n, "type"), "vless");
  UT_CHECK_EQ(S(n, "name"), "未知 VLESS-160");
  UT_CHECK_EQ(S(n, "server"), "2401:c080:1000:29ac:5400:6ff:fe43:9d48");
  UT_CHECK_EQ(I(n, "port"), 443);
  UT_CHECK_EQ(S(n, "uuid"), "28beee5e-40ab-3c36-b797-1e4ecf27d0a0");
  UT_CHECK_EQ(S(n, "flow"), "xtls-rprx-vision");
  UT_CHECK(B(n, "tls"));
  UT_CHECK_EQ(S(n, "servername"), "u729792us3611.wagahaha.xyz");
  UT_CHECK_EQ(S(n, "client-fingerprint"), "ios");
  std::vector<std::string> alpn = SEQ(n, "alpn");
  UT_CHECK_EQ(alpn.size(), (size_t)2);
  if (alpn.size() == 2) {
    UT_CHECK_EQ(alpn[0], "h2");
    UT_CHECK_EQ(alpn[1], "http/1.1");
  }
}

UT_TEST(t06_parse_vless_ws_grpc_raw) {
  // 构造样例：type=ws 带 path/host；type=grpc；type=raw → tcp
  auto ws = parse_vless(
      "vless://uuid@srv.com:443?type=ws&path=%2Fwspath&host=ws.host.com#vw");
  UT_CHECK(ws.has_value());
  UT_CHECK_EQ(S(*ws, "network"), "ws");
  YAML::Node wso = SUB(*ws, "ws-opts");
  UT_CHECK_EQ(S(wso, "path"), "/wspath");
  UT_CHECK_EQ(S(SUB(wso, "headers"), "Host"), "ws.host.com");

  auto grpc = parse_vless("vless://uuid@srv.com:443?type=grpc&path=svc#vg");
  UT_CHECK(grpc.has_value());
  UT_CHECK_EQ(S(SUB(*grpc, "grpc-opts"), "grpc-service-name"), "svc");

  auto raw = parse_vless("vless://uuid@srv.com:443?type=raw&path=x#vr");
  UT_CHECK(raw.has_value());
  UT_CHECK_EQ(S(*raw, "network"), "tcp");
  UT_CHECK(!HAS(*raw, "ws-opts"));
}

// ================= hysteria2（Rust 单测） =================

UT_TEST(t06_parse_hysteria2_ipv6) {
  // Rust: test_parse_hysteria2_ipv6
  auto m = parse_hysteria2(
      "hysteria2://password@[2001:db8::1]:8443?sni=example.com#test");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "server"), "2001:db8::1");
  UT_CHECK_EQ(I(*m, "port"), 8443);
}

UT_TEST(t06_parse_hysteria2_password_and_query_decode) {
  // Rust: test_parse_hysteria2_password_and_query_decode
  auto m = parse_hysteria2(
      "hysteria2://p@ss@example.com:8443?sni=example.com&obfs=salamander"
      "&obfs-password=my%20secret#h2-decode");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "password"), "p@ss");
  UT_CHECK_EQ(S(*m, "server"), "example.com");
  UT_CHECK_EQ(S(*m, "obfs-password"), "my secret");
}

UT_TEST(t06_parse_hysteria2_defaults) {
  // 构造样例：缺省字段
  auto m = parse_hysteria2("hysteria2://pw@example.com#h2min");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "name"), "h2min");
  UT_CHECK_EQ(I(*m, "port"), 443);
  UT_CHECK(B(*m, "udp"));
  UT_CHECK(!HAS(*m, "sni"));
  UT_CHECK(!HAS(*m, "skip-cert-verify"));
  auto m2 = parse_hysteria2("hysteria2://pw@example.com?insecure=1#i1");
  UT_CHECK(m2.has_value() && B(*m2, "skip-cert-verify"));
}

// ================= hysteria（Rust 单测） =================

UT_TEST(t06_parse_hysteria) {
  // Rust: test_parse_hysteria
  auto m = parse_hysteria(
      "hysteria://208.87.243.187:15823?upmbps=100&downmbps=100"
      "&auth=dongtaiwang.com&insecure=1&peer=apple.com&alpn=h3#hy-test");
  UT_CHECK(m.has_value());
  const YAML::Node& n = *m;
  UT_CHECK_EQ(S(n, "type"), "hysteria");
  UT_CHECK_EQ(S(n, "name"), "hy-test");
  UT_CHECK_EQ(S(n, "server"), "208.87.243.187");
  UT_CHECK_EQ(I(n, "port"), 15823);
  UT_CHECK_EQ(S(n, "auth-str"), "dongtaiwang.com");
  UT_CHECK_EQ(S(n, "sni"), "apple.com");
  UT_CHECK_EQ(S(n, "up"), "100 Mbps");
  UT_CHECK_EQ(S(n, "down"), "100 Mbps");
  UT_CHECK(B(n, "skip-cert-verify"));
  std::vector<std::string> alpn = SEQ(n, "alpn");
  UT_CHECK_EQ(alpn.size(), (size_t)1);
  if (alpn.size() == 1) UT_CHECK_EQ(alpn[0], "h3");
}

UT_TEST(t06_parse_hysteria_obfs_param) {
  // Rust: test_parse_hysteria_obfs_param（obfsParam 优先于 obfs）
  auto m = parse_hysteria(
      "hysteria://51.159.226.1:49003?auth=x&obfs=xplus&obfsParam=secret#hy-obfs");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "obfs"), "secret");
}

UT_TEST(t06_parse_hysteria_defaults) {
  // Rust: test_parse_hysteria_defaults
  auto m = parse_hysteria("hysteria://example.com?auth=pw#hy-min");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(I(*m, "port"), 443);
  UT_CHECK(!HAS(*m, "up"));
  UT_CHECK(!HAS(*m, "sni"));
  UT_CHECK(!HAS(*m, "skip-cert-verify"));
}

UT_TEST(t06_parse_hysteria_ipv6) {
  // Rust: test_parse_hysteria_ipv6
  auto m = parse_hysteria(
      "hysteria://[2001:db8::1]:8443?auth=pw&peer=example.com#hy-ipv6");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "server"), "2001:db8::1");
  UT_CHECK_EQ(I(*m, "port"), 8443);
}

UT_TEST(t06_parse_hysteria_invalid) {
  // Rust: test_parse_hysteria_invalid
  UT_CHECK(!parse_hysteria("hysteria://?auth=pw").has_value());
  auto m = parse_hysteria("hysteria://example.com?auth=pw&protocol=tcp#hy-proto");
  UT_CHECK(m.has_value());
  UT_CHECK(!HAS(*m, "protocol"));
}

UT_TEST(t06_parse_hysteria_percent_encoded_query) {
  // Rust: test_parse_hysteria_percent_encoded_query
  auto m = parse_hysteria(
      "hysteria://example.com:443?auth=pass%2Bword%26123&peer=sub.example.com"
      "&obfsParam=sec%20ret&protocol=wechat%2Dvideo#hy-decode");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "auth-str"), "pass+word&123");
  UT_CHECK_EQ(S(*m, "sni"), "sub.example.com");
  UT_CHECK_EQ(S(*m, "obfs"), "sec ret");
  UT_CHECK_EQ(S(*m, "protocol"), "wechat-video");
}

UT_TEST(t06_parse_hysteria_query_value_contains_at) {
  // Rust: test_parse_hysteria_query_value_contains_at
  auto m = parse_hysteria(
      "hysteria://auth_str@example.com:443?peer=user@backend.example.com"
      "&insecure=1#hy-query-at");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "auth-str"), "auth_str");
  UT_CHECK_EQ(S(*m, "server"), "example.com");
  UT_CHECK_EQ(S(*m, "sni"), "user@backend.example.com");
  UT_CHECK(B(*m, "skip-cert-verify"));
}

// ================= anytls（Rust 单测） =================

UT_TEST(t06_parse_anytls) {
  // Rust: test_parse_anytls
  auto m = parse_anytls(
      "anytls://mypassword@example.com:8443?sni=example.com&insecure=1"
      "&alpn=h2,http/1.1#anytls-test");
  UT_CHECK(m.has_value());
  const YAML::Node& n = *m;
  UT_CHECK_EQ(S(n, "type"), "anytls");
  UT_CHECK_EQ(S(n, "name"), "anytls-test");
  UT_CHECK_EQ(S(n, "server"), "example.com");
  UT_CHECK_EQ(I(n, "port"), 8443);
  UT_CHECK_EQ(S(n, "password"), "mypassword");
  UT_CHECK_EQ(S(n, "sni"), "example.com");
  UT_CHECK(B(n, "skip-cert-verify"));
  std::vector<std::string> alpn = SEQ(n, "alpn");
  UT_CHECK_EQ(alpn.size(), (size_t)2);
  if (alpn.size() == 2) {
    UT_CHECK_EQ(alpn[0], "h2");
    UT_CHECK_EQ(alpn[1], "http/1.1");
  }
}

UT_TEST(t06_parse_anytls_defaults) {
  // Rust: test_parse_anytls_defaults
  auto m = parse_anytls("anytls://pw@example.com#anytls-min");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(I(*m, "port"), 443);
  UT_CHECK(!HAS(*m, "sni"));
  UT_CHECK(!HAS(*m, "skip-cert-verify"));
}

UT_TEST(t06_parse_anytls_ipv6) {
  // Rust: test_parse_anytls_ipv6
  auto m = parse_anytls("anytls://pw@[2001:db8::1]:443?sni=example.com#anytls-ipv6");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "server"), "2001:db8::1");
  UT_CHECK_EQ(I(*m, "port"), 443);
}

UT_TEST(t06_parse_anytls_invalid) {
  // Rust: test_parse_anytls_invalid
  UT_CHECK(!parse_anytls("anytls://example.com:443").has_value());
  UT_CHECK(!parse_anytls("anytls://@example.com:443").has_value());
}

UT_TEST(t06_parse_anytls_password_with_at) {
  // Rust: test_parse_anytls_password_with_at
  auto m = parse_anytls(
      "anytls://user%40pass@example.com:8443?sni=example.com#anytls-enc");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "password"), "user@pass");

  auto m2 = parse_anytls(
      "anytls://user@pass@example.com:8443?sni=example.com#anytls-at");
  UT_CHECK(m2.has_value());
  UT_CHECK_EQ(S(*m2, "password"), "user@pass");
  UT_CHECK_EQ(S(*m2, "server"), "example.com");
  UT_CHECK_EQ(I(*m2, "port"), 8443);
}

UT_TEST(t06_parse_anytls_query_value_contains_at) {
  // Rust: test_parse_anytls_query_value_contains_at
  auto m = parse_anytls(
      "anytls://mypass@example.com:443?sni=user@internal.example.com"
      "&insecure=true#anytls-query-at");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "password"), "mypass");
  UT_CHECK_EQ(S(*m, "server"), "example.com");
  UT_CHECK_EQ(S(*m, "sni"), "user@internal.example.com");
  UT_CHECK(B(*m, "skip-cert-verify"));
}

// ================= tuic（Rust 单测） =================

UT_TEST(t06_parse_tuic_v5) {
  // Rust: test_parse_tuic_v5
  auto m = parse_tuic(
      "tuic://00000000-0000-0000-0000-000000000001:password@example.com:10443"
      "?congestion_control=cubic&alpn=h3&sni=example.com&allow_insecure=0"
      "&udp_relay_mode=native#test-tuic");
  UT_CHECK(m.has_value());
  const YAML::Node& n = *m;
  UT_CHECK_EQ(S(n, "type"), "tuic");
  UT_CHECK_EQ(S(n, "name"), "test-tuic");
  UT_CHECK_EQ(S(n, "server"), "example.com");
  UT_CHECK_EQ(I(n, "port"), 10443);
  UT_CHECK_EQ(S(n, "uuid"), "00000000-0000-0000-0000-000000000001");
  UT_CHECK_EQ(S(n, "password"), "password");
  UT_CHECK_EQ(S(n, "congestion-controller"), "cubic");
  UT_CHECK_EQ(S(n, "udp-relay-mode"), "native");
  std::vector<std::string> alpn = SEQ(n, "alpn");
  UT_CHECK_EQ(alpn.size(), (size_t)1);
  if (alpn.size() == 1) UT_CHECK_EQ(alpn[0], "h3");
}

UT_TEST(t06_parse_tuic_v4) {
  // Rust: test_parse_tuic_v4
  auto m = parse_tuic("tuic://TOKEN@example.com:443?alpn=h3&udp_relay_mode=quic#v4-node");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "type"), "tuic");
  UT_CHECK_EQ(S(*m, "token"), "TOKEN");
  UT_CHECK_EQ(S(*m, "udp-relay-mode"), "quic");
}

UT_TEST(t06_parse_tuic_ipv6) {
  // Rust: test_parse_tuic_ipv6
  auto m = parse_tuic("tuic://uuid:password@[2001:db8::1]:443?sni=example.com#tuic-ipv6");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "server"), "2001:db8::1");
  UT_CHECK_EQ(I(*m, "port"), 443);
}

UT_TEST(t06_parse_tuic_percent_encoded) {
  // Rust: test_parse_tuic_percent_encoded
  auto m = parse_tuic("tuic://uuid%3Dtest:pass%40word@example.com:443#encoded-tuic");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "uuid"), "uuid=test");
  UT_CHECK_EQ(S(*m, "password"), "pass@word");
}

UT_TEST(t06_parse_tuic_password_with_at) {
  // Rust: test_parse_tuic_password_with_at
  auto m = parse_tuic(
      "tuic://00000000-0000-0000-0000-000000000001:p@w@example.com:443?sni=x.com#tuic-at");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "password"), "p@w");
  UT_CHECK_EQ(S(*m, "server"), "example.com");
}

// ================= wireguard（Rust 单测） =================

UT_TEST(t06_parse_wireguard) {
  // Rust: test_parse_wireguard
  auto m = parse_wireguard(
      "wireguard://eCtXsJZ27+4PbhDkHnB923tkUn2Gj59wZw5wFA75MnU="
      "@162.159.192.1:2480?public-key=Cr8hWlKvtDt7nrvf+f0brNQQzabAqrjfBvas9pmowjo="
      "&address=172.16.0.2&mtu=1408#wg-test");
  UT_CHECK(m.has_value());
  const YAML::Node& n = *m;
  UT_CHECK_EQ(S(n, "type"), "wireguard");
  UT_CHECK_EQ(S(n, "name"), "wg-test");
  UT_CHECK_EQ(S(n, "server"), "162.159.192.1");
  UT_CHECK_EQ(I(n, "port"), 2480);
  UT_CHECK_EQ(S(n, "public-key"),
              "Cr8hWlKvtDt7nrvf+f0brNQQzabAqrjfBvas9pmowjo=");
  UT_CHECK_EQ(S(n, "ip"), "172.16.0.2");
  UT_CHECK_EQ(I(n, "mtu"), 1408);
  std::vector<std::string> allowed = SEQ(n, "allowed-ips");
  UT_CHECK_EQ(allowed.size(), (size_t)1);
  if (allowed.size() == 1) UT_CHECK_EQ(allowed[0], "0.0.0.0/0");
}

UT_TEST(t06_parse_wireguard_ipv6) {
  // Rust: test_parse_wireguard_ipv6
  auto m = parse_wireguard(
      "wireguard://eCtXsJZ27+4PbhDkHnB923tkUn2Gj59wZw5wFA75MnU="
      "@[2001:db8::1]:51820?public-key=Cr8hWlKvtDt7nrvf+f0brNQQzabAqrjfBvas9pmowjo="
      "&address=172.16.0.2#wg-ipv6");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "server"), "2001:db8::1");
  UT_CHECK_EQ(I(*m, "port"), 51820);
}

UT_TEST(t06_parse_wireguard_address_with_cidr) {
  // Rust: test_parse_wireguard_address_with_cidr
  auto m = parse_wireguard(
      "wireguard://key@server:51820?address=10.0.0.2/32,fd00::2/128#wg-cidr");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "ip"), "10.0.0.2");
  UT_CHECK_EQ(S(*m, "ipv6"), "fd00::2");
}

UT_TEST(t06_parse_wireguard_ipv6_first) {
  // Rust: test_parse_wireguard_ipv6_first
  auto m = parse_wireguard(
      "wireguard://key@server:51820?address=fd00::1/64,10.0.0.1/24#wg-ipv6-first");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "ip"), "10.0.0.1");
  UT_CHECK_EQ(S(*m, "ipv6"), "fd00::1");
}

UT_TEST(t06_parse_wireguard_multiple_ipv6) {
  // Rust: test_parse_wireguard_multiple_ipv6
  auto m = parse_wireguard(
      "wireguard://key@server:51820?address=10.0.0.1/32,fd00::1/64,fd01::2/64"
      "#wg-multi-ipv6");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "ip"), "10.0.0.1");
  std::vector<std::string> v6 = SEQ(*m, "ipv6");
  UT_CHECK_EQ(v6.size(), (size_t)2);
  if (v6.size() == 2) {
    UT_CHECK_EQ(v6[0], "fd00::1");
    UT_CHECK_EQ(v6[1], "fd01::2");
  }
}

// ================= socks5 / http 兜底（Rust 单测 + 构造样例） =================

UT_TEST(t06_parse_socks5_no_auth) {
  // Rust: test_parse_socks5_no_auth
  auto m = parse_socks5("socks5://example.com:1080#socks-test");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "type"), "socks5");
  UT_CHECK_EQ(S(*m, "name"), "socks-test");
  UT_CHECK_EQ(S(*m, "server"), "example.com");
  UT_CHECK_EQ(I(*m, "port"), 1080);
}

UT_TEST(t06_parse_socks5_with_auth) {
  // Rust: test_parse_socks5_with_auth
  auto m = parse_socks5("socks5://user:pass@example.com:1080#auth-socks");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "username"), "user");
  UT_CHECK_EQ(S(*m, "password"), "pass");
}

UT_TEST(t06_parse_socks5_ipv6) {
  // Rust: test_parse_socks5_ipv6
  auto m = parse_socks5("socks5://[2001:db8::1]:1080#ipv6-socks");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "server"), "2001:db8::1");
  UT_CHECK_EQ(I(*m, "port"), 1080);
}

UT_TEST(t06_parse_socks_alias) {
  // 构造样例：socks:// 别名（← Rust strip_prefix("socks5://").or(socks://)）
  auto m = parse_socks5("socks://user:pass@1.2.3.4:1080#alias");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "type"), "socks5");
  UT_CHECK_EQ(S(*m, "server"), "1.2.3.4");
  UT_CHECK_EQ(I(*m, "port"), 1080);
  UT_CHECK_EQ(S(*m, "username"), "user");
  UT_CHECK_EQ(S(*m, "password"), "pass");
}

UT_TEST(t06_parse_http_fallback) {
  // Rust: test_parse_http_fallback
  auto m = parse_http_fallback(
      "# [HTTP] v5-香港03|1x|v (v5.cdn.ljz8s7lhbv.fatdns.net:6712)");
  UT_CHECK(m.has_value());
  UT_CHECK_EQ(S(*m, "type"), "http");
  UT_CHECK_EQ(S(*m, "name"), "v5-香港03|1x|v");
  UT_CHECK_EQ(S(*m, "server"), "v5.cdn.ljz8s7lhbv.fatdns.net");
  UT_CHECK_EQ(I(*m, "port"), 6712);
  // 构造样例：无 '#'
  auto m2 = parse_http_fallback("[HTTP] plain (h.com:8080)");
  UT_CHECK(m2.has_value() && I(*m2, "port") == 8080);
  // 构造样例：端口非法 → 80
  auto m3 = parse_http_fallback("[HTTP] badport (h.com:abc)");
  UT_CHECK(m3.has_value() && I(*m3, "port") == 80);
}

// ================= parse_uri_list 集成 =================

UT_TEST(t06_parse_uri_list_mixed) {
  // 构造样例：混合列表 + 注释 + 空行 + 非法行 + [HTTP] 兜底行
  std::string list =
      "# 顶部注释\n"
      "\n"
      "ss://aes-256-gcm:pw@example.com:8388#SS1\n"
      "garbage-line-not-a-uri\n"
      "// 斜杠注释\n"
      "trojan://pw@t.com:443#T1\n"
      "# [HTTP] 兜底节点 (fb.example.com:8080)\n"
      "vmess://" + b64(R"({"ps":"V1","add":"v.com","port":1})") + "\n";
  auto r = parse_uri_list(list);
  UT_CHECK(r.has_value());
  const YAML::Node proxies = (*r)["proxies"];
  UT_CHECK(proxies.IsSequence());
  UT_CHECK_EQ(proxies.size(), (size_t)4);
  if (proxies.size() == 4) {
    UT_CHECK_EQ(S(proxies[0], "type"), "ss");
    UT_CHECK_EQ(S(proxies[0], "name"), "SS1");
    UT_CHECK_EQ(S(proxies[1], "type"), "trojan");
    UT_CHECK_EQ(S(proxies[2], "type"), "http");
    UT_CHECK_EQ(S(proxies[3], "type"), "vmess");
  }
}

UT_TEST(t06_parse_uri_list_all_invalid_and_b64_content) {
  // 构造样例：全非法 → nullopt
  UT_CHECK(!parse_uri_list("hello\nworld\n").has_value());
  UT_CHECK(!parse_uri_list("").has_value());

  // 构造样例：整体 Base64 包裹
  std::string content = "ss://aes-256-gcm:pw@example.com:8388#b64wrap\n";
  auto r = parse_uri_list(b64(content));
  UT_CHECK(r.has_value());
  const YAML::Node proxies = (*r)["proxies"];
  UT_CHECK_EQ(proxies.size(), (size_t)1);
  if (proxies.size() == 1) UT_CHECK_EQ(S(proxies[0], "name"), "b64wrap");

  // 构造样例：Base64 解码结果非 UTF-8 → 回退原文（→ 无节点 → nullopt）
  // 0xFF 0xFE 的 Base64
  UT_CHECK(!parse_uri_list("/v4=").has_value());
}

// ================= parse_subscription_content 三级回退 =================

UT_TEST(t06_parse_subscription_level1_yaml) {
  std::string yaml =
      "mixed-port: 10801\n"
      "proxies:\n"
      "  - name: a\n"
      "    type: ss\n";
  auto r = parse_subscription_content(yaml);
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(r.value().second, yaml);  // 序列化 == 原文
  UT_CHECK(static_cast<bool>(r.value().first["proxies"]));
}

UT_TEST(t06_parse_subscription_level1_proxy_providers) {
  std::string yaml = "proxy-providers:\n  sub1:\n    type: http\n";
  auto r = parse_subscription_content(yaml);
  UT_CHECK(r.is_ok());
}

UT_TEST(t06_parse_subscription_level2_base64_yaml) {
  std::string yaml = "proxies:\n  - name: b64node\n    type: ss\n";
  auto r = parse_subscription_content(b64(yaml));
  UT_CHECK(r.is_ok());
  if (r.is_ok()) {
    UT_CHECK_EQ(r.value().second, yaml);  // 序列化 == 解码文
    const YAML::Node proxies = r.value().first["proxies"];
    UT_CHECK_EQ(proxies.size(), (size_t)1);
  }
}

UT_TEST(t06_parse_subscription_level3_uri_list) {
  std::string uris = "ss://aes-256-gcm:pw@example.com:8388#L3\n";
  auto r = parse_subscription_content(uris);
  UT_CHECK(r.is_ok());
  if (r.is_ok()) {
    UT_CHECK(r.value().second.find("proxies:") != std::string::npos);
  }
  // Base64 包裹的 URI 列表（机场常见形态）
  auto r2 = parse_subscription_content(b64(uris));
  UT_CHECK(r2.is_ok());
}

UT_TEST(t06_parse_subscription_invalid) {
  // YAML 但无 proxies/proxy-providers 键 → 非①；Base64 失败 → 非②；URI 解析空 → 非③
  auto r = parse_subscription_content("mixed-port: 10801\n");
  UT_CHECK(!r.is_ok());
  // 纯垃圾
  auto r2 = parse_subscription_content("total garbage !!! ###");
  UT_CHECK(!r2.is_ok());
}

// ================= from_url：SSRF 集成（需 profiles 实例） =================

UT_TEST(t06_from_url_ssrf_rejects_loopback) {
  fresh_home("fromurl");
  profiles::set_profiles_instance(nullptr);
  IProfiles p = profiles::load();
  profiles::set_profiles_instance(&p);

  auto r1 = PrfItem::from_url("http://127.0.0.1/sub", std::nullopt,
                              std::nullopt, std::nullopt);
  UT_CHECK(!r1.is_ok());
  UT_CHECK(r1.error().message.find("cannot fetch") != std::string::npos);

  auto r2 = PrfItem::from_url("http://localhost/sub", std::nullopt,
                              std::nullopt, std::nullopt);
  UT_CHECK(!r2.is_ok());

  auto r3 = PrfItem::from_url("http://169.254.169.254/latest/meta-data",
                              std::nullopt, std::nullopt, std::nullopt);
  UT_CHECK(!r3.is_ok());
  profiles::set_profiles_instance(nullptr);
}

// ================= from_url_direct：L_Direct_Imports.yaml =================

UT_TEST(t06_from_url_direct_clear_creates_empty_file) {
  std::wstring home = fresh_home("direct_clear");
  profiles::set_profiles_instance(nullptr);
  IProfiles p = profiles::load();
  profiles::set_profiles_instance(&p);

  auto item = PrfItem::from_url_direct("clear", std::nullopt, std::nullopt);
  profiles::set_profiles_instance(nullptr);
  UT_CHECK(item.is_ok());
  if (!item.is_ok()) return;
  PrfItem it = item.value();
  UT_CHECK_EQ(*it.uid, "L_Direct_Imports");
  UT_CHECK_EQ(*it.desc, "0");
  UT_CHECK(it.file_data.has_value());

  std::wstring path = direct_imports_path();
  UT_CHECK(GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);
  std::string content = read_text_file(path);
  // ← Rust clear 分支：serde 序列化 {proxies: []}
  UT_CHECK(content.find("proxies:") != std::string::npos);
  UT_CHECK_EQ(proxies_count_of_file(path), (size_t)0);
  // clean 别名（大小写不敏感）
  profiles::set_profiles_instance(nullptr);
  IProfiles p2 = profiles::load();
  profiles::set_profiles_instance(&p2);
  auto item2 = PrfItem::from_url_direct("CLEAN", std::nullopt, std::nullopt);
  profiles::set_profiles_instance(nullptr);
  UT_CHECK(item2.is_ok());
}

UT_TEST(t06_from_url_direct_append_dedup_merge) {
  fresh_home("direct_merge");
  profiles::set_profiles_instance(nullptr);
  IProfiles p = profiles::load();
  profiles::set_profiles_instance(&p);
  std::wstring path = direct_imports_path();

  // 第一次导入：2 节点（ss + trojan）
  std::string first =
      "ss://aes-256-gcm:pw1@example.com:8388#%E9%A6%99%E6%B8%AF01\n"
      "trojan://pw@t.example.com:443#T1\n";
  auto r1 = PrfItem::from_url_direct(first, std::nullopt, std::nullopt);
  UT_CHECK(r1.is_ok());
  UT_CHECK_EQ(*r1.value().desc, "2");
  auto w1 = p.append_item(r1.value());
  UT_CHECK(w1.is_ok());
  UT_CHECK_EQ(proxies_count_of_file(path), (size_t)2);

  // 第二次导入：同名同 server 同 port（密码不同）→ 原位替换；新 trojan → 追加
  std::string second =
      "ss://aes-256-gcm:pw2@example.com:8388#%E9%A6%99%E6%B8%AF01\n"
      "trojan://pw2@t2.example.com:443#T2\n";
  auto r2 = PrfItem::from_url_direct(second, std::nullopt, std::nullopt);
  UT_CHECK(r2.is_ok());
  UT_CHECK_EQ(*r2.value().desc, "3");
  auto w2 = p.append_item(r2.value());
  UT_CHECK(w2.is_ok());
  UT_CHECK_EQ(proxies_count_of_file(path), (size_t)3);

  // 校验文件内容：替换后的 ss 密码 + 中文节点名保留
  std::string content = read_text_file(path);
  YAML::Node y;
  bool parsed = true;
  try {
    y = YAML::Load(content);
  } catch (...) {
    parsed = false;
  }
  UT_CHECK(parsed);
  if (parsed) {
    const YAML::Node proxies = y["proxies"];
    // 首位被替换（pw2），新增 T2 在尾部
    UT_CHECK_EQ(proxies[0]["name"].Scalar(), "香港01");
    UT_CHECK_EQ(proxies[0]["password"].Scalar(), "pw2");
    UT_CHECK_EQ(proxies[1]["name"].Scalar(), "T1");
    UT_CHECK_EQ(proxies[2]["name"].Scalar(), "T2");
  }

  // 清空
  auto r3 = PrfItem::from_url_direct("clear", std::nullopt, std::nullopt);
  UT_CHECK(r3.is_ok());
  UT_CHECK_EQ(proxies_count_of_file(path), (size_t)0);
  profiles::set_profiles_instance(nullptr);
}

UT_TEST(t06_from_url_direct_invalid_input) {
  fresh_home("direct_invalid");
  profiles::set_profiles_instance(nullptr);
  IProfiles p = profiles::load();
  profiles::set_profiles_instance(&p);
  auto r = PrfItem::from_url_direct("not-a-valid-node-uri!!!", std::nullopt,
                                    std::nullopt);
  profiles::set_profiles_instance(nullptr);
  UT_CHECK(!r.is_ok());
  UT_CHECK(r.error().message.find("无法解析") != std::string::npos);
}

UT_TEST(t06_from_url_direct_chinese_names) {
  // 构造样例：中文节点名 percent 编码往返
  fresh_home("direct_chinese");
  profiles::set_profiles_instance(nullptr);
  IProfiles p = profiles::load();
  profiles::set_profiles_instance(&p);
  auto item = PrfItem::from_url_direct(
      "ss://aes-256-gcm:pw@example.com:8388#%E6%B5%8B%E8%AF%95%E8%8A%82%E7%82%B9",
      std::nullopt, std::nullopt);
  profiles::set_profiles_instance(nullptr);
  UT_CHECK(item.is_ok());
  UT_CHECK_EQ(*item.value().desc, "1");
  // 节点名在序列化产物中
  UT_CHECK(item.value().file_data->find("测试节点") != std::string::npos);
}
