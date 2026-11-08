#include "core/monitor.h"

#include <windows.h>

#include <exception>

#include "util/log.h"
#include "util/dirs.h"
#include "yaml-cpp/yaml.h"
#include <fstream>

namespace clashwin::core {

CoreMonitor& CoreMonitor::instance() {
  static CoreMonitor s;
  return s;
}

void CoreMonitor::set_hooks(Hooks h) {
  std::lock_guard<std::mutex> lock(m_);
  hooks_ = std::move(h);
}

void CoreMonitor::set_params(Params p) {
  std::lock_guard<std::mutex> lock(m_);
  params_ = p;
}

int CoreMonitor::current_interval_secs() const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_));
  if (hooks_.network_online && !hooks_.network_online()) {
    return params_.offline_interval_secs;
  }
  if (api_fail_count_.load() > 0 || node_fail_count_.load() > 0) {
    return params_.retry_interval_secs;
  }
  return params_.normal_interval_secs;
}

void CoreMonitor::start() {
  if (running_.exchange(true)) return;
  worker_ = std::thread([this] {
    while (running_.load()) {
      int interval = current_interval_secs();
      // 分片等待（stop/kick 即时退出）
      for (int waited = 0; running_.load() && waited < interval; waited += 1) {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait_for(lock, std::chrono::seconds(1),
                     [this] { return !running_.load() || kick_.load(); });
        if (kick_.exchange(false)) break;
      }
      if (!running_.load()) break;
      // 守护线程绝不能因异常逃逸而 terminate：restart_core 钩子会同步执行
      // enhance pipeline / 内核拉起，任一环节抛出 yaml/文件异常都会让整个进程
      // 以 0xC0000409 abort。这里兜住并记录，下一轮继续自愈。
      try {
        tick_once();
      } catch (const std::exception& ex) {
        CWLOG_ERROR(std::string("CoreMonitor: tick 异常（已兜住继续运行）: ") + ex.what());
      } catch (...) {
        CWLOG_ERROR("CoreMonitor: tick 未知异常（已兜住继续运行）");
      }
    }
  });
}

