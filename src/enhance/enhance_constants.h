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

}  // namespace clashwin::enhance::consts
