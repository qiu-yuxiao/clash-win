// MainWindow 数据与后台管线（节点加载/切点/测速/导入/TUN守卫/WS管道）。
#include "main_window.h"
#include "main_window_internal.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "config/config.h"
#include "config/proxy_head_state.h"
#include "core/auto_select.h"
#include "core/connection_stale.h"  // BUG-396
#include "core/core_manager.h"
#include "core/monitor.h"
#include "events/notice.h"
#include "mihomo/ws_manager.h"
#include "system/service_runner.h"  // TUN 守卫：服务可用性探测
#include "ui/i18n_text.h"           // F-D01：通知文案走 locales，不硬编码中文
#include "ui/ui_postbox.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/node.h"
#include "util/utf.h"

namespace clashwin::ui::win {

namespace utf = clashwin::utf;

void MainWindow::sync_quick_bar(bool empty_as_dash) {
  if (!node_data_ || !quick_bar_) return;
  const std::string now = node_data_->now();
  const char* fallback = empty_as_dash ? "-" : "暂无激活的代理节点";
  quick_bar_->set_node_text(utf::utf8_to_utf16(now.empty() ? fallback : now));
  quick_bar_->set_badge(node_data_->row_delay(now), !now.empty());
  // 协议胶囊 + 异步地址解析（BUG-356/339：切节点与批量刷延迟 0ms 更新 UI，不阻塞在 IPC）
  // src: src/pages/_layout/components/active-node-card.tsx:155
  std::string type;
  for (const auto& r : node_data_->rows()) {
    if (r.name == now) {
      type = r.type;
      break;
    }
  }
  if (now.empty()) {
    quick_bar_meta_node_.clear();
    quick_bar_meta_addr_.clear();
    quick_bar_->set_node_meta(type, L"");
    return;
  }
  if (now == quick_bar_meta_node_) {
    quick_bar_->set_node_meta(type, quick_bar_meta_addr_);
    return;
  }
  quick_bar_meta_node_ = now;
  quick_bar_meta_addr_.clear();
  quick_bar_->set_node_meta(type, L"");
  std::thread([this, now, type] {
    std::wstring addr;
    auto r = cmd::CommandService::instance().invoke(
        "get_proxy_addr", nlohmann::json::object({{"name", now}}));
    if (r.is_ok() && r.value().is_object()) {
      const std::string server = r.value().value("server", std::string());
      const int port = r.value().value("port", 0);
      if (!server.empty()) {
        addr = utf::utf8_to_utf16(server + ":" + std::to_string(port));
      }
    }
    UiPostbox::instance().post([this, now, type, addr] {
      if (!node_data_ || !quick_bar_ || node_data_->now() != now) return;
      quick_bar_meta_node_ = now;
      quick_bar_meta_addr_ = addr;
      quick_bar_->set_node_meta(type, addr);
    });
  }).detach();
}

void MainWindow::switch_node_optimistic_async(const std::string& target) {
  if (!node_data_ || target.empty()) return;
  // BUG-356：0ms 乐观高亮更新，点击瞬间切换主列表与顶栏活跃节点，失败按代数守卫回滚
  // src: src/hooks/use-proxy-selection.ts:95
  const std::string prev = node_data_->set_now_optimistic(target);
  sync_quick_bar(true);
  const uint64_t epoch =
      switch_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;

  std::thread([this, target, prev, epoch] {
    bool ok = false;
    auto mc = cmd::mihomo();
    if (!mc.is_ok() || !mc.value()) {
      CWLOG_WARN("switch node: mihomo client unavailable");
    } else {
      auto r = mc.value()->select_node_for_group("PROXY", target);
      ok = r.is_ok();
      if (!ok) CWLOG_WARN("switch node failed: " + r.error().message);
    }

    if (ok) {
      // BUG-336：通知后端自愈监控进入手动切点宽限期，并持久化写回当前订阅 selected
      // src: src/hooks/use-proxy-selection.ts:111
      (void)cmd::CommandService::instance().invoke(
          "notify_node_switched", nlohmann::json::object());

      std::string uid;
      if (auto p = config::Config::instance().profiles().latest_arc();
          p && p->current && !p->current->empty()) {
        uid = *p->current;
      }
      if (!uid.empty()) {
        // src: src/hooks/use-proxy-selection.ts:74
        auto pr = cmd::CommandService::instance().invoke(
            "patch_profile",
            nlohmann::json::object(
                {{"index", uid},
                 {"profile",
                  {{"selected",
                    nlohmann::json::array(
                        {{{"name", "PROXY"}, {"now", target}}})}}}}));
        if (!pr.is_ok()) {
          CWLOG_WARN("persist selected node failed: " + pr.error().message);
        }
      }

      // BUG-396（use-proxy-selection.ts:91-92）：前台显式切点后立即精准清理
      // 仍挂在非目标节点上的 PROXY 存量长连接；不再按旧节点全量断流，
      // DIRECT/REJECT 与同节点连接保持零中断。
      if (mc.is_ok() && mc.value()) {
        (void)core::close_stale_proxy_connections(*mc.value(), target);
      }
    }

    UiPostbox::instance().post([this, prev, epoch, ok] {
      if (!node_data_) return;
      if (!ok) {
        if (switch_epoch_.load(std::memory_order_acquire) == epoch) {
          // src: src/hooks/use-proxy-selection.ts:128
          node_data_->set_now_optimistic(prev);
          sync_quick_bar(true);
        }
      }
      // 自编节点切换成功/失败提示已按用户裁决删除（Mini 无对应提示）
    });
  }).detach();
}

void MainWindow::test_single_node_async(const std::string& node) {
  if (!node_data_ || node.empty() || scene::is_preset_proxy(node)) return;
  auto latest = config::Config::instance().profiles().latest_arc();
  const std::string uid = latest ? latest->current.value_or("") : std::string();
  if (uid.empty()) return;

  // BUG-321/357：保存原始延迟与时间戳快照，异常/忙碌/空结果时原样恢复，绝不卡在 -2
  // src: src/services/delay.ts:351
  auto snap = node_data_->snapshot_delays({node});
  const auto started = std::chrono::steady_clock::now();
  node_data_->set_delay(node, -2);  // src: src/services/delay.ts:354
  if (quick_bar_ && node == node_data_->now()) {
    quick_bar_->set_badge(-2, true);  // src: src/services/delay.ts:354
  }

  std::thread([this, node, uid, snap = std::move(snap), started]() mutable {
    nlohmann::json args = nlohmann::json::object();
    args["profile_uid"] = uid;
    args["node_names"] = nlohmann::json::array({node});
    args["sort_type"] = 0;
    args["select"] = false;
    auto r = cmd::CommandService::instance().invoke("trigger_auto_select", args);

    int delay = 0;
    std::string err;
    if (!r.is_ok()) {
      err = r.error().message;
    } else if (!r.value().is_array() || r.value().empty()) {
      err = "AUTO_SELECT_NO_RESULT";  // src: src/services/delay.ts:364
    } else {
      bool matched = false;
      for (const auto& item : r.value()) {
        if (item.is_array() && item.size() == 2 && item[0].is_string() &&
            item[0].get<std::string>() == node) {
          delay = item[1].get<int>();
          matched = true;
          break;
        }
      }
      if (!matched) delay = 0;  // src: src/services/delay.ts:369
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - started)
              .count();
      if (elapsed < 500) {  // src: src/services/delay.ts:373
        std::this_thread::sleep_for(
            std::chrono::milliseconds(500 - elapsed));  // src: src/services/delay.ts:437
      }
    }

    UiPostbox::instance().post(
        [this, node, snap = std::move(snap), delay, err] {
          if (!node_data_) return;
          if (!err.empty()) {
            // src: src/services/delay.ts:383
            node_data_->restore_delays_for_still_pending(snap);
            if (quick_bar_ && node == node_data_->now()) {
              quick_bar_->set_badge(node_data_->row_delay(node), true);
            }
            // src: src/pages/_layout/components/active-node-card.tsx:192
            if (err.find("AUTO_SELECT_BUSY") != std::string::npos ||
                err.find("AUTO_SELECT_MANUAL_PREEMPTED") != std::string::npos) {
              events::NoticeService::instance().push(
                  "info", tr8("settings.mini.testingInProgress"));
            } else if (err.find("AUTO_SELECT_NO_RESULT") != std::string::npos) {
              events::NoticeService::instance().push(
                  "error", tr8("settings.mini.testNotExecuted"));
            } else {
              events::NoticeService::instance().push(
                  "error", tr8("settings.mini.testFailed", {{"message", err}}));
            }
            return;
          }
          node_data_->set_delay(node, delay);
          node_data_->restore_delays_for_still_pending(snap);
          if (quick_bar_ && node == node_data_->now()) {
            quick_bar_->set_badge(delay, true);
          }
          // 自编单节点测速提示已删除（Mini 无对应提示）
        });
  }).detach();
}

