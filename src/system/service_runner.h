#pragma once
// T25 服务操作执行器与 Mock 注入抽象
// 统一封装 install / uninstall / reinstall / repair / is_available
// 保证测试环境下 100% 离线、零 UAC 弹窗、零实际注册表/服务篡改
#include "core/service_mode.h"
#include "util/result.h"

#include <memory>
#include <string>

namespace clashwin::system {

class IServiceCommandRunner {
 public:
  virtual ~IServiceCommandRunner() = default;
  virtual Result<void> run_installer(bool install) = 0;
  // 卸载（← Mini: src-tauri/src/core/service.rs:44-74 uninstall_service()）
  // 与 Mini 一致：后端能力存在、UI 不暴露；reinstall / repair 的"先卸载"也走这里。
  // 默认委托 run_installer(false)，Mock 无需实现即可复用既有日志语义。
  virtual Result<void> run_uninstaller() { return run_installer(false); }
  virtual bool is_installed() = 0;
  // 服务注册细节：默认查真实 SCM / 注册表；测试可覆盖以获得确定性
  virtual bool is_binary_valid();
  virtual core::service_mode::ScmServiceRegistration inspect_registration();
  virtual bool is_image_own();
  // ImagePath 二进制哈希是否与上游基线一致（总纲 §5.2.2「借用与归还」）
  virtual bool is_image_upstream_baseline();
  virtual Result<void> start_service() = 0;
  virtual Result<void> stop_service() = 0;
  virtual bool wait_for_ready(int timeout_ms) = 0;
  virtual bool is_available() = 0;
};

class DefaultServiceCommandRunner : public IServiceCommandRunner {
 public:
  Result<void> run_installer(bool install) override;
  Result<void> run_uninstaller() override;
  bool is_installed() override;
  core::service_mode::ScmServiceRegistration inspect_registration() override;
  Result<void> start_service() override;
  Result<void> stop_service() override;
  bool wait_for_ready(int timeout_ms) override;
  bool is_available() override;
};

std::shared_ptr<IServiceCommandRunner> get_service_runner();
void set_service_runner(std::shared_ptr<IServiceCommandRunner> runner);

}  // namespace clashwin::system
