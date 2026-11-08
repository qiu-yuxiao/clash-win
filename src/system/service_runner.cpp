#include "system/service_runner.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <mutex>
#include <string_view>

#include "core/service_mode.h"
#include "mihomo/service_ipc.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"

namespace clashwin::system {

namespace {

namespace errs {
constexpr int kIo = 2906;
}

std::mutex g_runner_mu;
std::shared_ptr<IServiceCommandRunner> g_runner;

}  // namespace

Result<void> DefaultServiceCommandRunner::run_installer(bool install) {
  // ← Mini: src-tauri/src/core/service.rs:76-121 install_service()
  //   （非管理员走 RunasCommand 提权；管理员直跑）。我方向来只用 UAC 一支。
  // 【共存铁律】clash_verge_service 为与 Clash Mini 共用的特权服务：
  //   - 仅允许"服务缺失 + 安装"路径（install==true）；卸载器一律拒绝；
  //   - 安装前校验目标服务名必须是唯一名 clash_verge_service（禁止第二服务名）；
  //   - 已存在时的安装/重装/覆盖由调用方（cmd_service）拦截，不会到达此处。
  if (!install) {
    return Result<void>::err({errs::kIo,
        "Clash Win never uninstalls the shared clash_verge_service"});
  }
  if (std::wstring_view(core::service_mode::kServiceNameW) !=
      std::wstring_view(L"clash_verge_service")) {
    return Result<void>::err({errs::kIo,
        "service name guard: target service name is not clash_verge_service"});
  }
  // 安装器位置：正式安装包在 resources\；绿色/测试布局把随包依赖放在 sidecar\ 或根目录。
  const std::wstring leaf = L"\\clash-verge-service-install.exe";
  const std::wstring first = util::dirs::app_resources_dir() + leaf;
  std::wstring exe;
  for (const std::wstring& dir : {first,
                                  util::dirs::exe_dir() + L"\\sidecar" + leaf,
                                  util::dirs::exe_dir() + leaf}) {
    if (std::filesystem::exists(dir)) {
      exe = dir;
      break;
    }
  }
  if (exe.empty()) {
    return Result<void>::err({errs::kIo, "installer not found: " + utf::utf16_to_utf8(first)});
  }
  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.lpVerb = L"runas";  // UAC 用户确认
  const std::wstring work_dir = std::filesystem::path(exe).parent_path().wstring();
  sei.hwnd = ::GetForegroundWindow();
  sei.lpFile = exe.c_str();
  sei.lpDirectory = work_dir.c_str();
  sei.nShow = SW_HIDE;
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  if (!ShellExecuteExW(&sei)) {
    return Result<void>::err({errs::kIo, "launch installer failed (UAC cancelled?)"});
  }
  if (sei.hProcess) {
    WaitForSingleObject(sei.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    if (code != 0) {
      return Result<void>::err({errs::kIo, "installer exit code " + std::to_string(code)});
    }
  }
  return Result<void>::ok();
}

bool DefaultServiceCommandRunner::is_installed() {
  return core::service_mode::is_service_installed();
}

Result<void> DefaultServiceCommandRunner::start_service() {
  return core::service_mode::start_service();
}

bool DefaultServiceCommandRunner::wait_for_ready(int timeout_ms) {
  mihomo::ServiceIpc ipc;
  ipc.connect();
  core::service_mode::ServiceIpcAdapter adapter(&ipc);
  return core::service_mode::wait_for_service_ready(adapter, timeout_ms);
}

bool DefaultServiceCommandRunner::is_available() {
  mihomo::ServiceIpc ipc;
  if (ipc.connect().is_err()) {
    CWLOG_WARN("service availability: connect pipe failed");
    return false;
  }
  core::service_mode::ServiceIpcAdapter adapter(&ipc);
  auto probe = adapter.magic_probe();
  if (probe.is_err()) {
    CWLOG_WARN("service availability: magic probe failed: " + probe.error().message);
    return false;
  }
  return true;
}

std::shared_ptr<IServiceCommandRunner> get_service_runner() {
  std::lock_guard<std::mutex> lock(g_runner_mu);
  if (!g_runner) {
    g_runner = std::make_shared<DefaultServiceCommandRunner>();
  }
  return g_runner;
}

void set_service_runner(std::shared_ptr<IServiceCommandRunner> runner) {
  std::lock_guard<std::mutex> lock(g_runner_mu);
  g_runner = std::move(runner);
}

}  // namespace clashwin::system