void MainWindow::reload_nodes() {
  if (!node_data_ || !quick_bar_) return;
  bool ok = node_data_->load();
  sync_quick_bar(false);
  if (ok) {
    node_retry_left_ = 0;
    KillTimer(hwnd_, detail::kTimerNodeRetry);
  } else if (node_retry_left_ > 0) {
    --node_retry_left_;
    SetTimer(hwnd_, detail::kTimerNodeRetry, 2000, nullptr);
  }
}

void MainWindow::trigger_auto_select_and_refresh(const std::string& uid,
                                                 bool user_initiated,
                                                 bool skip_probe_if_fresh) {
  if (uid.empty()) return;
  const uint64_t gen = auto_select_coord_gen_.fetch_add(1) + 1;
  const uint64_t wait_id = user_initiated ? begin_batch_test_notice() : 0;
  // BUG-321/355：用户主动全量测速（如 F1）先快照并批量置 -2，结束后为仍停在 -2 的行兜底回滚
  // src: src/services/delay.ts:445
  scene::NodeData::DelaySnapshotMap snap;
  bool ui_fresh_active = false;
  if (node_data_) {
    std::vector<std::string> names;
    for (const auto& row : node_data_->rows()) {
      if (!scene::is_preset_proxy(row.name)) {
        names.push_back(row.name);
      }
    }
    if (skip_probe_if_fresh && !user_initiated && !names.empty()) {
      ui_fresh_active = node_data_->has_fresh_active_exit_latency(names);
    }
    if (user_initiated && !names.empty()) {
      snap = node_data_->snapshot_delays(names);
      node_data_->set_delays_batch(names, -2);  // src: src/services/delay.ts:452
      if (quick_bar_ && !node_data_->now().empty() &&
          !scene::is_preset_proxy(node_data_->now())) {
        quick_bar_->set_badge(-2, true);  // src: src/services/delay.ts:452
      }
    }
  }
  std::thread([this, uid, gen, user_initiated, skip_probe_if_fresh,
               ui_fresh_active, wait_id, snap = std::move(snap)]() mutable {
    auto is_stale_coord = [&]() -> bool {
      if (app::Lifecycle::instance().is_exiting()) return true;
      if (!user_initiated &&
          auto_select_coord_gen_.load(std::memory_order_acquire) != gen) {
        return true;
      }
      auto latest = config::Config::instance().profiles().latest_arc();
      return !latest || !latest->current || *latest->current != uid;
    };

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(10);  // src: src/pages/_layout/utils/profile-coordination.ts:240
    while (std::chrono::steady_clock::now() < deadline) {
      if (is_stale_coord()) break;
      if (cmd::mihomo_unchecked().get_base_config().is_ok()) break;
      std::this_thread::sleep_for(
          std::chrono::milliseconds(200));  // src: src/pages/_layout/utils/profile-coordination.ts:240
    }
    if (!user_initiated && is_stale_coord()) return;

    // 连续性优先（← profile-coordination.ts / monitor.rs）：非用户主动触发时若持久出口仍在过滤子集内且非假节点，只测速不换点
    bool should_select = user_initiated;
    if (!user_initiated) {
      bool keep_exit = false;
      auto grp_init = cmd::mihomo_unchecked().get_group_by_name("PROXY");
      if (grp_init.is_ok()) {
        std::string filter_text;
        try {
          auto st = config::proxy_head_state::load();
          if (auto gs = config::proxy_head_state::get_group_state(st, uid, "PROXY")) {
            filter_text = gs->filter_text;
          }
        } catch (...) {
        }
        const bool confirmed_fake =
            grp_init.value().now && !grp_init.value().now->empty() &&
            util::is_confirmed_fake_node(*grp_init.value().now);
        const std::vector<std::string>* all_ptr =
            grp_init.value().all ? &(*grp_init.value().all) : nullptr;
        keep_exit = core::CoreMonitor::persisted_exit_is_usable(
            grp_init.value().now, all_ptr, filter_text, confirmed_fake);
        // BUG-402：若允许首启/轻量跳过且持久出口有效 + 活跃延迟仍新鲜（45min 内），零拨测直接刷新展示
        if (skip_probe_if_fresh && keep_exit && !is_stale_coord()) {
          bool fresh_exit = ui_fresh_active;
          if (!fresh_exit && grp_init.value().now && !grp_init.value().now->empty()) {
            auto px = cmd::mihomo_unchecked().get_proxy_by_name(*grp_init.value().now);
            if (px.is_ok()) {
              const int64_t epoch_ms =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
              fresh_exit = scene::is_proxy_history_fresh_and_valid(
                  px.value().alive, px.value().history, epoch_ms);
            }
          }
          if (fresh_exit) {
            UiPostbox::instance().post([this] { reload_nodes(); });
            return;
          }
        }
      }
      should_select = !keep_exit;
    }

    // BUG-396（proxy-groups.tsx:408-415）：仅用户主动 F1/过滤词/F4 传
    // clean_stale_connections=true；配置切换后的后台初始化不清理存量连接。
    auto r = cmd::CommandService::instance().invoke(
        "trigger_auto_select",
        nlohmann::json::object({{"profile_uid", uid},
                                {"sort_type", 0},
                                {"select", should_select},
                                {"clean_stale_connections", user_initiated}}));

    scene::BatchTestSummary sum;
    if (user_initiated) {
      if (!r.is_ok()) {
        if (r.error().message.find("AUTO_SELECT_BUSY") != std::string::npos ||
            r.error().message.find("AUTO_SELECT_MANUAL_PREEMPTED") !=
                std::string::npos) {
          sum.busy = true;
        }
      } else if (r.value().is_array() && !r.value().empty()) {
        sum.executed = true;
        auto grp = cmd::mihomo_unchecked().get_group_by_name("PROXY");
        const std::string now_node =
            (grp.is_ok() && grp.value().now) ? *grp.value().now : std::string();
        for (const auto& item : r.value()) {
          if (item.is_array() && item.size() == 2 && item[0].is_string() &&
              item[0].get<std::string>() == now_node) {
            const int d = item[1].get<int>();
            if (d >= core::kNodeDelayMinMs && d < core::kNodeDelayMaxMs) {
              sum.selected = true;
              sum.selected_node = now_node;
              sum.selected_delay_ms = d;
            }
            break;
          }
        }
      }
    }

    UiPostbox::instance().post(
        [this, user_initiated, wait_id, sum, snap = std::move(snap)] {
          if (node_data_ && !snap.empty()) {
            node_data_->restore_delays_for_still_pending(snap);
          }
          reload_nodes();
          if (node_data_ && !snap.empty()) {
            node_data_->restore_delays_for_still_pending(snap);
          }
          if (user_initiated) finish_batch_test_notice(wait_id, sum);
        });
  }).detach();
}

