// T03 门禁测试②：端口 guard / controller 冲突寻位 / secret 修正 / 载入管线
// （对照 clash.rs guard 官方测试 test_clash_info 的全部用例 + 端口 0/越界回退）
#include <winsock2.h>
#include <ws2tcpip.h>

#include <fstream>
#include <string>
#include <utility>

#include "config/clash.h"
#include "util/str_utils.h"
#include "clashwin/config/constants.h"
#include "test_util.h"
#include "config/yaml_serde.h"

using namespace clashwin;
using namespace clashwin::config;
using namespace clashwin::util;
using namespace clashwin::config::consts;

namespace {

IClashTemp map_with(std::initializer_list<std::pair<const char*, std::string>> kvs) {
  YAML::Node m(YAML::NodeType::Map);
  for (auto& [k, v] : kvs) m[k] = v;
  return IClashTemp(m);
}

}  // namespace

UT_TEST(t03_guard_mixed_port_cases) {
  // ← clash.rs::guard_mixed_port + 官方用例
  UT_CHECK_EQ(IClashTemp::guard_mixed_port(YAML::Node(YAML::NodeType::Map)),
              10801);  // 缺失 → 默认
  UT_CHECK_EQ(IClashTemp::guard_mixed_port(map_with({{"mixed-port", "0"}}).mapping()),
              10801);  // 0 → 默认
  UT_CHECK_EQ(IClashTemp::guard_mixed_port(map_with({{"mixed-port", "8888"}}).mapping()),
              8888);
  UT_CHECK_EQ(IClashTemp::guard_mixed_port(map_with({{"mixed-port", "65537"}}).mapping()),
              1);  // u16 截断（Rust as u16 wrapping）
  UT_CHECK_EQ(IClashTemp::guard_mixed_port(map_with({{"mixed-port", ""}}).mapping()),
              10801);
  UT_CHECK_EQ(IClashTemp::guard_mixed_port(map_with({{"mixed-port", "abc"}}).mapping()),
              10801);
  // 字符串数字可解析
  UT_CHECK_EQ(IClashTemp::guard_mixed_port(map_with({{"mixed-port", "7897"}}).mapping()),
              7897);
}

UT_TEST(t03_guard_socks_and_http_port) {
  UT_CHECK_EQ(IClashTemp::guard_socks_port(YAML::Node(YAML::NodeType::Map)), 10802);
  UT_CHECK_EQ(IClashTemp::guard_socks_port(map_with({{"socks-port", "0"}}).mapping()),
              10802);
  // ← clash.rs:283 形态：http port 兜底（Mini 7899 → Clash Win 错开为 10814）
  UT_CHECK_EQ(IClashTemp::guard_port(YAML::Node(YAML::NodeType::Map)), 7899);
  UT_CHECK_EQ(IClashTemp::guard_port(map_with({{"port", "0"}}).mapping()), 7899);
  UT_CHECK_EQ(IClashTemp::guard_port(map_with({{"port", "10803"}}).mapping()), 10803);
}

UT_TEST(t03_guard_controller_parse_cases) {
  // ← clash.rs::test_clash_info 官方用例（external-controller 解析）
  UT_CHECK_EQ(IClashTemp::guard_server_ctrl(YAML::Node(YAML::NodeType::Map)),
              std::string("127.0.0.1:9098"));
  UT_CHECK_EQ(IClashTemp::guard_server_ctrl(map_with({{"external-controller", ""}}).mapping()),
              std::string("127.0.0.1:9098"));
  UT_CHECK_EQ(IClashTemp::guard_server_ctrl(map_with({{"external-controller", "   :98888 "}}).mapping()),
              std::string("127.0.0.1:9098"));  // 越界端口 → 非法 → 默认
  UT_CHECK_EQ(IClashTemp::guard_server_ctrl(map_with({{"external-controller", "127.0.0.1:8888"}}).mapping()),
              std::string("127.0.0.1:8888"));
  UT_CHECK_EQ(IClashTemp::guard_server_ctrl(map_with({{"external-controller", "0.0.0.0:8080"}}).mapping()),
              std::string("0.0.0.0:8080"));  // 保留原样
  UT_CHECK_EQ(IClashTemp::guard_server_ctrl(map_with({{"external-controller", "[::]:8080"}}).mapping()),
              std::string("[::]:8080"));
  UT_CHECK_EQ(IClashTemp::guard_server_ctrl(map_with({{"external-controller", "192.168.1.1:8080"}}).mapping()),
              std::string("192.168.1.1:8080"));
  UT_CHECK_EQ(IClashTemp::guard_server_ctrl(map_with({{"external-controller", "192.168.1.1:80800"}}).mapping()),
              std::string("127.0.0.1:9098"));
  UT_CHECK_EQ(IClashTemp::guard_server_ctrl(map_with({{"external-controller", ":9888"}}).mapping()),
              std::string("127.0.0.1:9888"));  // ':' 前缀补 127.0.0.1

  // client 口径：unspecified → 127.0.0.1
  UT_CHECK_EQ(IClashTemp::guard_client_ctrl(map_with({{"external-controller", "0.0.0.0:8080"}}).mapping()),
              std::string("127.0.0.1:8080"));
  UT_CHECK_EQ(IClashTemp::guard_client_ctrl(map_with({{"external-controller", "[::]:8080"}}).mapping()),
              std::string("127.0.0.1:8080"));
}

