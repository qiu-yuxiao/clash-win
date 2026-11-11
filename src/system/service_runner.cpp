#include "system/service_runner.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <mutex>
#include <string_view>

#include "core/service_mode.h"
#include "mihomo/service_ipc.h"
#include "system/coexist.h"
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

// 服务配套 exe 定位：正式安装包在 resources\；绿色/测试布局把随包依赖放在 sidecar\ 或根目录。
std::wstring find_companion_exe(const wchar_t* leaf) {
  const std::wstring first = util::dirs::app_resources_dir() + leaf;
  for (const std::wstring& dir : {first,
                                  util::dirs::exe_dir() + L"\\sidecar" + leaf,
                                  util::dirs::exe_dir() + leaf}) {
    if (std::filesystem::exists(dir)) return dir;
  }
  return {};
}

// 提权执行服务安装/卸载器（UAC 一次确认），等待退出并回传退出码
Result<void> run_elevated(const std::wstring& exe, const char* label) {
  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.lpVerb = L"runas";  // UAC 用户确认
  const std::wstring work_dir = std::filesystem::path(exe).parent_path().wstring();
  sei.hwnd = ::GetForegroundWindow();
  sei.lpFile = exe.c_str();
  sei.lpDirectory = work_dir.c_str();
  sei.nShow = SW_HIDE;
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;

  // ★ 自报家门（用户 2026-11-11 反馈：UAC 弹窗未标明来源，用户不清楚是哪来的）。
  // UAC 本身的发布者文字由签名决定（本程序未签名，见 Known Limitation d，不可改），
  // 因此在提权前先弹出一个明确标注「Clash Win」的确认框，让用户清楚来源与目的。
  const int pre_answer = ::MessageBoxW(
      ::GetForegroundWindow(),
      L"Clash Win 需要管理员权限来安装 / 管理系统代理服务（clash_verge_service），"
      L"用于启用 TUN 模式或正常联网。\r\n\r\n"
      L"点击「确定」后 Windows 会弹出用户账户控制（UAC）确认；"
      L"若您并未主动开启相关功能，可点击「取消」。",
      L"Clash Win", MB_OKCANCEL | MB_ICONINFORMATION | MB_TOPMOST);
  if (pre_answer != IDOK) {
    return Result<void>::err(
        {errs::kIo, std::string(label) + " cancelled before UAC (user pre-confirm)"});
  }

  if (!ShellExecuteExW(&sei)) {
    return Result<void>::err({errs::kIo, std::string(label) + " failed (UAC cancelled?)"});
  }
  if (sei.hProcess) {
    WaitForSingleObject(sei.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    if (code != 0) {
      return Result<void>::err({errs::kIo,
          std::string(label) + " exit code " + std::to_string(code)});
    }
  }
  return Result<void>::ok();
}

}  // namespace

Result<void> DefaultServiceCommandRunner::run_installer(bool install) {
  // ← Mini: src-tauri/src/core/service.rs:76-121 install_service()
  //   （非管理员走 RunasCommand 提权；管理员直跑）。我方向来只用 UAC 一支。
  // 【服务名】clash_verge_service 为与 Clash Mini 共用的特权服务，安装前校验名称唯一。
  if (!install) {
    return Result<void>::err({errs::kIo,
        "run_installer(false) is not a valid operation; use run_uninstaller()"});
  }
  if (std::wstring_view(core::service_mode::kServiceNameW) !=
      std::wstring_view(L"clash_verge_service")) {
    return Result<void>::err({errs::kIo,
        "service name guard: target service name is not clash_verge_service"});
  }
  const std::wstring exe = find_companion_exe(L"\\clash-verge-service-install.exe");
  if (exe.empty()) {
    return Result<void>::err({errs::kIo,
        "installer not found: " + utf::utf16_to_utf8(
            util::dirs::app_resources_dir() + L"\\clash-verge-service-install.exe")});
  }
  return run_elevated(exe, "installer");
}

Result<void> DefaultServiceCommandRunner::run_uninstaller() {
  // ← Mini: src-tauri/src/core/service.rs:44-74 uninstall_service()
  //   卸载器缺失 → 直接报错（Mini:54-56 bail）；退出码非 0 → 报错（Mini:66-71）。
  const std::wstring exe = find_companion_exe(L"\\clash-verge-service-uninstall.exe");
  if (exe.empty()) {
    return Result<void>::err({errs::kIo,
        "uninstaller not found: " + utf::utf16_to_utf8(
            util::dirs::app_resources_dir() + L"\\clash-verge-service-uninstall.exe")});
  }
  return run_elevated(exe, "uninstaller");
}

bool DefaultServiceCommandRunner::is_installed() {
  return core::service_mode::is_service_installed();
}

bool IServiceCommandRunner::is_binary_valid() {
  return core::service_mode::is_service_binary_valid();
}

bool IServiceCommandRunner::is_image_own() {
  return coexist::service_image_is_own();
}

bool IServiceCommandRunner::is_image_upstream_baseline() {
  return coexist::service_image_matches_upstream();
}

Result<void> DefaultServiceCommandRunner::stop_service() {
  return core::service_mode::stop_service();
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
