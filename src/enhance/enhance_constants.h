#pragma once
// ← ClashVerge src-tauri/src/constants.rs（enhance 相关常量）
#include <string>

namespace clashwin::enhance::consts {

// ← constants.rs::tun::TUN_INET6_ADDRESS（IPv6 防泄漏：TUN 的 inet6-address）
inline constexpr const char* kTunInet6Address = "fdfe:dcba:9876::1/126";

// ← constants.rs::tun::DEFAULT_FAKE_IP_FILTER（fake-ip 默认免分配名单，
// 防 Windows NCSI 离线感叹号 / 局域网失效 / NTP 授时失效）
inline const char* const kDefaultFakeIpFilter[] = {
    "dns.msftncsi.com",
    "+.msftconnecttest.com",
    "+.msftncsi.com",
    "*.lan",
    "*.local",
    "localhost.ptlogin2.qq.com",
    "time.*.com",
    "time.*.gov",
    "time.*.edu.cn",
    "time.*.apple.com",
    "time1.*.com",
    "time2.*.com",
    "time3.*.com",
    "time4.*.com",
    "time5.*.com",
    "time6.*.com",
    "time7.*.com",
    "ntp.*.com",
    "*.time.edu.cn",
    "*.ntp.org.cn",
    "+.pool.ntp.org",
};

// ← constants.rs::timing::INTERNAL_CONTROL_TIMEOUT_MS（脚本执行看门狗）
inline constexpr int kScriptTimeoutMs = 3000;

// ← script.rs 输出限制
inline constexpr size_t kMaxOutputs = 1000;
inline constexpr size_t kMaxOutputSize = 1024 * 1024;        // 1MB
inline constexpr size_t kMaxJsonSize = 10 * 1024 * 1024;     // 10MB

// ← tmpl.rs::ITEM_SCRIPT（默认脚本模板）
inline constexpr const char* kItemScript =
    "// Define main function (script entry)\n"
    "\n"
    "function main(config, profileName) {\n"
    "  return config;\n"
    "}\n";

// ← chain.rs::builtin（内置脚本，ChainSupport::ClashMeta）
// meta_hy_alpn.js：hysteria alpn 字符串转数组（meta 1.13.2 兼容）
inline constexpr const char* kBuiltinScriptHyAlpn = R"JS(// This function is exported for use by the Clash core
// eslint-disable-next-line unused-imports/no-unused-vars
function main(config, _name) {
  if (Array.isArray(config.proxies)) {
    config.proxies.forEach((p, i) => {
      if (p.type === 'hysteria' && typeof p.alpn === 'string') {
        config.proxies[i].alpn = [p.alpn]
      }
    })
  }
  return config
}
)JS";

// meta_guard.js：mode=script → rule 归一
inline constexpr const char* kBuiltinScriptMetaGuard = R"JS(// This function is exported for use by the Clash core
// eslint-disable-next-line unused-imports/no-unused-vars
function main(config, _name) {
  if (config.mode === 'script') {
    config.mode = 'rule'
  }
  return config
}
)JS";

}  // namespace clashwin::enhance::consts
