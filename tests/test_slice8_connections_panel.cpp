#include <iostream>
#include <cassert>
#include <filesystem>
#include "clashwin/core/connection_manager.h"
#include "clashwin/config/enhance_chain.h"

using namespace clashwin::core;
using namespace clashwin::config;

void TestConnectionsJsonParsing() {
    std::cout << "[Test 1] Testing /connections JSON parser..." << std::endl;

    std::string sample_json = R"({
      "downloadTotal": 104857600,
      "uploadTotal": 52428800,
      "connections": [
        {
          "id": "conn-001",
          "metadata": {
            "network": "tcp",
            "type": "HTTP",
            "sourceIP": "192.168.1.100",
            "destinationIP": "142.250.190.46",
            "sourcePort": "54321",
            "destinationPort": "443",
            "host": "www.google.com",
            "process": "chrome.exe",
            "processPath": "C:\\Program Files\\Google\\Chrome\\chrome.exe"
          },
          "upload": 2048,
          "download": 65536,
          "curUpload": 1024,
          "curDownload": 32768,
          "start": "2026-10-24T12:00:00Z",
          "chains": ["PROXY", "HK-Node-01"],
          "rule": "DomainSuffix",
          "rulePayload": "google.com"
        },
        {
          "id": "conn-002",
          "metadata": {
            "network": "udp",
            "type": "Socks5",
            "sourceIP": "192.168.1.100",
            "destinationIP": "1.1.1.1",
            "sourcePort": "50000",
            "destinationPort": "53",
            "host": "",
            "process": "dns.exe",
            "processPath": "C:\\Windows\\System32\\dns.exe"
          },
          "upload": 512,
          "download": 1024,
          "curUpload": 256,
          "curDownload": 512,
          "start": "2026-10-24T12:01:00Z",
          "chains": ["DIRECT"],
          "rule": "Match",
          "rulePayload": ""
        }
      ]
    })";

    ConnectionManager& mgr = ConnectionManager::Instance();
    mgr.ParseConnectionsJson(sample_json);

    assert(mgr.GetDownloadTotal() == 104857600);
    assert(mgr.GetUploadTotal() == 52428800);
    assert(mgr.GetDownloadSpeed() == 32768 + 512);
    assert(mgr.GetUploadSpeed() == 1024 + 256);

    auto conns = mgr.GetConnections();
    assert(conns.size() == 2);

    // 验证连接 1
    assert(conns[0].id == "conn-001");
    assert(conns[0].host == "www.google.com");
    assert(conns[0].dest_ip == "142.250.190.46");
    assert(conns[0].dest_port == 443);
    assert(conns[0].network == "tcp");
    assert(conns[0].process == "chrome.exe");
    assert(conns[0].chains.size() == 2);
    assert(conns[0].chains[1] == "HK-Node-01");

    // 验证连接 2
    assert(conns[1].id == "conn-002");
    assert(conns[1].dest_ip == "1.1.1.1");
    assert(conns[1].dest_port == 53);
    assert(conns[1].network == "udp");

    std::cout << "  -> /connections JSON parsing passed!" << std::endl;
}

void TestRightClickRouting() {
    std::cout << "[Test 2] Testing Right-click Rule Injection & Routing..." << std::endl;

    std::filesystem::path test_dir = std::filesystem::current_path() / "test_conn_tmp";
    std::filesystem::create_directories(test_dir);

    EnhanceChain& chain = EnhanceChain::Instance();
    chain.Init(test_dir);
    chain.ClearInjectedRules();

    ConnectionManager& mgr = ConnectionManager::Instance();

    // 1. 测试右键直连域名
    bool r1 = mgr.RouteConnection("conn-001", "DIRECT", true, 9098);
    assert(r1);

    // 验证 Merge.yaml 中 prepend-rules 首位为直连 google.com
    assert(!chain.GetPrependRules().empty());
    assert(chain.GetPrependRules()[0] == "DOMAIN-SUFFIX,www.google.com,DIRECT");

    // 验证被路由的连接已从活跃列表中摘除
    auto conns_after_r1 = mgr.GetConnections();
    assert(conns_after_r1.size() == 1);
    assert(conns_after_r1[0].id == "conn-002");

    // 2. 测试右键代理 IP
    bool r2 = mgr.RouteConnection("conn-002", "PROXY", false, 9098);
    assert(r2);

    assert(chain.GetPrependRules().size() == 2);
    assert(chain.GetPrependRules()[0] == "IP-CIDR,1.1.1.1/32,PROXY");

    auto conns_after_r2 = mgr.GetConnections();
    assert(conns_after_r2.empty());

    chain.ClearInjectedRules();
    std::filesystem::remove_all(test_dir);

    std::cout << "  -> Right-click Routing & Rule Injection passed!" << std::endl;
}

void TestCloseAll() {
    std::cout << "[Test 3] Testing CloseAllConnections..." << std::endl;

    ConnectionManager& mgr = ConnectionManager::Instance();

    std::string sample_json = R"({
      "downloadTotal": 1000,
      "uploadTotal": 500,
      "connections": [
        {
          "id": "c1",
          "metadata": { "destinationIP": "8.8.8.8" }
        }
      ]
    })";
    mgr.ParseConnectionsJson(sample_json);
    assert(mgr.GetConnections().size() == 1);

    mgr.CloseAllConnections(9098);
    assert(mgr.GetConnections().empty());
    assert(mgr.GetDownloadSpeed() == 0);
    assert(mgr.GetUploadSpeed() == 0);

    std::cout << "  -> CloseAllConnections passed!" << std::endl;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  ClashWin Phase 4 Slice 8 - Connections Panel Test" << std::endl;
    std::cout << "==================================================" << std::endl;

    TestConnectionsJsonParsing();
    TestRightClickRouting();
    TestCloseAll();

    std::cout << "\n>>> [ALL SLICE 8 TESTS PASSED SUCCESSFULLY!] <<<\n" << std::endl;
    return 0;
}
