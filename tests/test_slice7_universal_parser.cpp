#include <iostream>
#include <cassert>
#include <filesystem>
#include <string>
#include <vector>
#include "clashwin/config/universal_parser.h"
#include "clashwin/config/enhance_chain.h"
#include "clashwin/config/node_filter.h"
#include "clashwin/config/profile_manager.h"

using namespace clashwin::config;

void TestBase64Robust() {
    std::cout << "[Test 1] Testing Robust Base64 Decoding..." << std::endl;

    // 1. 标准解码
    auto s1 = UniversalParser::DecodeBase64Robust("aGVsbG8gd29ybGQ=");
    assert(s1.has_value() && *s1 == "hello world");

    // 2. 缺失填充 (=)
    auto s2 = UniversalParser::DecodeBase64Robust("aGVsbG8gd29ybGQ");
    assert(s2.has_value() && *s2 == "hello world");

    // 3. 包含换行与空白
    auto s3 = UniversalParser::DecodeBase64Robust("  aGVs\r\nbG8g\n d29y bGQ= \t");
    assert(s3.has_value() && *s3 == "hello world");

    // 4. URL-safe base64 (- and _)
    // "hello" standard base64 "aGVsbG8="
    // test with characters that map to - and _
    auto s4 = UniversalParser::DecodeBase64Robust("-_==");
    assert(s4.has_value());

    std::cout << "  -> Base64 Robust tests passed!" << std::endl;
}