// TUN 自动关闭守卫 ← use-system-state.ts:99-128
//   TUN 已开 且 当前不可用（非管理员 且 服务不可用）→
//   patchVerge({enable_tun_mode:false, enable_system_proxy:true})，
//   成功 info tunMode.autoDisabled / 失败 error tunMode.autoDisableFailed。
//   Mini 的 isLoading / isStartingUp / isLongOperationRunning 三重闸门在此对应为：
//   - isStartingUp → 启动 10s 宽限（STARTUP_GRACE_MS，services/delay.ts:39）
//   - isLongOperationRunning → install_service 是阻塞命令，UI 线程期间定时器不触发
//   - isLoading → 探测本身在 worker 线程串行（busy 标志），不重复判定
void MainWindow::tun_guard_tick() {
  if (start_ms_ != 0 && detail::now_ms() - start_ms_ < 10000) return;  // 启动宽限
  if (detail::now_ms() < tun_guard_cool_until_) return;               // 1s 冷却（:123-127）
  if (tun_guard_busy_.exchange(true)) return;                         // 上一轮探测未完
  std::thread([this] {
    const bool tun_on =
        config::Config::instance().verge().latest_arc()->enable_tun_mode.value_or(false);
    bool available = true;
    if (tun_on) {
      available = cmd::is_admin();
      if (!available) {
        auto runner = system::get_service_runner();
        available = runner && runner->is_available();
      }
    }
    if (tun_on && !available) {
      auto r = cmd::CommandService::instance().invoke(
          "patch_verge_config",
          nlohmann::json::object(
              {{"enable_tun_mode", false}, {"enable_system_proxy", true}}));
      // ← :110-120 成功/失败两条提示，绝静默
      events::NoticeService::instance().push(
          r.is_ok() ? "info" : "error",
          tr8(r.is_ok()
                  ? "settings.sections.system.notifications.tunMode.autoDisabled"
                  : "settings.sections.system.notifications.tunMode.autoDisableFailed"));
      if (r.is_ok()) {
        UiPostbox::instance().post([this] {
          if (settings_drawer_) settings_drawer_->sync_takeover(true, false);
        });
      }
      tun_guard_cool_until_ = detail::now_ms() + 1000;
    }
    tun_guard_busy_.store(false);
  }).detach();
}

