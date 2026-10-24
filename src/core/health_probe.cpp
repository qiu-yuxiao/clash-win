#include <windows.h>
#include <tlhelp32.h>
#include "clashwin/core/health_probe.h"
#include "clashwin/core/mihomo_client.h"
#include "clashwin/config/profile_manager.h"
#include <iostream>

namespace clashwin::core {

static int64_t CurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

HealthProbeService& HealthProbeService::Instance() {
    static HealthProbeService instance;
    return instance;
}

HealthProbeService::HealthProbeService() = default;

HealthProbeService::~HealthProbeService() {
    Stop();
}

void HealthProbeService::KillAllOrphanCores() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = {sizeof(pe)};
    if (Process32FirstW(hSnapshot, &pe)) {
        DWORD currentPid = GetCurrentProcessId();
        do {
            if (_wcsicmp(pe.szExeFile, L"mini-mihomo.exe") == 0) {
                if (pe.th32ProcessID != currentPid) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        TerminateProcess(hProc, 0);
                        WaitForSingleObject(hProc, 1000);
                        CloseHandle(hProc);
                    }
                }
            }
        } while (Process32NextW(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);
}

void HealthProbeService::Start(uint16_t rest_port) {
    if (running_) return;

    rest_port_ = rest_port;
    running_ = true;
    consecutive_failures_ = 0;
    current_interval_sec_ = network::kHealthProbeRegularIntervalSec;

    worker_thread_ = std::thread(&HealthProbeService::WorkerLoop, this);
}

void HealthProbeService::Stop() {
    running_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void HealthProbeService::TriggerImmediateCheck() {
    std::thread([this]() {
        PerformHealthCheck();
    }).detach();
}

bool HealthProbeService::IsNodeInCooldown(const std::string& node_name) const {
    auto it = failed_cooldown_map_.find(node_name);
    if (it == failed_cooldown_map_.end()) return false;

    int64_t elapsedSec = (CurrentTimeMs() - it->second) / 1000;
    return elapsedSec < network::kFailedServerCooldownSec; // 600秒冷却
}

void HealthProbeService::WorkerLoop() {
    while (running_) {
        uint32_t interval = current_interval_sec_.load();
        for (uint32_t i = 0; i < interval * 2; ++i) {
            if (!running_) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (running_) {
            PerformHealthCheck();
        }
    }
}

void HealthProbeService::PerformHealthCheck() {
    std::string activeNode = MihomoClient::Instance().GetCurrentSelectedProxy(rest_port_);
    if (activeNode.empty()) return;

    int delay = MihomoClient::Instance().TestProxyDelay(activeNode, rest_port_, 2000);

    if (delay > 0 && delay < 2000) {
        // 探测成功，重置失败状态
        consecutive_failures_ = 0;
        current_interval_sec_ = network::kHealthProbeRegularIntervalSec; // 恢复恒定 15s

        if (on_status_changed_) {
            on_status_changed_(activeNode, delay, false);
        }
    } else {
        // 探测失败或超时
        int fails = ++consecutive_failures_;
        failed_cooldown_map_[activeNode] = CurrentTimeMs();

        // 动态收紧为 3 秒高频复检
        current_interval_sec_ = network::kHealthProbeRetryIntervalSec;

        if (on_status_changed_) {
            on_status_changed_(activeNode, -1, false);
        }

        // 核心防抖自愈：严格执行连续 2 次失败才触发换点
        if (fails >= static_cast<int>(network::kHealthProbeFailStrikes)) {
            TriggerAutoHeal();
        }
    }
}

void HealthProbeService::TriggerAutoHeal() {
    const auto& nodes = config::ProfileManager::Instance().GetNodes();
    std::string currentActive = MihomoClient::Instance().GetCurrentSelectedProxy(rest_port_);

    for (const auto& node : nodes) {
        if (node.name == currentActive) continue;
        if (IsNodeInCooldown(node.name)) continue; // 避开冷却期节点

        int testDelay = MihomoClient::Instance().TestProxyDelay(node.name, rest_port_, 2000);
        if (testDelay > 0 && testDelay < 2000) {
            // 找到健康候选节点，执行平滑自愈切换
            if (MihomoClient::Instance().SelectProxy("PROXY", node.name, rest_port_)) {
                config::ProfileManager::Instance().SetCurrentActiveNode(node.name);
                consecutive_failures_ = 0;
                current_interval_sec_ = network::kHealthProbeRegularIntervalSec;

                if (on_status_changed_) {
                    on_status_changed_(node.name, testDelay, true); // healed = true
                }
                return;
            }
        }
    }
}

void HealthProbeService::BatchTestAllNodes(uint16_t rest_port) {
    const auto& nodes = config::ProfileManager::Instance().GetNodes();
    for (const auto& node : nodes) {
        int delay = MihomoClient::Instance().TestProxyDelay(node.name, rest_port, 2000);
        if (delay <= 0 || delay >= 2000) {
            failed_cooldown_map_[node.name] = CurrentTimeMs();
        }
    }
}

} // namespace clashwin::core