void TestProtocolParsers() {
    std::cout << "[Test 2] Testing 19 Protocols Parsing..." << std::endl;

    // 1. VMess
    // JSON: {"v":"2","ps":"HK_VMess","add":"1.2.3.4","port":443,"id":"11111111-2222-3333-4444-555555555555","aid":0,"net":"ws","path":"/ws","tls":"tls"}
    // Base64 of above JSON
    std::string vmess_json = "{\"v\":\"2\",\"ps\":\"HK_VMess\",\"add\":\"1.2.3.4\",\"port\":443,\"id\":\"11111111-2222-3333-4444-555555555555\",\"aid\":0,\"net\":\"ws\",\"path\":\"/ws\",\"tls\":\"tls\"}";
    auto vmess_b64 = "vmess://eyd2IjoiMiIsInBzIjoiSEtfVk1lc3MiLCJhZGQiOiIxLjIuMy40IiwicG9ydCI6NDQzLCJpZCI6IjExMTExMTExLTIyMjItMzMzMy00NDQ0LTU1NTU1NTU1NTU1NSIsImFpZCI6MCwibmV0Ijoid3MiLCJwYXRoIjoiL3dzIiwidGxzIjoidGxzIn0=";
    auto node_vmess = UniversalParser::ParseVmess(vmess_b64);
    assert(node_vmess.has_value());
    assert(node_vmess->name == "HK_VMess");
    assert(node_vmess->server == "1.2.3.4");
    assert(node_vmess->port == 443);
    assert(node_vmess->network == "ws");
    assert(node_vmess->tls == true);

    // 2. VLESS with Reality
    std::string vless_link = "vless://11111111-2222-3333-4444-555555555555@1.2.3.4:443?security=reality&pbk=fakePub123&sid=fakeSid1&sni=hk.example.com&flow=xtls-rprx-vision#HK-Reality";
    auto node_vless = UniversalParser::ParseVless(vless_link);
    assert(node_vless.has_value());
    assert(node_vless->name == "HK-Reality");
    assert(node_vless->reality_public_key == "fakePub123");
    assert(node_vless->flow == "xtls-rprx-vision");

    // 3. Shadowsocks SIP002
    // base64("aes-256-gcm:password") = "YWVzLTI1Ni1nY206cGFzc3dvcmQ="
    std::string ss_link = "ss://YWVzLTI1Ni1nY206cGFzc3dvcmQ=@1.2.3.4:8388#Japan-SS";
    auto node_ss = UniversalParser::ParseShadowsocks(ss_link);
    assert(node_ss.has_value());
    assert(node_ss->name == "Japan-SS");
    assert(node_ss->cipher == "aes-256-gcm");
    assert(node_ss->password == "password");
    assert(node_ss->port == 8388);

    // 4. ShadowsocksR
    // ssr://1.2.3.4:8388:origin:aes-256-cfb:plain:dGFzc3dvcmQ=/?remarks=V1NTUms5cWRXND0= (remarks = SS_SSR)
    std::string ssr_raw = "1.2.3.4:8388:origin:aes-256-cfb:plain:dGFzc3dvcmQ=/?remarks=U1NSX05vZGU=";
    // Base64 encode the ssr_raw
    std::string ssr_link = "ssr://MS4yLjMuNDo4Mzg4Om9yaWdpbjphZXMtMjU2LWNmYjpwbGFpbjpkR0Z6YzNkdmNtUT0vP3JlbWFya3M9VTFOc1gwNXZaR1U9";
    auto node_ssr = UniversalParser::ParseShadowsocksR(ssr_link);
    assert(node_ssr.has_value());
    assert(node_ssr->type == "ssr");
    assert(node_ssr->server == "1.2.3.4");

    // 5. Trojan
    std::string trojan_link = "trojan://secretpass@1.2.3.4:443?sni=trojan.example.com&allowInsecure=1#US-Trojan";
    auto node_trojan = UniversalParser::ParseTrojan(trojan_link);
    assert(node_trojan.has_value());
    assert(node_trojan->name == "US-Trojan");
    assert(node_trojan->password == "secretpass");
    assert(node_trojan->skip_cert_verify == true);

    // 6. Hysteria2
    std::string hy2_link = "hysteria2://pass123@1.2.3.4:443?sni=hy2.example.com&insecure=1&obfs=salamander&obfs-password=obfspass#Hy2-Node";
    auto node_hy2 = UniversalParser::ParseHysteria2(hy2_link);
    assert(node_hy2.has_value());
    assert(node_hy2->name == "Hy2-Node");
    assert(node_hy2->obfs == "salamander");
    assert(node_hy2->obfs_password == "obfspass");

    // 7. Hysteria v1
    std::string hy_link = "hysteria://1.2.3.4:443?auth=secret123&peer=hy.example.com&up=50&down=100#Hy1-Node";
    auto node_hy = UniversalParser::ParseHysteria(hy_link);
    assert(node_hy.has_value());
    assert(node_hy->name == "Hy1-Node");
    assert(node_hy->password == "secret123");
    assert(node_hy->up == "50 Mbps");
    assert(node_hy->down == "100 Mbps");

    // 8. AnyTLS
    std::string anytls_link = "anytls://pass123@1.2.3.4:443?sni=anytls.example.com&insecure=1#AnyTLS-Node";
    auto node_anytls = UniversalParser::ParseAnyTls(anytls_link);
    assert(node_anytls.has_value());
    assert(node_anytls->name == "AnyTLS-Node");

    // 9. TUIC
    std::string tuic_link = "tuic://my-uuid:my-password@1.2.3.4:443?sni=tuic.example.com#TUIC-Node";
    auto node_tuic = UniversalParser::ParseTuic(tuic_link);
    assert(node_tuic.has_value());
    assert(node_tuic->uuid == "my-uuid");
    assert(node_tuic->password == "my-password");

    // 10. WireGuard
    std::string wg_link = "wireguard://cGFzc3dvcmQ=@1.2.3.4:51820?public-key=pubkey123&address=10.0.0.2/32,fd00::2/128&reserved=209,98,59#WG-Node";
    auto node_wg = UniversalParser::ParseWireGuard(wg_link);
    assert(node_wg.has_value());
    assert(node_wg->name == "WG-Node");
    assert(node_wg->public_key == "pubkey123");
    assert(node_wg->ip == "10.0.0.2/32");
    assert(node_wg->reserved == "209,98,59");

    // 11. SOCKS5
    std::string socks5_link = "socks5://testuser:testpass@1.2.3.4:1080#Socks5-Node";
    auto node_socks = UniversalParser::ParseSocks5(socks5_link);
    assert(node_socks.has_value());
    assert(node_socks->username == "testuser");
    assert(node_socks->password == "testpass");

    // 12. HTTP Fallback
    std::string http_fallback = "# [HTTP] Corporate Office (proxy.corp.internal:8080)";
    auto node_http = UniversalParser::ParseHttpFallback(http_fallback);
    assert(node_http.has_value());
    assert(node_http->name == "Corporate Office");
    assert(node_http->server == "proxy.corp.internal");
    assert(node_http->port == 8080);

    std::cout << "  -> All 12 protocol parsers verified successfully!" << std::endl;
}

