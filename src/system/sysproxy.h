#pragma once
// T18 系统代理（← core/sysopt.rs + sysproxy-rs 语义）。
//
// 【原版机制查证】sysproxy-rs 为外部 crate（git 依赖，本地无源码），
// census 未确认项明确「sysproxy crate 落注册表的具体键写法」未确认。
// 本实现按 census §124 已确认口径落地：
//   写 HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings
//   （ProxyEnable/ProxyServer/ProxyOverride/AutoConfigURL）+
//   InternetSetOption(SETTINGS_CHANGED/REFRESH) 广播（浏览器立即感知）；
//   RAS 条目经 RasEnumEntriesW 遍历逐条同步（差异声明见报告）。
//
// 【测试铁律】真系统代理开关绝不在测试中触发——全部经 ISysProxyAccess
// 注入（mock 注册表语义）；生产才挂 WininetSysProxyAccess。
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "clashwin/config/models.h"
#include "util/result.h"

namespace clashwin::system {

// 错误码（对齐 cmd 层 errs::kIo 口径；system 层不依赖 cmd 头）
inline constexpr int kErrIo = 2906;

// ← sysproxy-rs Sysproxy{enable,host,port,bypass}
struct SysProxyState {
  bool enable = false;
  std::wstring host = L"127.0.0.1";
  uint16_t port = 0;
  std::wstring bypass;
};

// ← sysproxy-rs Autoproxy{enable,url}（PAC / AutoConfigURL）
struct AutoProxyState {
  bool enable = false;
  std::wstring url;
};

// 进入系统代理前的 OS 全量快照（退出/切回手动时逐值还原）。
// 注：Mini 的 sysopt.rs（ClashVerge/src-tauri/src/core/sysopt.rs:147-179）
// 只改 enable/host/port，不备份原值；本项按验收要求做增强。
struct SysProxySnapshot {
  bool valid = false;
  bool enable = false;
  std::wstring server;             // ProxyServer 原串（"host:port"）
  std::wstring bypass;             // ProxyOverride
  std::wstring auto_config_url;    // AutoConfigURL；空 = 无 PAC
  bool auto_detect = false;        // AutoDetect DWORD
  bool proxy_server_present = true;
  bool proxy_override_present = true;
  bool auto_config_present = false;
  bool auto_detect_present = false;
};

// 注册表语义访问层（生产 = WinINET 注册表 + 广播；测试 = mock）
class ISysProxyAccess {
 public:
  virtual ~ISysProxyAccess() = default;
  virtual Result<SysProxyState> read_sysproxy() = 0;
  // 写 ProxyEnable/ProxyServer/ProxyOverride 后广播
  // PROXY_SETTINGS_CHANGED + REFRESH（浏览器立即感知）
  virtual Result<void> write_sysproxy(const SysProxyState& s) = 0;
  virtual Result<AutoProxyState> read_autoproxy() = 0;
  // 写/清 AutoConfigURL 后广播（enable=false 时删除键）
  virtual Result<void> write_autoproxy(const AutoProxyState& a) = 0;

  // 全量快照（含 AutoDetect）。默认实现由 read_sysproxy/read_autoproxy 组合，
  // mock 无需改动；生产实现覆盖以保留原串与 AutoDetect。
  virtual Result<SysProxySnapshot> read_snapshot();
  virtual Result<void> write_snapshot(const SysProxySnapshot& s);
  // true = 允许把备份落盘（崩溃后下次启动读回）。mock 默认 false（零磁盘污染）。
  virtual bool persistent() const { return false; }
};

// 生产实现（WinINET 注册表 + InternetSetOption 广播 + RAS 条目同步）
ISysProxyAccess* wininet_access();

// ← sysopt.rs:54 DEFAULT_BYPASS（逐字符一致；分号分隔）
extern const wchar_t kDefaultBypass[];

// ← sysopt.rs:56-75 get_bypass：
//   custom 空 → DEFAULT；use_default(默认 true) → "DEFAULT,custom"；否则 custom
std::wstring build_bypass(bool use_default,
                          const std::string& custom_bypass_utf8);

// ← sysopt.rs:26-34 proxy_apply_steps（顺序规则；纯函数，照原版单测语义）：
//   纯全局（sys on, pac off）→ [Autoproxy 清, Sysproxy 开]
//   其余 → [Sysproxy, Autoproxy]
enum class ApplyStep { Sysproxy, Autoproxy };
std::vector<ApplyStep> proxy_apply_steps(bool sys_enabled, bool auto_enabled);

// ← sysopt.rs Sysopt 单例
class Sysopt {
 public:
  static Sysopt& instance();

  // 访问层注入（测试 mock；缺省 nullptr = 未装配，写操作返回错误留痕）
  void set_access(ISysProxyAccess* access) { access_ = access; }
  ISysProxyAccess* access() const { return access_; }

  // ← sysopt.rs update_sysproxy 的入参视图（从 IVerge/clash 提取；
  // 直接传参避免本层反向依赖 Config，装配层负责提取）
  struct VergeView {
    bool enable_system_proxy = false;
    bool proxy_auto_config = false;
    bool enable_proxy_guard = false;
    uint64_t proxy_guard_duration = 30;  // ← proxy_guard_duration.unwrap_or(30)
    bool use_default_bypass = true;      // ← unwrap_or(true)
    std::string system_proxy_bypass;     // utf8；空 = 无自定义
    std::string proxy_host = "127.0.0.1";
    uint16_t mixed_port = 0;             // ← verge_mixed_port 或 clash mixed_port
    uint16_t pac_port = 33335;           // ← 单实例端口（对齐 Mini 33335）
  };

  // ← sysopt.rs:129-197 update_sysproxy：
  //   master switch 关 → 双关 + GuardType::None（启动时由此收敛残留代理）
  //   PAC 模式 → sys off + auto on；纯全局 → sys on + auto off
  //   guard 仅 enable_proxy_guard 时挂 Sysproxy/Autoproxy 状态
  Result<void> update_sysproxy(const VergeView& v);

  // ← sysopt.rs:200-235 reset_sysproxy：CAS 防重入 + 双关 + guard 停
  Result<void> reset_sysproxy();

  // ← sysopt.rs:88-118 refresh_guard：sys/guard 关 → stop；
  //   否则 set_interval(proxy_guard_duration) + start
  void refresh_guard(const VergeView& v);

  // ← sysopt.rs:124-126 wait_idle（等 update 完成，读侧用）
  void wait_idle();

 private:
  Sysopt() = default;
  // 进入系统代理前调用：无可恢复备份时快照 OS 原值并落盘
  void ensure_backup_locked();
  // 恢复原值 + 清备份（返回写失败以便上抛）
  Result<void> restore_backup_locked();
  bool load_backup_file();
  void save_backup_file() const;
  void clear_backup_file() const;

  std::mutex update_lock_;              // ← update_lock: TokioMutex
  std::atomic<bool> reset_flag_{false};  // ← reset_sysproxy: AtomicBool CAS
  ISysProxyAccess* access_ = nullptr;    // 不持有所有权
  SysProxySnapshot backup_;              // 进入前 OS 原值（内存快照）
  bool have_backup_ = false;
};

// VergeView 提取（← sysopt.rs:132-143 从 IVerge/clash 提取口径）：
//   mixed_port = verge_mixed_port，缺失时用 clash_mixed_port 兜底；
//   proxy_host 缺省 127.0.0.1；pac_port = 单实例 server 端口
Sysopt::VergeView make_verge_view(const config::IVerge& v,
                                  uint16_t clash_mixed_port,
                                  uint16_t pac_port);

}  // namespace clashwin::system