void CoreMonitor::stop() {
  if (!running_.exchange(false)) return;
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void CoreMonitor::kick() {
  kick_.store(true);
  cv_.notify_all();
}

void CoreMonitor::record_server_failure(const std::string& server) {
  int64_t now = hooks_.now_ms ? hooks_.now_ms()
                              : static_cast<int64_t>(GetTickCount64());
  std::lock_guard<std::mutex> lock(failed_m_);
  failed_servers_[server] = now;
}

void CoreMonitor::clear_server_failures() {
  std::lock_guard<std::mutex> lock(failed_m_);
  failed_servers_.clear();
}

std::map<std::string, std::string> CoreMonitor::load_node_server_map() {
  std::map<std::string, std::string> server_map;
  std::wstring path = util::dirs::clash_mini_path();
  std::ifstream fin(path, std::ios::binary);
  if (!fin.is_open()) return server_map;
  std::string content((std::istreambuf_iterator<char>(fin)),
                       std::istreambuf_iterator<char>());
  try {
    YAML::Node root = YAML::Load(content);
    if (root && root["proxies"] && root["proxies"].IsSequence()) {
      for (const auto& px : root["proxies"]) {
        if (px.IsMap() && px["name"] && px["server"]) {
          std::string name = px["name"].as<std::string>("");
          std::string server = px["server"].as<std::string>("");
          if (!name.empty() && !server.empty()) {
            server_map[name] = server;
          }
        }
      }
    }
  } catch (...) {
  }
  return server_map;
}

std::vector<std::string> CoreMonitor::snapshot_failed_servers() {
  int64_t now = hooks_.now_ms ? hooks_.now_ms()
                              : static_cast<int64_t>(GetTickCount64());
  std::vector<std::string> out;
  std::lock_guard<std::mutex> lock(failed_m_);
  for (auto it = failed_servers_.begin(); it != failed_servers_.end();) {
    if (now - it->second >= params_.failed_server_cooldown_ms) {
      it = failed_servers_.erase(it);  // 冷却过期移除
    } else {
      out.push_back(it->first);
      ++it;
    }
  }
  return out;
}

CoreMonitor::Action CoreMonitor::heal(const std::string& uid) {
  int64_t now = hooks_.now_ms ? hooks_.now_ms()
                              : static_cast<int64_t>(GetTickCount64());
  int64_t last = last_heal_ms_.load();
  if (last != 0 && now - last < params_.heal_cooldown_ms) {
    CWLOG_INFO("CoreMonitor: 自愈冷却中，跳过");
    return Action::Idle;
  }
  last_heal_ms_.store(now);
  if (!hooks_.auto_select_trigger || uid.empty()) return Action::Idle;

  auto r = hooks_.auto_select_trigger();
  if (r.is_ok() && r.value()) {
    // 自愈成功：清零计数
    api_fail_count_.store(0);
    node_fail_count_.store(0);
    heal_fail_count_.store(0);
    return Action::Healed;
  }
  if (r.is_ok() && !r.value()) {
    // BUSY（AutoSelect 占用）→ 不计入失败、回退冷却时间戳允许下次重试（monitor.rs:1568）
    last_heal_ms_.store(last);
    return Action::Idle;
  }

  // 自愈失败计数累计
  int fails = heal_fail_count_.fetch_add(1) + 1;

  // 阶梯 2：连续 5 次自愈选点失败 → 弹窗警报（fire_self_heal_alert，monitor.rs:1496-1508）
  if (fails >= params_.alert_fail_threshold) {
    heal_fail_count_.store(0);
    CWLOG_WARN("CoreMonitor: 连续 5 次自愈选点失败，弹出系统警报");
    if (hooks_.alert) {
      hooks_.alert(L"Clash Win - 网络警报",
                   L"后台自动优选节点连续 5 次失败，当前所有代理节点均已失效，无法正常连接网络。\n\n请检查您的网络连接或节点订阅状态。");
      return Action::Alerted;
    }
    return Action::Idle;
  }

  // 阶梯 1：连续 3 次自愈选点失败 → 判断 API 存活（monitor.rs:1594-1621）
  if (fails >= params_.heal_fail_threshold) {
    bool api_ok = hooks_.api_alive ? hooks_.api_alive() : true;
    if (api_ok) {
      CWLOG_INFO("CoreMonitor: 连续 3 次自愈选点失败但内核 API 可达（节点侧问题），跳过内核重启，等待恢复");
      return Action::Idle;
    }
    CWLOG_WARN("CoreMonitor: 连续 3 次自愈选点失败且内核 API 不可达，重启内核以恢复 API 通道");
    heal_fail_count_.store(0);
    if (hooks_.restart_core && hooks_.restart_core().is_ok()) {
      return Action::RestartedOnHealFail;
    }
    if (hooks_.alert) {
      hooks_.alert(L"Clash Win", L"内核守护自愈连续失败，已尝试重启内核仍未恢复，请检查内核状态。");
      return Action::Alerted;
    }
  }

  return Action::Idle;
}

void CoreMonitor::reset_state() {
  api_fail_count_.store(0);
  node_fail_count_.store(0);
  heal_fail_count_.store(0);
  was_offline_.store(false);
  last_heal_ms_.store(0);
  last_tick_ms_.store(0);
  last_node_change_ms_.store(0);
  has_node_change_.store(false);
  {
    std::lock_guard<std::mutex> lock(last_node_m_);
    last_active_node_.clear();
  }
  std::lock_guard<std::mutex> lock(failed_m_);
  failed_servers_.clear();
}

// ★ §5.1 切换宽限期判定（← Mini monitor.rs:1383-1396）
//   切换后 60s 内：失败判定只记日志、不计入连续失败次数。
bool CoreMonitor::in_switch_grace() const {
  if (!has_node_change_.load()) return false;
  int64_t changed = last_node_change_ms_.load();
  int64_t now = hooks_.now_ms ? hooks_.now_ms()
                              : static_cast<int64_t>(GetTickCount64());
  int64_t grace;
  {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_));
    grace = params_.switch_grace_ms;
  }
  return (now - changed) < grace;
}