// 首启就绪轮询：refreshProxy（getProxies 全量）+ activeDelay（src:
// first-run-guide-dialog.tsx:143-170/:236-245）；row_delay(-1/-2) → null(-1)
void MainWindow::sync_first_run_proxy() {
  if (!node_data_) return;
  node_data_->load();
  const std::string now = node_data_->now();
  const int delay = now.empty() ? -1 : node_data_->row_delay(now);
  if (quick_bar_) {
    quick_bar_->set_node_text(utf::utf8_to_utf16(now.empty() ? "暂无激活的代理节点" : now));
    quick_bar_->set_badge(delay, !now.empty());
  }
  if (first_run_) {
    first_run_->set_active_node_name(now);
    first_run_->set_active_delay_ms(delay >= 0 ? delay : -1);
  }
}

void MainWindow::run_import_profile_async(
    const std::string& url, const std::string& name,
    std::function<void(bool ok, const std::string& err)> done) {
  // ← src/pages/_layout/hooks/use-profile-actions.ts:211-289 handleImportProfile（抽屉导入与首次向导共用同一条链路）
  if (profile_importing_.exchange(true)) {
    // 自编"正在导入"提示已删除（Mini 无此常驻提示）
    if (done) done(false, std::string());
    return;
  }
  const uint64_t wait_id = 0;  // Mini 无常驻导入提示
  std::thread([this, url, name, wait_id, done = std::move(done)]() mutable {
    cmd::LongOperationGuard long_guard;
    nlohmann::json args = nlohmann::json::object({{"url", url}});
    if (!name.empty()) args["name"] = name;
    bool used_proxy = false;
    auto r = cmd::CommandService::instance().invoke("import_profile", args);
    if (!r.is_ok()) {
      // src: src/pages/_layout/hooks/use-profile-actions.ts:272-274 首次直连失败 → 走 Clash 自身代理重试
      args["option"] =
          nlohmann::json::object({{"with_proxy", false}, {"self_proxy", true}});
      r = cmd::CommandService::instance().invoke("import_profile", args);
      used_proxy = true;
    }
    const bool ok = r.is_ok();
    std::string new_uid;
    if (ok && r.value().is_object()) {
      new_uid = r.value().value("uid", std::string());
      if (!new_uid.empty()) {
        auto cur = config::Config::instance().profiles().latest_arc()->current;
        if (!cur || *cur != new_uid) {
          cmd::CommandService::instance().invoke(
              "patch_profiles_config",
              nlohmann::json::object({{"current", new_uid}}));
        }
      }
    }
    const std::string err = ok ? std::string() : r.error().message;
    UiPostbox::instance().post(
        [this, wait_id, ok, used_proxy, new_uid, err, done = std::move(done)] {
          profile_importing_.store(false);
          events::NoticeService::instance().hide(wait_id);
          if (ok) {
            events::NoticeService::instance().push(
                "success",
                tr8(used_proxy
                        ? "shared.feedback.notifications.importWithClashProxy"
                        : "shared.feedback.notifications.importSuccess"));
            if (settings_drawer_ && settings_drawer_->profile_card()) {
              settings_drawer_->profile_card()->reload_profiles();
              if (settings_drawer_->bounds().w > 0)
                settings_drawer_->layout_tree(settings_drawer_->bounds());
              settings_drawer_->invalidate();
            }
            reload_nodes();
            if (!new_uid.empty()) trigger_auto_select_and_refresh(new_uid);
          } else {
            // ← src/pages/_layout/hooks/use-profile-actions.ts:279-282 showNotice.error('profiles.page.feedback.notifications.importFail', detail)
            events::NoticeService::instance().push(
                "error", tr8("profiles.page.feedback.notifications.importFail") +
                             " " + err);
          }
          if (done) done(ok, err);
        });
  }).detach();
}

