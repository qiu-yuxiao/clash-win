#pragma once
// 配置切换/更新互斥锁（T15）——cmd 层（import/update/create/reorder/
// patch_profiles_config）与 core 层 timer 共用同一把锁。
// ← feat.rs CURRENT_SWITCHING_PROFILE: AtomicBool；CAS false→true 语义：
//   已为 true（切换/更新进行中）→ 操作方跳过或拒绝。
// 层级：core 不依赖 cmd → 锁定义在 core，cmd_profile 引用（替代其内部
// static，T09 语义不变）。
#include <atomic>

namespace clashwin::core {

std::atomic<bool>& profile_switching_lock();

// RAII：构造置 true、析构置 false（cmd 层「进入即锁定、退出必释放」）
struct ProfileSwitchGuard {
  explicit ProfileSwitchGuard(std::atomic<bool>& l) : lock_(l) { lock_.store(true); }
  ~ProfileSwitchGuard() { lock_.store(false); }
  ProfileSwitchGuard(const ProfileSwitchGuard&) = delete;
  ProfileSwitchGuard& operator=(const ProfileSwitchGuard&) = delete;

 private:
  std::atomic<bool>& lock_;
};

}  // namespace clashwin::core