CoreMonitor::Action CoreMonitor::tick_once() {
  last_tick_ms_.store(hooks_.now_ms ? hooks_.now_ms()
                                    : static_cast<int64_t>(GetTickCount64()));
  Hooks h;
  Params p;
  {
    std::lock_guard<std::mutex> lock(m_);
    h = hooks_;
    p = params_;
  }

  // 1. 进程级直检：进程死 → 立即重启
  if (h.core_alive && !h.core_alive()) {
    CWLOG_WARN("CoreMonitor: 内核进程静默退出（无退出日志）→ 自动重启");
    if (h.restart_core && h.restart_core().is_ok()) return Action::Restarted;
    return Action::Idle;
  }

  // 2. 物理网络离线：跳过检测
  bool online = h.network_online ? h.network_online() : true;
  if (!online) {
    was_offline_.store(true);
    return Action::Idle;
  }
  // 离线 → 在线转移：清零计数并自愈
  if (was_offline_.exchange(false)) {
    api_fail_count_.store(0);
    node_fail_count_.store(0);
    std::string uid = h.current_uid ? h.current_uid() : std::string();
    if (!uid.empty()) return heal(uid);
    return Action::Idle;
  }

  // 3. API 存活：连续 3 次不可达 → 自愈
  bool api_ok = h.api_alive ? h.api_alive() : true;
  if (!api_ok) {
    int fails = api_fail_count_.fetch_add(1) + 1;
    if (fails >= p.api_fail_threshold) {
      api_fail_count_.store(0);
      std::string uid = h.current_uid ? h.current_uid() : std::string();
      return heal(uid);
    }
    return Action::Idle;
  }
  api_fail_count_.store(0);

  // ★ §5.1 活跃节点切换检测（← Mini monitor.rs:1358-1370）
  //   节点发生变化（含自动选点与手动切换）→ 记录切换时刻、起算 60s 宽限期，
  //   并立即清零连续失败计数与重试模式（Mini 同语义）。
  if (h.active_node_name) {
    const std::string active = h.active_node_name();
    bool changed = false;
    {
      std::lock_guard<std::mutex> lock(last_node_m_);
      if (active != last_active_node_) {
        last_active_node_ = active;
        changed = true;
      }
    }
    if (changed) {
      last_node_change_ms_.store(
          h.now_ms ? h.now_ms() : static_cast<int64_t>(GetTickCount64()));
      has_node_change_.store(true);
      node_fail_count_.store(0);
      CWLOG_INFO("CoreMonitor: 活跃节点发生变化，起算 60s 切换宽限期");
    }
  }

  // ★ §5.4 第 3 道铁锁：假/广告节点 → 直接判需重选（← Mini monitor.rs:663-666）
  //   「内核重载默认选中伪节点 → 后台自愈切回真节点 → 重载再次切回伪节点」死循环防线。
  //   注意：此处必须走「假节点 = 需切换」而非「健康」——与 Mini 语义完全一致。
  if (h.is_dummy_active) {
    if (h.is_dummy_active()) {
      CWLOG_WARN("CoreMonitor: 活跃节点是伪节点（广告/公告节点），触发重选");
      // 伪节点属于「明确异常」，不适用切换宽限期的宽松判定
      node_fail_count_.store(0);
      std::string uid = h.current_uid ? h.current_uid() : std::string();
      if (!uid.empty()) return heal(uid);
      return Action::Idle;
    }
  }

  // ★ §5.8 测速失真复核（← Mini monitor.rs:685-703）
  //   拨测链路整体失败（主/备双地址均失败）= 测速链路不可用，
  //   **不可据此判定节点已死** → 本轮 NoAction，等下一周期重试。
  //   （2026-10-18 实测：机场劫持 HTTP 测速地址期间内核返回 all proxies timeout，
  //     app 误判「所有节点不可达」→ 连续多轮自愈全部落空。）
  if (h.probe_metrics_with_fallback) {
    if (!h.probe_metrics_with_fallback()) {
      CWLOG_WARN(
          "CoreMonitor: 测速链路不可用（主/备测速地址均整体失败），本轮不判定节点死活");
      node_fail_count_.store(0);
      return Action::Idle;
    }
  }

  // 4. 节点健康：连续 2 次判死 → 自愈
  bool node_ok = h.node_healthy ? h.node_healthy() : true;
  if (!node_ok) {
    // ★ §5.1 切换宽限期：切到新节点后 60s 内失败判定可接受，
    //   不累计连续失败次数（防乒乓震荡）——← Mini monitor.rs:1383-1396
    if (in_switch_grace()) {
      CWLOG_INFO(
          "CoreMonitor: 活跃节点检测异常，当前处于切换宽限期，不计入连续失败");
      node_fail_count_.store(0);
      return Action::Idle;
    }
    int fails = node_fail_count_.fetch_add(1) + 1;
    if (fails >= p.node_dead_threshold) {
      node_fail_count_.store(0);
      std::string dead_node;
      if (h.active_node_name) {
        dead_node = h.active_node_name();
      }
      if (!dead_node.empty()) {
        auto server_map = load_node_server_map();
        auto it = server_map.find(dead_node);
        if (it != server_map.end() && !it->second.empty()) {
          record_server_failure(it->second);
          CWLOG_INFO("CoreMonitor: 活跃节点连续判死，服务器记入冷却避让: " + it->second);
        }
      }
      std::string uid = h.current_uid ? h.current_uid() : std::string();
      return heal(uid);
    }
    return Action::Idle;
  }
  node_fail_count_.store(0);
  return Action::Idle;
}

bool CoreMonitor::wait_for_clash_ready(const ReadyHooks& h) {
  auto poll = [&](std::function<bool()> ready, int budget_ms) {
    if (!ready) return true;
    int64_t start = h.now_ms ? h.now_ms() : 0;
    for (;;) {
      if (ready()) return true;
      int64_t now = h.now_ms ? h.now_ms() : 0;
      if (now - start >= budget_ms) return false;
      if (h.sleep_ms) h.sleep_ms(300);
      else Sleep(300);
    }
  };
  if (!poll(h.api_alive, 30000)) return false;
  if (!poll(h.group_ready, 20000)) return false;
  if (!poll(h.network_online, 15000)) return false;
  return true;
}

}  // namespace clashwin::core