void MainWindow::build_ws_pipes() {
  // T14：WS 订阅 → UiPostbox 转投 UI 线程（复用 T13 范式）
  ws_ = std::make_unique<mihomo::WsManager>();
  // BUG-406: 停核前第 0ms 断开全部本地命名管道 WS 与空闲 HTTP 连接池句柄
  auto alive = alive_;
  core::CoreManager::instance().set_pre_stop_hook([this, alive] {
    if (alive && alive->load(std::memory_order_acquire) && ws_) {
      ws_->disconnect_all(0u);
    }
    cmd::mihomo_unchecked().http().clear_idle();
  });
  ws_->configure(clashwin::util::dirs::ipc_path(),
                 clashwin::config::consts::kDefaultSecret);
  ws_sub_traffic_ = ws_->subscribe_traffic([this](const std::string& payload) {
    UiPostbox::instance().post([this, payload] {
      if (traffic_pane_) traffic_pane_->on_traffic_payload(payload);
    });
  });
  ws_sub_conn_ = 0;
  update_conn_ws_state();
}

void MainWindow::update_conn_ws_state() {
  // Mini 同构：/connections 流仅抽屉打开且宽>285 且未最小化时订阅（连接面板只在抽屉右列）
  const bool needed = settings_drawer_ && settings_drawer_->is_open() &&
                      !bp_.is_narrow() && !bp_.is_mini() &&
                      bp_.logical_h() > win::kMiniHeightThreshold &&
                      (!hwnd_ || !IsIconic(hwnd_));
  if (needed) {
    conn_gc_deadline_ms_ = 0;
  } else if (conn_gc_deadline_ms_ == 0) {
    conn_gc_deadline_ms_ = detail::now_ms() + 30000;
  }
  if (needed && ws_sub_conn_ == 0 && ws_) {
    ws_sub_conn_ = ws_->subscribe_connections(
        [this](const std::string& payload) {
          UiPostbox::instance().post([this, payload] {
            if (conn_data_) conn_data_->consume(payload);
          });
        });
    std::thread([this] {
      auto mc = cmd::mihomo();
      if (!mc.is_ok() || !mc.value()) return;
      auto cr = mc.value()->get_connections();
      if (!cr.is_ok()) return;
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& c : cr.value().connections) {
        nlohmann::json m = {
            {"network", c.metadata.network},
            {"type", c.metadata.type},
            {"host", c.metadata.host},
            {"sourceIP", c.metadata.source_ip},
            {"sourcePort", c.metadata.source_port},
            {"destinationIP", c.metadata.destination_ip},
            {"destinationPort", c.metadata.destination_port},
            {"remoteDestination", c.metadata.remote_destination},
            {"process", c.metadata.process},
            {"processPath", c.metadata.process_path}};
        arr.push_back({{"id", c.id},
                       {"upload", c.upload},
                       {"download", c.download},
                       {"start", c.start},
                       {"chains", c.chains},
                       {"rule", c.rule},
                       {"rulePayload", c.rule_payload},
                       {"metadata", std::move(m)}});
      }
      nlohmann::json snap = {
          {"type", "snapshot"},
          {"data",
           {{"epochId", ""},
            {"sequenceId", -1},
            {"uploadTotal", cr.value().upload_total},
            {"downloadTotal", cr.value().download_total},
            {"memory", cr.value().memory},
            {"connections", std::move(arr)}}}};
      std::string payload = snap.dump();
      UiPostbox::instance().post([this, payload] {
        if (conn_data_ && conn_data_->active().empty()) {
          conn_data_->consume(payload);
          conn_data_->flush_notify_for_test();
        }
      });
    }).detach();
  } else if (!needed && ws_sub_conn_ != 0 && ws_) {
    ws_->unsubscribe(ws_sub_conn_);
    ws_sub_conn_ = 0;
  }
}

}  // namespace clashwin::ui::win
