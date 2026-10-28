#pragma once
// AUTO_SELECT 自动选点 + 激活校准（T16）——← module/monitor.rs 全语义。
//
// 【与原版的锁差异声明（BUG-121 教训）】原版 AUTO_SELECT_RUNNING 为
// AtomicBool CAS 互斥；早期实现存在错误路径下锁不释放导致**永久卡死**
// （实机已知 BUG-121），后版加了 Drop Guard。本实现从第一天起就用
// RAII Guard（构造 CAS 成功才持有，析构必释放），任何路径（含 panic/
// 异常/提前 return）都不可能留下 true 态——正确语义优先于复刻历史行为。
//
// 择优规则（← trigger_backend_auto_select_inner）：
// - display 全量（未测得=0 Timeout）；candidates = [30, 2000)
// - 失败服务器避让：本层留 hook（failed_servers 快照源 = 健康检测归 T19
//   监控线程），空集时为 no-op
// - select：双重校验 current_uid → 选 candidates 最小延迟 → 切换 →
//   **回写 profile.selected（filterText 子集校验，越界不写回）**
// - sort_type：0=head state / 1=延迟升序（默认）/ 2=名称（仅展示序）
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "delay_manager.h"

namespace clashwin::core {

// 选点/校准钩子（生产 = Config profiles/MihomoClient；测试 mock）
struct AutoSelectHooks {
  // 当前激活配置 uid（双重校验 ← monitor.rs 活动配置被改则舍弃切换）
  std::function<std::optional<std::string>()> current_uid;
  // 过滤词读取（子集校验/全量过滤；lowercase 由实现保证）
  std::function<std::string()> filter_text;
  // dummy 节点判定（← util/node is_dummy_node）
  std::function<bool(const std::string& name)> is_dummy;
  // 切换节点（PUT /proxies/PROXY）
  std::function<bool(const std::string& group, const std::string& node)>
      select_node;
  // 回写 selected（uid + node；失败返回 false）
  std::function<bool(const std::string& uid, const std::string& node)>
      write_selected;
  // 读取 profile.selected 当前值（子集校验用）
  std::function<std::optional<std::string>(const std::string& uid)>
      read_selected;
  // 失败服务器快照（健康检测源，T18 guard_monitor；空集 = 不避让）
  std::function<std::vector<std::string>()> failed_servers;
  // 节点 → 服务器映射（避让用；空 = 不避让）
  std::function<std::map<std::string, std::string>()> node_server_map;
  // 内核就绪等待（T18 转正：CoreMonitor::wait_for_clash_ready；返回
  // false = 等待超时，trigger 仍继续（原版语义：等待尽力而为不阻断））
  std::function<bool()> wait_ready;
};

class AutoSelect {
 public:
  static AutoSelect& instance();

  void set_hooks(AutoSelectHooks hooks);

  struct Outcome {
    std::vector<std::pair<std::string, uint32_t>> display;
    bool selected = false;
    bool busy = false;  // AUTO_SELECT_RUNNING 占用（返回繁忙）
  };

  // ← trigger_backend_auto_select（含互斥/校验/择优/切换/回写全流程）
  Outcome trigger(const std::string& profile_uid,
                  std::optional<std::vector<std::string>> node_names,
                  int sort_type, bool select, bool skip_wait_ready = false);

  // 纯择优计算（测试直测；无锁副作用）：
  // candidates=[30,2000) → 失败服务器避让 → 最小延迟
  static std::optional<std::string> pick_fastest(
      const std::vector<std::pair<std::string, uint32_t>>& display,
      const std::function<std::vector<std::string>()>& failed_servers = nullptr,
      const std::function<std::map<std::string, std::string>()>&
          node_server_map = nullptr);

  // 激活校准防线（三重之一，← use-profiles activateSelected 语义）：
  // 持久 selected 若不在 filterText 子集内（越界/被污染）→ 不写回内核，
  // 改回写内核实际节点（now）；子集内 → 正常应用。
  // 返回最终应生效的节点（内核视角）。
  static std::string calibrate_selected(
      const std::string& uid, const std::string& filter_text_lower,
      const std::string& kernel_now,
      const std::function<std::optional<std::string>(const std::string&)>&
          read_selected,
      const std::function<bool(const std::string&, const std::string&)>&
          write_selected);

  // 唤醒静默测速（T16⑤）：5min 冷却 + 复用 trigger 互斥；T19 挂触发
  bool silent_test_on_wakeup(const std::string& profile_uid);
  int64_t wakeup_cooldown_remaining_ms() const;

 private:
  AutoSelect() = default;
  AutoSelectHooks hooks_;
  std::atomic<bool> running_{false};
  std::atomic<int64_t> last_wakeup_ms_{0};
};

}  // namespace clashwin::core
