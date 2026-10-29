#include "system/service_runner.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <mutex>

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
  std::wstring base = util::dirs::app_resources_dir();
  std::wstring exe = base + (install ? L"\\clash-verge-service-install.exe"
                                     : L"\\clash-verge-service-uninstall.exe");
  if (!std::filesystem::exists(exe)) {
    return Result<void>::err({errs::kIo, "installer not found: " + utf::utf16_to_utf8(exe)});
  }
  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.lpVerb = L"runas";
  sei.lpFile = exe.c_str();
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
    return false;
  }
  core::service_mode::ServiceIpcAdapter adapter(&ipc);
  return core::service_mode::is_service_ready(adapter);
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