UT_TEST(t03_guard_client_info) {
  IClashTemp empty;
  auto info = empty.get_client_info();
  UT_CHECK_EQ(info.mixed_port, 10801);
  UT_CHECK_EQ(info.socks_port, 10802);
  UT_CHECK_EQ(info.port, 7899);
  UT_CHECK_EQ(info.server, std::string("127.0.0.1:9098"));
  UT_CHECK(!info.secret.has_value());
  UT_CHECK(!info.ipv6);
  UT_CHECK(!info.allow_lan);

  IClashTemp full = map_with({
      {"mixed-port", "8888"},
      {"external-controller", "0.0.0.0:8080"},
      {"secret", "s3cret"},
      {"ipv6", "true"},
      {"allow-lan", "true"},
  });
  auto info2 = full.get_client_info();
  UT_CHECK_EQ(info2.mixed_port, 8888);
  UT_CHECK_EQ(info2.server, std::string("127.0.0.1:8080"));
  UT_CHECK(info2.secret && *info2.secret == "s3cret");
  UT_CHECK(info2.ipv6);
  UT_CHECK(info2.allow_lan);
}

UT_TEST(t03_controller_conflict_finds_free_port) {
  // 独占 9108 → load/resolve 自动寻 9109 起的空闲口
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  SOCKET blocker = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(39098);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  UT_CHECK(bind(blocker, (sockaddr*)&addr, sizeof(addr)) == 0);
  UT_CHECK(listen(blocker, 1) == 0);

  // 9108 被占且探测无成功响应 → 冲突；寻位从 9108 起找到 9109
  UT_CHECK(IClashTemp::is_port_conflict(39098, "x"));
  UT_CHECK_EQ(IClashTemp::find_free_controller_port(39098), 39099);

  YAML::Node m(YAML::NodeType::Map);
  m["external-controller"] = "127.0.0.1:39098";
  m["secret"] = "adapted-by-qiu-yuxiao";
  IClashTemp c(m);
  c.resolve_controller_conflict();
  UT_CHECK_EQ(c.mapping()["external-controller"].Scalar(),
              "127.0.0.1:" + std::to_string(IClashTemp::find_free_controller_port(consts::kControllerPort)));

  closesocket(blocker);
  WSACleanup();

  // 空闲时保持 9108
  UT_CHECK(!IClashTemp::is_port_conflict(39098, "x"));
  YAML::Node m2(YAML::NodeType::Map);
  m2["external-controller"] = "127.0.0.1:39098";
  m2["secret"] = "adapted-by-qiu-yuxiao";
  IClashTemp c2(m2);
  c2.resolve_controller_conflict();
  UT_CHECK_EQ(c2.mapping()["external-controller"].Scalar(),
              std::string("127.0.0.1:39098"));
}

UT_TEST(t03_secret_fix_and_load_pipeline) {
  // 载入管线：写一份残缺 config.yaml（无 config_version、tun.stack=system、
  // secret=set-your-secret、缺大量模板键）→ load → 迁移+补键+guard
  wchar_t buf[MAX_PATH];
  GetTempPathW(MAX_PATH, buf);
  std::wstring root = std::wstring(buf) + L"clashwin_t03\\load";
  CreateDirectoryW((std::wstring(buf) + L"clashwin_t03").c_str(), nullptr);
  CreateDirectoryW(root.c_str(), nullptr);
  DeleteFileW((root + L"\\config.yaml").c_str());
  DeleteFileW((root + L"\\config.yaml.bak.v1").c_str());
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", root.c_str());

  {
    std::ofstream f(root + L"\\config.yaml", std::ios::binary | std::ios::trunc);
    f << "mixed-port: 0\n"
      << "tun:\n  stack: system\n"
      << "secret: set-your-secret\n";
  }

  IClashTemp c = clash::load();
  // 迁移：无版本号 → v1 → v2（stack 归一化）
  UT_CHECK_EQ(serde::opt_u32(c.mapping(), "config_version"), 2u);
  UT_CHECK_EQ(c.mapping()["tun"]["stack"].Scalar(), std::string("gvisor"));
  // secret 修正
  UT_CHECK_EQ(c.mapping()["secret"].Scalar(), std::string("adapted-by-qiu-yuxiao"));
  // 模板键补齐 + guard
  UT_CHECK_EQ(c.get_mixed_port(), 10801);  // 0 → 回退
  UT_CHECK_EQ(c.mapping()["log-level"].Scalar(), std::string("warning"));
  UT_CHECK_EQ(c.mapping()["unified-delay"].Scalar(), std::string("true"));
  // 迁移前备份存在
  UT_CHECK(GetFileAttributesW((root + L"\\config.yaml.bak.v1").c_str()) !=
           INVALID_FILE_ATTRIBUTES);

  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
}
