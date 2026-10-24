#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <unordered_map>
#include <chrono>
#include "clashwin/common/types.h"
#include "clashwin/common/constants.h"

namespace clashwin::core {

class HealthProbeService {
public:
    static HealthProbeService& Instance();

    // 启动前强制扫除残留的孤儿 mini-mihomo.exe 进程
    static void KillAllOrphanCores();

    // 启动常驻 15s 健康探针守护线程
    void Start(uint16_t rest_port = 9098);

    // 停止探针
    void Stop();

    // 手动触发单次立即健康检查
    void TriggerImmediateCheck();

    // 批量触发全节点测速
    void BatchTestAllNodes(uint16_t rest_port = 9098);

    // 检查节点是否处于失败冷却期 (600s)
    bool IsNodeInCooldown(const std::string& node_name) const;

    // 设置状态回调（用于 UI 状态刷新或通知）
    void SetOnStatusChanged(std::function<void(const std::string& active_node, int delay_ms, bool healed)> cb) {
        on_status_changed_ = std::move(cb);
    }

private:
    HealthProbeService();
    ~HealthProbeService();

    void WorkerLoop();
    void PerformHealthCheck();
    void TriggerAutoHeal();

    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    uint16_t rest_port_{9098};

    // 失败计数与动态周期控制
    std::atomic<int> consecutive_failures_{0};
    std::atomic<uint32_t> current_interval_sec_{network::kHealthProbeRegularIntervalSec};

    // 节点失败冷却记录 (节点名 -> 上次失败时间戳 ms)
    mutable std::unordered_map<std::string, int64_t> failed_cooldown_map_;

    std::function<void(const std::string& active_node, int delay_ms, bool healed)> on_status_changed_;
};

} // namespace clashwin::core
