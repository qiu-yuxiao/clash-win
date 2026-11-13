#pragma once
// ← ClashVerge src-tauri/src/feat/config.rs:84-195 UpdateFlags / determine_update_flags
// 纯函数：从旧配置、patch 与当前内核运行模式推导副作用标志位。
// 独立出头文件便于单元测试直接断言（BUG-304/305 口径回归）。
#include <cstdint>

#include "clashwin/config/models.h"
#include "nlohmann/json.hpp"

namespace clashwin::cmd {

// ← feat/config.rs:84-100 bitflags（u16）
enum UpdateFlags : uint16_t {
  kFRestartCore = 1 << 0,
  kFClashConfig = 1 << 1,
  kFVergeConfig = 1 << 2,
  kFLaunch = 1 << 3,
  kFSysProxy = 1 << 4,
  kFSysTrayIcon = 1 << 5,
  kFLanguage = 1 << 11,
  kFLogLevel = 1 << 12,
  kFLogFile = 1 << 13,
  kFAlwaysOnTop = 1 << 14,
  kFGroupSysTray = kFSysTrayIcon,
};

// ← feat/config.rs:102-106：显式 null 重置标志（如 enable_tun_sniffer: null 回退自动态）
struct VergePatchResets {
  bool reset_tun_sniffer = false;
  bool operator==(const VergePatchResets&) const = default;
};

// ← cmd/verge.rs:5-11 parse_verge_patch_resets
VergePatchResets parse_verge_patch_resets(const nlohmann::json& payload);

// running_service = CoreManager::running_mode() == RunningMode::Service
uint16_t determine_update_flags(const config::IVerge& old_config,
                                const config::IVerge& patch,
                                bool running_service,
                                VergePatchResets resets = {});

}  // namespace clashwin::cmd
