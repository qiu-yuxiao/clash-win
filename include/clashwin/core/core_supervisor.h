#pragma once

#include <string>
#include <filesystem>
#include <windows.h>

namespace clashwin::core {

class CoreSupervisor {
public:
    static CoreSupervisor& Instance();

    // 启动 mini-mihomo.exe 内核并强行绑定 Windows Job Object
    bool StartCore(const std::filesystem::path& core_path,
                   const std::filesystem::path& config_dir,
                   uint16_t mixed_port,
                   uint16_t rest_port);

    // 停止内核子进程
    void StopCore();

    // 重启内核子进程
    bool RestartCore(const std::filesystem::path& core_path,
                     const std::filesystem::path& config_dir,
                     uint16_t mixed_port,
                     uint16_t rest_port);

    // 检查内核进程是否存活
    [[nodiscard]] bool IsRunning() const;

    // 获取内核当前进程 ID
    [[nodiscard]] DWORD GetProcessId() const { return process_id_; }

private:
    CoreSupervisor();
    ~CoreSupervisor();

    void SetupJobObject();

    HANDLE h_job_object_{NULL};
    HANDLE h_process_{NULL};
    HANDLE h_thread_{NULL};
    DWORD process_id_{0};
};

} // namespace clashwin::core