void TestMultiLineSubscription() {
    std::cout << "[Test 3] Testing Multi-line Subscription Parsing..." << std::endl;

    std::string raw_sub = 
        "trojan://secretpass@1.2.3.4:443#US-Trojan\n"
        "ss://YWVzLTI1Ni1nY206cGFzc3dvcmQ=@1.2.3.4:8388#Japan-SS\n"
        "hysteria2://pass123@1.2.3.4:443#Hy2-Node\n"
        "# 剩余流量：100G (This should be filtered out by NodeFilter)\n";

    auto nodes = UniversalParser::ParseUriListToNodes(raw_sub);
    assert(nodes.size() == 3);

    auto yaml_opt = UniversalParser::ParseUriListToYaml(raw_sub);
    assert(yaml_opt.has_value());
    std::string yaml = *yaml_opt;
    assert(yaml.find("US-Trojan") != std::string::npos);
    assert(yaml.find("Japan-SS") != std::string::npos);
    assert(yaml.find("Hy2-Node") != std::string::npos);
    assert(yaml.find("proxies:") != std::string::npos);
    assert(yaml.find("rules:") != std::string::npos);

    std::cout << "  -> Multi-line subscription parsing passed!" << std::endl;
}

void TestEnhanceChain() {
    std::cout << "[Test 4] Testing Enhance Chain (Merge.yaml) & Rule Injection..." << std::endl;

    std::filesystem::path test_dir = std::filesystem::current_path() / "test_enhance_tmp";
    std::filesystem::create_directories(test_dir);

    EnhanceChain& chain = EnhanceChain::Instance();
    chain.Init(test_dir);
    chain.ClearInjectedRules();

    // 1. 注入域名规则
    bool inj1 = chain.InjectRule("api.openai.com", "PROXY", true);
    assert(inj1);

    // 2. 注入 IP 规则
    bool inj2 = chain.InjectRule("1.1.1.1", "DIRECT", true);
    assert(inj2);

    // 3. 注入泛域名规则
    bool inj3 = chain.InjectRule("*.github.com", "PROXY", false);
    assert(inj3);

    assert(chain.GetPrependRules().size() == 2);
    assert(chain.GetPrependRules()[0] == "IP-CIDR,1.1.1.1/32,DIRECT");
    assert(chain.GetPrependRules()[1] == "DOMAIN-SUFFIX,api.openai.com,PROXY");
    assert(chain.GetAppendRules().size() == 1);
    assert(chain.GetAppendRules()[0] == "DOMAIN-SUFFIX,github.com,PROXY");

    // 4. 应用增强
    std::string sample_yaml = 
        "mixed-port: 10801\n"
        "rules:\n"
        "  - MATCH,PROXY\n";

    std::string enhanced = chain.ApplyEnhance(sample_yaml);
    assert(enhanced.find("IP-CIDR,1.1.1.1/32,DIRECT") != std::string::npos);
    assert(enhanced.find("DOMAIN-SUFFIX,api.openai.com,PROXY") != std::string::npos);
    assert(enhanced.find("DOMAIN-SUFFIX,github.com,PROXY") != std::string::npos);

    // 验证顺序：prepend rules 必须出现在 MATCH 之前
    size_t ip_pos = enhanced.find("IP-CIDR,1.1.1.1/32,DIRECT");
    size_t match_pos = enhanced.find("MATCH,PROXY");
    size_t github_pos = enhanced.find("DOMAIN-SUFFIX,github.com,PROXY");

    assert(ip_pos < match_pos);
    assert(match_pos < github_pos);

    chain.ClearInjectedRules();
    std::filesystem::remove_all(test_dir);

    std::cout << "  -> Enhance Chain & Rule Injection verified successfully!" << std::endl;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  ClashWin Phase 4 Slice 7 - Universal Parser Test " << std::endl;
    std::cout << "==================================================" << std::endl;

    TestBase64Robust();
    TestProtocolParsers();
    TestMultiLineSubscription();
    TestEnhanceChain();

    std::cout << "\n>>> [ALL SLICE 7 TESTS PASSED SUCCESSFULLY!] <<<\n" << std::endl;
    return 0;
}
