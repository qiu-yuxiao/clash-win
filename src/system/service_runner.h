#pragma once
// T25 服务操作执行器与 Mock 注入抽象
// 统一封装 install / uninstall / reinstall / repair / is_available
// 保证测试环境下 100% 离线、零 UAC 弹窗、零实际注册表/服务篡改
#include "util/result.h"

#include <memory>
#include <string>

namespace clashwin::system {

class IServiceCommandRunner {
 public:
  virtual ~IServiceCommandRunner() = default;
  virtual Result<void> run_installer(bool install) = 0;
  virtual bool is_installed() = 0;
  virtual Result<void> start_service() = 0;
  virtual Result<void> stop_service() = 0;
  virtual bool wait_for_ready(int timeout_ms) = 0;
  virtual bool is_available() = 0;
};

class DefaultServiceCommandRunner : public IServiceCommandRunner {
 public:
  Result<void> run_installer(bool install) override;
  bool is_installed() override;
  Result<void> start_service() override;
  Result<void> stop_service() override;
  bool wait_for_ready(int timeout_ms) override;
  bool is_available() override;
};

std::shared_ptr<IServiceCommandRunner> get_service_runner();
void set_service_runner(std::shared_ptr<IServiceCommandRunner> runner);

}  // namespace clashwin::system
