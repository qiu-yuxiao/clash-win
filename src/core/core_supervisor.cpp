#include "clashwin/core/core_supervisor.h"
#include "clashwin/common/constants.h"
#include <iostream>
#include <vector>

namespace clashwin::core {

CoreSupervisor::CoreSupervisor() {
    SetupJobObject();
}

CoreSupervisor::~CoreSupervisor() {
    StopCore();
    if (h_job_object_) {
        CloseHandle(h_job_object_);
        h_job_object_ = NULL;
    }
}

CoreSupervisor& CoreSupervisor::Instance() {
    static CoreSupervisor instance;
    return instance;
}

void CoreSupervisor::SetupJobObject() {
    if (h_job_object_) return;

    h_job_object_ = CreateJobObjectW(NULL, NULL);
    if (!h_job_object_) return;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
    // 核心铁律：主进程退出或异常崩溃时，操作系统内核强制级联强杀内核子进程，绝对零孤儿进程残留
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    SetInformationJobObject(
        h_job_object_,
        JobObjectExtendedLimitInformation,
        &jeli,
        sizeof(jeli)
    );
}

bool CoreSupervisor::StartCore(const std::filesystem::path& core_path,
                             const std::filesystem::path& config_dir,
                             uint16_t mixed_port,
                             uint16_t rest_port) {
    (void)mixed_port;
    (void)rest_port;
    if (IsRunning()) {
        return true;
    }

    if (!std::filesystem::exists(core_path)) {
        std::cerr << "[CoreSupervisor] 错误：内核路径不存在: " << core_path << "\n";
        return false;
    }

    std::wstring cmd_line = L"\"" + core_path.wstring() + L"\" -d \"" + config_dir.wstring() + L"\"";

    // 设置 Go 运行时内存抑制环境变量（通过环境继承传递给子进程）
    SetEnvironmentVariableW(L"GOMEMLIMIT", L"96MiB");
    SetEnvironmentVariableW(L"GOGC", L"50");
    SetEnvironmentVariableW(L"GOMAXPROCS", L"2");

    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // 隐藏控制台黑框

    PROCESS_INFORMATION pi = {0};

    // 复制命令行字符串用于 CreateProcessW
    std::vector<wchar_t> cmd_buffer(cmd_line.begin(), cmd_line.end());
    cmd_buffer.push_back(L'\0');

    std::wstring working_dir = config_dir.wstring();

    BOOL success = CreateProcessW(
        NULL,
        cmd_buffer.data(),
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED,
        NULL, // 继承当前环境（含 GOMEMLIMIT）
        working_dir.c_str(),
        &si,
        &pi
    );

    if (!success) {
        DWORD err = GetLastError();
        std::cerr << "[CoreSupervisor] CreateProcessW 失败，错误码: " << err << "\n";
        return false;
    }

    // 绑定至 Windows Job Object
    if (h_job_object_) {
        AssignProcessToJobObject(h_job_object_, pi.hProcess);
    }

    // 恢复运行
    ResumeThread(pi.hThread);

    h_process_ = pi.hProcess;
    h_thread_ = pi.hThread;
    process_id_ = pi.dwProcessId;

    return true;
}

void CoreSupervisor::StopCore() {
    if (h_process_) {
        TerminateProcess(h_process_, 0);
        WaitForSingleObject(h_process_, 2000);
        CloseHandle(h_process_);
        h_process_ = NULL;
    }
    if (h_thread_) {
        CloseHandle(h_thread_);
        h_thread_ = NULL;
    }
    process_id_ = 0;
}

bool CoreSupervisor::RestartCore(const std::filesystem::path& core_path,
                               const std::filesystem::path& config_dir,
                               uint16_t mixed_port,
                               uint16_t rest_port) {
    StopCore();
    Sleep(200);
    return StartCore(core_path, config_dir, mixed_port, rest_port);
}

bool CoreSupervisor::IsRunning() const {
    if (!h_process_) return false;
    DWORD exit_code = 0;
    if (GetExitCodeProcess(h_process_, &exit_code)) {
        return exit_code == STILL_ACTIVE;
    }
    return false;
}

} // namespace clashwin::core
