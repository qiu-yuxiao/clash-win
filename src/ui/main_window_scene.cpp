// MainWindow 控件树构建、场景事件接线与设置状态同步。
#include "main_window.h"
#include "main_window_internal.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "clashwin/config/constants.h"
#include "clashwin/update/client_updater.h"  // install_app_update（B-D44 更新流收口）
#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "config/config.h"
#include "core/autostart.h"
#include "events/event_bus.h"
#include "events/event_names.h"
#include "events/notice.h"
#include "mihomo/ws_manager.h"
#include "ui/i18n_text.h"  // F-D01：通知文案走 locales，不硬编码中文
#include "ui/textfield.h"
#include "ui/ui_postbox.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/node.h"
#include "util/utf.h"

namespace clashwin::ui::win {

namespace fs = std::filesystem;
namespace utf = clashwin::utf;

namespace {
// 剪贴板：TextField 的生产实现（Ctrl+C/V/X 与右键编辑菜单共用）
std::wstring read_clipboard_text() {
  if (!OpenClipboard(nullptr)) return {};
  std::wstring out;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
      out.assign(p);
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return out;
}

void write_clipboard_text(const std::wstring& s) {
  if (!OpenClipboard(nullptr)) return;
  EmptyClipboard();
  const size_t bytes = (s.size() + 1) * sizeof(wchar_t);
  if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
    if (void* p = GlobalLock(h)) {
      memcpy(p, s.c_str(), bytes);
      GlobalUnlock(h);
      if (SetClipboardData(CF_UNICODETEXT, h)) {
        CloseClipboard();  // 成功后所有权归系统
        return;
      }
    }
    GlobalFree(h);  // 未交出：自行释放
  }
  CloseClipboard();
}
}  // namespace

uint64_t MainWindow::push_tracked_wait_notice(const std::string& msg) {
  const uint64_t wait_id = events::NoticeService::instance().push("info", msg, 0);
  if (wait_id != 0) {
    wait_notice_started_ms_[wait_id] = GetTickCount64();
  }
  return wait_id;
}

void MainWindow::hide_tracked_wait_notice(uint64_t wait_id) {
  if (wait_id == 0) return;
  uint64_t started_ms = 0;
  if (auto it = wait_notice_started_ms_.find(wait_id);
      it != wait_notice_started_ms_.end()) {
    started_ms = it->second;
    wait_notice_started_ms_.erase(it);
  }
  constexpr uint64_t kMinWaitDwellMs = 320;
  const uint64_t now = GetTickCount64();
  if (started_ms == 0 || now >= started_ms + kMinWaitDwellMs) {
    events::NoticeService::instance().hide(wait_id);
    return;
  }
  const uint64_t rem = (started_ms + kMinWaitDwellMs) - now;
  auto alive = alive_;
  std::thread([alive, wait_id, rem] {
    std::this_thread::sleep_for(std::chrono::milliseconds(rem));
    if (!alive || !alive->load(std::memory_order_acquire)) return;
    UiPostbox::instance().post([alive, wait_id] {
      if (!alive->load(std::memory_order_acquire)) return;
      events::NoticeService::instance().hide(wait_id);
    });
  }).detach();
}

void MainWindow::build_widget_tree() {
  // 生产剪贴板接 Win32（TextField 的 Ctrl+C/V/X 与右键编辑命令共用）
  TextField::set_clipboard_hooks(read_clipboard_text, write_clipboard_text);

  // root = Stack{ flow = Column{ titlebar(30), content = Stack{ scene, drawer } } }
  // 【T17-fix 根治】此前 root_ 直接挂 titlebar/scene——Stack::on_arrange
  //   铺满语义使两者 bounds 均为全窗（30px 只是 titlebar 绘制假设，scene
  //   从 y=0 盖过标题栏），真窗仅靠 WM 层 NCHITTEST 硬撑。现流式布局收进
  //   flow Column（titlebar measure 固定 30 + content flex 撑满剩余），
  //   Stack 只承担 overlay（抽屉覆盖）。
  // 【P0-drawer】抽屉挂载范围与 Mini 对齐：源码 Settings Drawer 在
  //   .layout-content（标题栏下方内容区，_layout.tsx:1254-1424 的
  //   position:absolute top:0/left:0/100%×100%），标题栏始终在它上方、
  //   保持可见可点（最小化/关闭/齿轮）。此前挂在 root_ = 整窗覆盖，
  //   把标题栏吃掉。现改为 content Stack 的第二个子层（全内容区覆盖）。
  auto flow = std::make_unique<Column>();
  flow_ = flow.get();
  root_.add_child(std::move(flow));

  // 悬停提示层（src: node_modules/@mui/material/Tooltip/Tooltip.js:331 enterDelay=100ms）
  auto tip = std::make_unique<TooltipHost>();
  tooltip_host_ = tip.get();
  tooltip_host_->bind_owner_hwnd(hwnd_);
  root_.add_child(std::move(tip));

  // 通知条（← components/layout/notice-manager.tsx:157-216，zIndex 1500）。
  // 【缺口修复】此前 SnackbarW 存在但从未挂载 → NoticeService.push 的所有提示
  //   （检查更新/内核更新/导入结果/错误）全部静默，用户侧表现为「没有任何反馈」。
  auto snack = std::make_unique<SnackbarW>();
  snackbar_ = snack.get();
  root_.add_child(std::move(snack));
  snackbar_->attach(events::NoticeService::instance());
  // 后端状态串 → 本地化 toast（← pages/_layout/utils/notification-handlers.ts）。
  // 【缺口修复】EventBus::notice_message 此前零订阅者 → 导入/校验/服务结果全静默。
  install_notice_handler();

  // 订阅刷新代理配置事件（择优选点/配置刷新时触发）：自动重新同步活跃节点
  // BUG-407: kRefreshClash 统一由 postbox_sub_refresh_ 处理，
  // 避免双重订阅导致每次 refresh_clash 执行两次 reload_nodes()。
  events::EventBus::instance().subscribe(
      events::names::kRefreshProxies,
      [this](const events::Payload&) {
        UiPostbox::instance().post([this] { reload_nodes(); });
      });
  // ← use-core-update.ts:39-62 listen('core-upgrade-progress')：
  //   驱动弹窗进度条，并在 done/error 时弹出 coreUpdateSuccess / coreUpdateFailed 通知
  bus_sub_core_upgrade_ = events::EventBus::instance().subscribe(
      events::names::kCoreUpgradeProgress,
      [this](const events::Payload& payload) {
        if (!payload.is_object()) return;
        const std::string st = payload.value("status", std::string());
        const int prog = payload.value("progress", 0);
        const std::string msg = payload.value("message", std::string());
        UiPostbox::instance().post([this, st, prog, msg] {
          if (!core_update_ || !core_update_->is_open()) return;
          core_update_->handle_progress_event(st, prog, msg);
          if (st == "done") {
            events::NoticeService::instance().push(
                "success", tr8("layout.notices.coreUpdateSuccess"));
          } else if (st == "error") {
            events::NoticeService::instance().push(
                "error", tr8("layout.notices.coreUpdateFailed", {{"message", msg}}));
          }
        });
      });
  bus_sub_open_guide_ = events::EventBus::instance().subscribe(
      "ui:open_guide",
      [this](const events::Payload&) {
        UiPostbox::instance().post([this] {
          if (settings_drawer_) settings_drawer_->set_open(false);
          if (first_run_) first_run_->open();
        });
      });

  auto tb = std::make_unique<TitleBar>(this);
  title_bar_ = tb.get();
  title_bar_->set_z_order(10);  // 使标题栏下投影叠在 content_ 之上，同时低于 SnackbarW(1500)
  title_bar_->set_version(utf::utf8_to_utf16(config::consts::kAppVersion));  // 单一事实源
  flow_->add_child(std::move(tb));

  auto content = std::make_unique<ContentStack>();
  content->flex_weight = 1;  // 标题栏下方剩余高度
  content_ = content.get();  // 布局时按底部流量区高度给抽屉留边
  Stack* content_raw = content.get();  // 【CRASHFIX】move 后 content 为空，
                                       // use-after-move 曾致 add_child(this=0) AV
  flow_->add_child(std::move(content));

  auto scene = std::make_unique<MainScene>();
  scene_ = scene.get();
  content_raw->add_child(std::move(scene));

  auto drawer = std::make_unique<scene::SettingsDrawer>();
  settings_drawer_ = drawer.get();
  drawer_mounted_ = true;
  content_->overlay = settings_drawer_;  // 只铺上栏，不遮底部流量条
  content_raw->add_child(std::move(drawer));
}

void MainWindow::build_scene() {
  // T13：场景装配——数据（production hooks）→ 快捷条/节点列表挂载 → 事件订阅
  node_data_ = std::make_unique<scene::NodeData>(scene::NodeData::production_hooks());

  node_data_sub_ = node_data_->subscribe([this] {
    sync_quick_bar(true);
  });

  auto qb = std::make_unique<scene::QuickBar>();
  quick_bar_ = qb.get();
  scene_->attach_quick_bar(std::move(qb));

  auto nl = std::make_unique<scene::NodeList>();
  node_list_ = nl.get();
  node_list_->bind(node_data_.get());
  // BUG-336/356：点击节点行 → 0ms 乐观高亮更新 + 异步 IPC/回滚 + selected 持久化
  // src: src/hooks/use-proxy-selection.ts:95
  node_list_->select_node = [this](const std::string& node) {
    switch_node_optimistic_async(node);
    return true;
  };
  // BUG-357：点击节点行右侧 65px 延迟热区 → 单节点探测（不切换活跃节点）
  // src: src/components/proxy/proxy-item.tsx:161
  node_list_->on_test_single = [this](const std::string& name) {
    test_single_node_async(name);
  };
  // BUG-360：过滤框 700ms 可取消自动测速防抖定时器接线
  // src: src/components/proxy/proxy-head.tsx:148
  node_list_->on_arm_filter_auto_test_timer = [this](uint32_t ms) {
    if (hwnd_) SetTimer(hwnd_, detail::kTimerFilterAutoTest, ms, nullptr);
  };
  node_list_->on_cancel_filter_auto_test_timer = [this] {
    if (hwnd_) KillTimer(hwnd_, detail::kTimerFilterAutoTest);
  };
  node_list_->on_batch_test_begin = [] { return begin_batch_test_notice(); };
  node_list_->on_batch_test_end =
      [](uint64_t wait_id, const scene::BatchTestSummary& sum) {
        finish_batch_test_notice(wait_id, sum);
      };
  // 自编排序切换提示已删除（Mini 无对应提示）
  node_list_->on_sort_changed = [](int) {};
  // src: src/pages/_layout/components/active-node-card.tsx:205 handleCycleNode：
  //   在当前过滤+排序后的可见列表中顺序轮换，自动跳过已确认超时（delay==0 || delay>=5000）的节点；
  //   若全部超时则回退至紧邻下一个节点，并走统一乐观切换 + selected 持久化链路（BUG-336/356）。
  quick_bar_->on_cycle_node = [this] {
    if (!node_data_) return;
    std::vector<std::string> candidates;
    if (node_list_) candidates = node_list_->visible_node_names();
    if (candidates.empty()) {
      for (const auto& r : node_data_->rows()) candidates.push_back(r.name);
    }
    if (candidates.size() < 2) return;
    const std::string cur_name = node_data_->now();
    size_t cur = candidates.size() - 1;  // 未匹配 → 从索引 0 开始尝试
    for (size_t i = 0; i < candidates.size(); ++i) {
      if (candidates[i] == cur_name) {
        cur = i;
        break;
      }
    }
    size_t chosen = (cur + 1) % candidates.size();
    for (size_t step = 1; step <= candidates.size(); ++step) {
      const size_t idx = (cur + step) % candidates.size();
      const int d = node_data_->row_delay(candidates[idx]);
      // src: src/pages/_layout/components/active-node-card.tsx:245
      if (d != 0 && d < scene::kNodeDelayMaxMs) {
        chosen = idx;
        break;
      }
    }
    if (candidates[chosen] == cur_name) return;
    switch_node_optimistic_async(candidates[chosen]);
  };

  // 点击延迟 Chip → 单点测速（src: src/pages/_layout/components/active-node-card.tsx:174）
  quick_bar_->on_test_delay = [this] {
    if (node_data_) test_single_node_async(node_data_->now());
  };

  // BUG-359：网络健康度持久胶囊同步 + 点击立即重测自愈（src: src/pages/_layout/components/active-node-card.tsx:105-147）
  quick_bar_->set_network_health(latest_network_health());
  net_health_sub_ = subscribe_network_health(
      [this](scene::NetworkHealthIssue issue) {
        UiPostbox::instance().post([this, issue] {
          if (quick_bar_) quick_bar_->set_network_health(issue);
        });
      });
  quick_bar_->on_self_heal_retest = [this] {
    if (!node_data_) return;
    auto latest = config::Config::instance().profiles().latest_arc();
    const std::string uid = latest ? latest->current.value_or("") : std::string();
    if (uid.empty()) return;
    if (self_heal_busy_.exchange(true)) {
      events::NoticeService::instance().push(
          "info", tr8("settings.mini.testingInProgress"));
      return;
    }
    if (quick_bar_) quick_bar_->set_self_heal_testing(true);
    std::vector<std::string> names;
    for (const auto& row : node_data_->rows()) {
      if (!scene::is_preset_proxy(row.name)) {
        names.push_back(row.name);
      }
    }
    auto snap = node_data_->snapshot_delays(names);
    if (!names.empty()) node_data_->set_delays_batch(names, -2);  // src: src/services/delay.ts:452
    const bool select_node =
        (latest_network_health() != scene::NetworkHealthIssue::Offline);
    std::thread([this, uid, select_node, snap = std::move(snap)]() mutable {
      // src: src/pages/_layout/components/active-node-card.tsx:121
      // BUG-396（active-node-card.tsx:155-161）：顶栏异常胶囊前台重测，
      // 选中节点时同步清理旧节点存量连接（离线仅测速不切换，不清理）。
      auto r = cmd::CommandService::instance().invoke(
          "trigger_auto_select",
          nlohmann::json::object({{"profile_uid", uid},
                                  {"sort_type", 0},
                                  {"select", select_node},
                                  {"clean_stale_connections", select_node}}));
      bool has_healthy = false;
      bool empty_result = false;
      std::string err;
      if (!r.is_ok()) {
        err = r.error().message;
      } else if (!r.value().is_array() || r.value().empty()) {
        empty_result = true;
      } else {
        for (const auto& item : r.value()) {
          if (item.is_array() && item.size() == 2 && item[1].is_number_integer()) {
            const int d = item[1].get<int>();
            // src: src/pages/_layout/components/active-node-card.tsx:127
            if (d > 0 && d < scene::kNodeDelayMaxMs) {
              has_healthy = true;
              break;
            }
          }
        }
      }
      UiPostbox::instance().post(
          [this, snap = std::move(snap), has_healthy, empty_result, err] {
            self_heal_busy_.store(false);
            if (quick_bar_) quick_bar_->set_self_heal_testing(false);
            if (node_data_) node_data_->restore_delays_for_still_pending(snap);
            reload_nodes();
            if (node_data_) node_data_->restore_delays_for_still_pending(snap);
            if (!err.empty()) {
              // src: src/pages/_layout/components/active-node-card.tsx:137
              if (err.find("AUTO_SELECT_BUSY") != std::string::npos ||
                  err.find("AUTO_SELECT_MANUAL_PREEMPTED") !=
                      std::string::npos) {
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
            if (has_healthy) {
              // src: src/pages/_layout/components/active-node-card.tsx:128
              set_latest_network_health(scene::NetworkHealthIssue::None);
              if (quick_bar_)
                quick_bar_->set_network_health(scene::NetworkHealthIssue::None);
            } else if (empty_result) {
              events::NoticeService::instance().push(
                  "error", tr8("settings.mini.testNotExecuted"));
            } else {
              // src: src/pages/_layout/components/active-node-card.tsx:132
              const auto cur_issue = latest_network_health();
              events::NoticeService::instance().push(
                  "error",
                  tr8(cur_issue == scene::NetworkHealthIssue::Offline
                          ? "settings.mini.networkOffline"
                          : "settings.mini.allNodesTimeout"));
            }
          });
    }).detach();
  };
  scene_->attach_node_list(std::move(nl));

  // 数据初始加载（内核配置就绪竞态：失败 2s 重试，最多 8 次）
  node_retry_left_ = 8;  // src: src/pages/_layout.tsx:351
  reload_nodes();

  // 延迟结果事件：局部刷新（BUG-344：区分实时探测 120s 与近期缓存复用 30s 封顶）
  // src: src/services/delay.ts:397
  postbox_sub_delay_ = UiPostbox::instance().subscribe_ui(
      events::names::kBackendDelayResults, [this](const nlohmann::json& p) {
        const bool from_probe =
            p.is_object() ? p.value("fromProbe", true) : true;
        node_data_->apply_delay_results(p, from_probe);
        quick_bar_->set_node_text(utf::utf8_to_utf16(
            node_data_->now().empty() ? "-" : node_data_->now()));
        quick_bar_->set_badge(node_data_->row_delay(node_data_->now()),
                              !node_data_->now().empty());
      });
  // 配置/代理刷新事件：全量重载
  postbox_sub_refresh_ = UiPostbox::instance().subscribe_ui(
      events::names::kRefreshClash, [this](const nlohmann::json&) {
        node_retry_left_ = 8;  // src: src/pages/_layout.tsx:351
        reload_nodes();
      });

  // T14：流量面板挂 traffic_slot；连接面板 Large 模式挂载（census：宽≤285
  // 时不挂载）
  auto tp = std::make_unique<scene::TrafficPane>();
  traffic_pane_ = tp.get();
  if (auto* slot = scene_->traffic_slot()) {
    slot->add_child(std::move(tp));  // 所有权移交场景树
  } else {
    traffic_pane_holder_ = std::move(tp);  // 无 slot 兜底持有
    traffic_pane_->set_visible(false);
  }

  conn_data_ = std::make_unique<scene::ConnectionData>(300);  // src: src/hooks/use-connection-data.ts:24
  conn_data_->on_resync = [this] {
    CWLOG_WARN("conn resync：等待下一个 snapshot 帧（WS 重连）");
    if (ws_sub_conn_ != 0 && ws_) {
      ws_->unsubscribe(ws_sub_conn_);
      ws_sub_conn_ = 0;
      update_conn_ws_state();
    }
  };

  build_ws_pipes();

  // QuickBar 齿轮/图钉接线（Mini：橙方块 gear=开抽屉，pin=置顶并持久化 enable_always_on_top，BUG-340）
  if (quick_bar_) {
    quick_bar_->on_open_settings = [this] {
      CWLOG_INFO("[GEAR] on_click → toggle drawer");
      if (settings_drawer_) settings_drawer_->set_open(!settings_drawer_->is_open());
    };
    quick_bar_->on_toggle_pin = [this] {
      // src: src/pages/_layout.tsx:446
      const bool target_pin = !pinned_;
      pinned_ = target_pin;
      if (hwnd_) {
        SetWindowPos(hwnd_, target_pin ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0,
                     0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      }
      if (quick_bar_) quick_bar_->set_pinned(target_pin);
      std::thread([this, target_pin] {
        auto r = cmd::CommandService::instance().invoke(
            "patch_verge_config",
            nlohmann::json::object({{"enable_always_on_top", target_pin}}));
        const bool ok = r.is_ok();
        const std::string err = ok ? std::string() : r.error().message;
        UiPostbox::instance().post([this, target_pin, ok, err] {
          if (!ok) {
            pinned_ = !target_pin;
            if (hwnd_) {
              SetWindowPos(hwnd_, pinned_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0,
                           0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            if (quick_bar_) quick_bar_->set_pinned(pinned_);
            events::NoticeService::instance().push("error", err);
          }
        });
      }).detach();
    };
  }

  // ---------- T17/T20：设置抽屉命令链接线 ----------
  if (settings_drawer_) {
    if (auto* cp = settings_drawer_->conn_panel()) {
      cp->bind(conn_data_.get());
      cp->on_close_one = [](const std::string& id) {
        std::thread([id] {
          auto r = cmd::CommandService::instance().invoke(
              "close_connection", nlohmann::json::object({{"id", id}}));
          const bool ok = r.is_ok();
          const std::string err = ok ? std::string() : r.error().message;
          UiPostbox::instance().post([err] {
            // 自编断开单条成功提示已删除（Mini 无对应提示），仅透传错误
            if (!err.empty()) events::NoticeService::instance().push("error", err);
          });
        }).detach();
      };
      cp->on_close_all = [] {
        std::thread([] {
          auto r = cmd::CommandService::instance().invoke(
              "close_all_connections", nlohmann::json::object());
          const bool ok = r.is_ok();
          const std::string err = ok ? std::string() : r.error().message;
          UiPostbox::instance().post([ok, err] {
            dispatch_close_all_connections_notice(ok, err);
          });
        }).detach();
      };
      cp->on_clear_history = [this] {
        if (conn_data_) conn_data_->clear_closed();
        // 自编清空历史提示已删除（Mini 无对应提示）
      };
    }
    settings_drawer_->on_open_changed = [this](bool open) {
      // src: src/pages/_layout.tsx:523,1132「!drawerOpen」→ 抽屉打开时快捷栏与节点列表均不渲染
      if (quick_bar_) quick_bar_->set_visible(!open);
      if (node_list_) {
        node_list_->set_visible(!open);
        node_list_->set_a11y_hidden(open);
      }
      update_conn_ws_state();
    };
    // patch_verge 键 → patch_verge_config；clash 键 → patch_clash_config
    settings_drawer_->on_patch_verge =
        [this](const std::string& k, const nlohmann::json& v) {
          const std::string success_msg = resolve_verge_patch_success_message(k, v);
          if (k == "clash_mini_control_skin") {
            // 皮肤偏好已由 SettingsDrawer::select_skin 落盘至 clashwin-skin.json；
            // 自编皮肤切换提示已删除（Mini 无对应提示）。
            return;
          }
          const bool is_clash = (k == "clash_allow_lan" || k == "clash_ipv6");
          const bool is_basic_toggle =
              (k == "enable_auto_launch" || k == "enable_silent_start" ||
               k == "enable_tun_sniffer");
          const bool is_auto_launch = (k == "enable_auto_launch");
          const bool want_auto_launch =
              (is_auto_launch && v.is_boolean()) ? v.get<bool>() : false;
          const bool is_rule = (k == "rule_fallback");
          const char* info_key =
              (is_clash || is_basic_toggle)
                  ? "layout.notices.adjusting"
                  : (is_rule ? "layout.notices.adjustingProxyMode" : nullptr);
          const std::string cmd =
              is_clash ? "patch_clash_config" : "patch_verge_config";
          nlohmann::json args;
          if (is_clash) {
            const std::string field =
                (k == "clash_allow_lan") ? "allow-lan" : "ipv6";
            args = nlohmann::json::object({{field, v}});
          } else {
            args = nlohmann::json::object({{k, v}});
          }
          const uint64_t wait_id =
              info_key ? push_tracked_wait_notice(tr8(info_key)) : 0;

          // ---- 后台线程：只做阻塞式 svc.invoke（UI 线程不再被 wait_for 拖住）----
          std::thread([this, k, cmd, args, wait_id, success_msg, is_clash, is_rule,
                       is_auto_launch, want_auto_launch] {
            std::optional<cmd::LongOperationGuard> long_guard;
            if (is_clash || is_rule) long_guard.emplace();
            auto r = cmd::CommandService::instance().invoke(cmd, args);
            if (r.is_ok() && is_rule) {
              // ← _layout.tsx:405-413 切分流同步切 clash mode='rule'，合并在同一后台事务中
              cmd::CommandService::instance().invoke(
                  "patch_clash_mode", nlohmann::json::object({{"mode", "rule"}}));
            }
            const bool ok = r.is_ok();
            bool auto_launch_mismatch = false;
            if (ok && is_auto_launch) {
              // ← basic-settings-card.tsx:221-248 回查注册表真实自启状态，防被安全软件静默拦截
              auto st = core::AutostartManager::instance().get_launch_status();
              if (!st.is_ok() || st.value() != want_auto_launch) {
                auto_launch_mismatch = true;
              }
            }
            const std::string err = ok ? std::string() : r.error().message;
            CWLOG_INFO(std::string("[PATCH_VERGE] ") + k + "=" + args.dump() +
                       (ok ? " ok" : " FAIL " + err));
            // ---- 收尾统一回 UI 线程（避免 NoticeService 变更回调跨线程触碰 SnackbarW）----
            UiPostbox::instance().post(
                [this, wait_id, success_msg, ok, err, is_auto_launch,
                 want_auto_launch, auto_launch_mismatch] {
                  auto& notices = events::NoticeService::instance();
                  if (wait_id) hide_tracked_wait_notice(wait_id);  // finally hideNotice
                  if (!settings_drawer_) return;       // 同 on_takeover_change 生命周期守卫
                  if (ok) {
                    if (is_auto_launch) {
                      if (auto_launch_mismatch) {
                        notices.push("warning",
                                     tr8("settings.mini.autoLaunchVerifyFailed"));
                        resync_settings_ui();
                      } else {
                        notices.push(
                            "success",
                            tr8(want_auto_launch ? "settings.mini.settingEnabled"
                                                 : "settings.mini.settingDisabled"));
                      }
                    } else if (!success_msg.empty()) {
                      notices.push("success", success_msg);
                    }
                  } else {
                    notices.push("error", err);
                    resync_settings_ui();  // ★ ❌5 失败回滚到权威值（Mini mutateInfo）
                  }
                });
          }).detach();
        };
    // 流量接管模式切换 ← src/pages/_layout/hooks/use-proxy-mode-actions.ts:92-140 handleTakeoverModeChange
    settings_drawer_->on_takeover_change =
        [this](const std::string& mode) -> bool {
          std::string prev_mode = "MAN";
          try {
            const auto& v = *config::Config::instance().verge().latest_arc();
            if (v.enable_tun_mode.value_or(false)) prev_mode = "TUN";
            else if (v.enable_system_proxy.value_or(false)) prev_mode = "SYS";
          } catch (...) {}

          const uint64_t wait_id =
              push_tracked_wait_notice(tr8("layout.notices.adjusting"));

          std::thread([this, mode, prev_mode, wait_id] {
            cmd::LongOperationGuard long_guard;
            auto& svc = cmd::CommandService::instance();

            if (mode == "MAN") {
              if (prev_mode == "TUN") {
                svc.invoke("stop_service");
              }
              auto r = svc.invoke(
                  "patch_verge_config",
                  nlohmann::json::object({{"enable_system_proxy", false},
                                          {"enable_tun_mode", false}}));
              const bool ok = r.is_ok();
              if (ok) {
                // ← src/pages/_layout/hooks/use-proxy-mode-actions.ts:128-132 手动模式开启 auto_close_connection 时断开存量连接
                bool auto_close = false;
                try {
                  auto latest = config::Config::instance().verge().latest_arc();
                  if (latest) {
                    auto_close = latest->auto_close_connection.value_or(false);
                  }
                } catch (...) {}
                if (auto_close) {
                  svc.invoke("close_all_connections", nlohmann::json::object());
                }
              }
              const std::string err = ok ? std::string() : r.error().message;
              UiPostbox::instance().post([this, wait_id, ok, err, prev_mode] {
                auto& nots = events::NoticeService::instance();
                hide_tracked_wait_notice(wait_id);
                if (ok) {
                  nots.push("success", tr8("layout.notices.switchedToManualMode"));
                  if (settings_drawer_) settings_drawer_->sync_takeover(false, false);
                } else {
                  nots.push("error", err);
                  if (settings_drawer_)
                    settings_drawer_->sync_takeover(prev_mode == "SYS", prev_mode == "TUN");
                }
              });
              return;
            }

            if (mode == "SYS") {
              if (prev_mode == "TUN") {
                svc.invoke("stop_service");
              }
              auto r = svc.invoke(
                  "patch_verge_config",
                  nlohmann::json::object({{"enable_system_proxy", true},
                                          {"enable_tun_mode", false}}));
              const bool ok = r.is_ok();
              const std::string err = ok ? std::string() : r.error().message;
              UiPostbox::instance().post([this, wait_id, ok, err, prev_mode] {
                auto& nots = events::NoticeService::instance();
                hide_tracked_wait_notice(wait_id);
                if (ok) {
                  nots.push("success", tr8("layout.notices.systemProxyEnabled"));
                  if (settings_drawer_) settings_drawer_->sync_takeover(true, false);
                } else {
                  nots.push("error", err);
                  if (settings_drawer_)
                    settings_drawer_->sync_takeover(prev_mode == "SYS", prev_mode == "TUN");
                }
              });
              return;
            }

            // mode == "TUN"
            bool is_admin = false;
            {
              auto ar = svc.invoke("app_is_admin");
              if (ar.is_ok() && ar.value().is_boolean())
                is_admin = ar.value().get<bool>();
            }
            const bool service_ok = svc.invoke("is_service_available").is_ok();
            if (!(is_admin || service_ok)) {
              auto inst = svc.invoke("install_service");
              if (!inst.is_ok()) {
                UiPostbox::instance().post([this, wait_id, prev_mode] {
                  auto& nots = events::NoticeService::instance();
                  hide_tracked_wait_notice(wait_id);
                  nots.push("error", tr8("layout.notices.tunServiceConfigFailed"));
                  if (settings_drawer_)
                    settings_drawer_->sync_takeover(prev_mode == "SYS", prev_mode == "TUN");
                });
                return;
              }
            }
            auto r = svc.invoke(
                "patch_verge_config",
                nlohmann::json::object({{"enable_system_proxy", false},
                                        {"enable_tun_mode", true}}));
            const bool ok = r.is_ok();
            const std::string err = ok ? std::string() : r.error().message;
            UiPostbox::instance().post([this, wait_id, ok, err, prev_mode] {
              auto& nots = events::NoticeService::instance();
              hide_tracked_wait_notice(wait_id);
              if (ok) {
                nots.push("success", tr8("layout.notices.tunModeEnabled"));
                if (settings_drawer_) settings_drawer_->sync_takeover(false, true);
              } else {
                nots.push("error", err);
                if (settings_drawer_)
                  settings_drawer_->sync_takeover(prev_mode == "SYS", prev_mode == "TUN");
              }
            });
          }).detach();

          return true;
        };
    settings_drawer_->on_patch_clash_mode = [](const std::string&) {
      // 生产主窗口中已由 on_patch_verge("rule_fallback") 在同一后台线程内串行执行，
      // 避免 UI 线程同步阻塞并消除与 patch_verge_config 的并发竞态。
    };
    // Mixed Port 保存 ← use-minimal-settings.ts:87-107 handleSavePort
    settings_drawer_->on_save_port = [this](int port) -> bool {
      auto& cfg = config::Config::instance();
      const int verge_port =
          static_cast<int>(cfg.verge().latest_arc()->verge_mixed_port.value_or(0));
      const int core_port =
          static_cast<int>(cfg.clash().latest_arc()->get_mixed_port());
      if (port == verge_port && port == core_port) return true;  // 无变更，静默
      const uint64_t wait_id =
          push_tracked_wait_notice(tr8("layout.notices.adjusting"));
      std::thread([this, port, wait_id] {
        cmd::LongOperationGuard long_guard;
        auto& svc = cmd::CommandService::instance();
        auto inuse =
            svc.invoke("is_port_in_use", nlohmann::json::object({{"port", port}}));
        if (inuse.is_ok() && inuse.value().is_boolean() && inuse.value().get<bool>()) {
          UiPostbox::instance().post([this, port, wait_id] {
            auto& notices = events::NoticeService::instance();
            hide_tracked_wait_notice(wait_id);
            notices.push(
                "error", tr8("settings.modals.clashPort.messages.portInUse",
                             {{"port", std::to_string(port)}}));
            if (settings_drawer_ && settings_drawer_->basic_card() &&
                settings_drawer_->basic_card()->port_current_ > 0) {
              settings_drawer_->basic_card()->port_field_->set_text(
                  std::to_wstring(settings_drawer_->basic_card()->port_current_));
              settings_drawer_->basic_card()->port_field_->invalidate();
            }
          });
          return;
        }
        auto a = svc.invoke("patch_clash_config",
                            nlohmann::json::object({{"mixed-port", port}}));
        auto b = svc.invoke("patch_verge_config",
                            nlohmann::json::object({{"verge_mixed_port", port}}));
        const bool ok = a.is_ok() && b.is_ok();
        const std::string err =
            ok ? std::string() : (a.is_ok() ? b.error().message : a.error().message);
        UiPostbox::instance().post([this, port, wait_id, ok, err] {
          auto& notices = events::NoticeService::instance();
          hide_tracked_wait_notice(wait_id);
          if (ok) {
            if (settings_drawer_ && settings_drawer_->basic_card()) {
              settings_drawer_->basic_card()->port_current_ = port;
            }
            notices.push("success", tr8("settings.mini.portSaved"));
          } else {
            notices.push("error", err);
            if (settings_drawer_ && settings_drawer_->basic_card() &&
                settings_drawer_->basic_card()->port_current_ > 0) {
              settings_drawer_->basic_card()->port_field_->set_text(
                  std::to_wstring(settings_drawer_->basic_card()->port_current_));
              settings_drawer_->basic_card()->port_field_->invalidate();
            }
          }
        });
      }).detach();
      return true;
    };
    settings_drawer_->on_import_profile =
        [this](const std::string& url, const std::string& name) {
          nlohmann::json args = nlohmann::json::object({{"url", url}});
          if (!name.empty()) args["name"] = name;
          bool used_proxy = false;
          auto r = cmd::CommandService::instance().invoke("import_profile", args);
          if (!r.is_ok()) {
            args["option"] = nlohmann::json::object(
                {{"with_proxy", false}, {"self_proxy", true}});
            r = cmd::CommandService::instance().invoke("import_profile", args);
            used_proxy = true;
          }
          if (r.is_ok()) {
            events::NoticeService::instance().push(
                "success",
                tr8(used_proxy
                        ? "shared.feedback.notifications.importWithClashProxy"
                        : "shared.feedback.notifications.importSuccess"));
            std::string new_uid;
            if (r.value().is_object()) {
              new_uid = r.value().value("uid", std::string());
            }
            if (!new_uid.empty()) {
              auto cur = config::Config::instance().profiles().latest_arc()->current;
              if (!cur || *cur != new_uid) {
                cmd::CommandService::instance().invoke(
                    "patch_profiles_config",
                    nlohmann::json::object({{"current", new_uid}}));
              }
              trigger_auto_select_and_refresh(new_uid);
            }
            reload_nodes();
            return true;
          }
          events::NoticeService::instance().push(
              "error", tr8("profiles.page.feedback.notifications.importFail") +
                           " " + r.error().message);
          return false;
        };
    settings_drawer_->on_import_profile_async =
        [this](const std::string& url, const std::string& name,
               std::function<void(bool ok)> done) {
          run_import_profile_async(
              url, name,
              [done = std::move(done)](bool ok, const std::string&) {
                if (done) done(ok);
              });
        };
    settings_drawer_->on_select_profile = [this](const std::string& uid) {
      // ← src/pages/_layout/hooks/use-profile-actions.ts:291-312：同当前订阅无操作；切订阅先清缓存，后台切换并弹通知
      if (auto p = config::Config::instance().profiles().latest_arc();
          p && p->current && *p->current == uid) {
        return;
      }
      if (node_data_) node_data_->clear_delay_cache();
      // 自编"正在切换订阅"常驻提示已删除（Mini 无对应提示）
      const uint64_t wait_id = 0;
      std::thread([this, uid, wait_id] {
        cmd::LongOperationGuard long_guard;
        auto r = cmd::CommandService::instance().invoke(
            "patch_profiles_config", nlohmann::json::object({{"current", uid}}));
        const bool ok = r.is_ok();
        const std::string err = ok ? std::string() : r.error().message;
        if (ok) {
          // BUG-340：手动切订阅成功后 best-effort 清理旧订阅遗留连接（src: src/pages/_layout/hooks/use-profile-actions.ts:302）
          auto cr = cmd::CommandService::instance().invoke(
              "close_all_connections", nlohmann::json::object());
          if (!cr.is_ok()) {
            CWLOG_WARN("close_all_connections after profile switch failed: " +
                       cr.error().message);
          }
        }
        UiPostbox::instance().post([this, uid, wait_id, ok, err] {
          events::NoticeService::instance().hide(wait_id);
          if (ok) {
            events::EventBus::notice_message("profile_switched::ok", "");
            if (settings_drawer_ && settings_drawer_->profile_card()) {
              settings_drawer_->profile_card()->reload_profiles();
              settings_drawer_->invalidate();
            }
            reload_nodes();
            trigger_auto_select_and_refresh(uid);
          } else {
            events::EventBus::notice_message("update_failed", err);
          }
        });
      }).detach();
    };
    settings_drawer_->on_update_profile = [this](const std::string& uid) {
      // ← src/pages/_layout/hooks/use-profile-actions.ts:314-337：常驻 info「正在更新订阅」→ 后台更新 → 收尾隐藏并通知
      const uint64_t wait_id =
          push_tracked_wait_notice(tr8("layout.notices.updatingSubscription"));
      std::thread([this, uid, wait_id] {
        cmd::LongOperationGuard long_guard;
        auto r = cmd::CommandService::instance().invoke(
            "update_profile", nlohmann::json::object({{"index", uid}}));
        const bool ok = r.is_ok();
        const std::string err = ok ? std::string() : r.error().message;
        UiPostbox::instance().post([this, uid, wait_id, ok, err] {
          hide_tracked_wait_notice(wait_id);
          if (ok) {
            events::EventBus::notice_message("update_subscription::ok", "");
            if (settings_drawer_ && settings_drawer_->profile_card()) {
              settings_drawer_->profile_card()->reload_profiles();
              settings_drawer_->invalidate();
            }
            reload_nodes();
            trigger_auto_select_and_refresh(uid);
          } else {
            // ← use-profile-actions.ts handleUpdateProfile（BUG-417）：三级回退全失败的
            //   带前缀错误映射为本地化 update_failed_even_with_clash 提示，其余走通用失败提示
            static constexpr const char* kProxyFailedPrefix = "update_failed_even_with_clash:";
            CWLOG_ERROR("on_update_profile failed: " + err);
            if (err.rfind(kProxyFailedPrefix, 0) == 0) {
              std::string detail = err.substr(std::char_traits<char>::length(kProxyFailedPrefix));
              while (!detail.empty() && detail.front() == ' ') detail.erase(detail.begin());
              events::EventBus::notice_message("update_failed_even_with_clash", detail);
            } else {
              events::EventBus::notice_message("update_failed", err);
            }
          }
        });
      }).detach();
    };
    settings_drawer_->on_delete_profile = [this](const std::string& uid) {
      // ← src/pages/_layout/hooks/use-profile-actions.ts:348-359：后台删除 → 成功 common.deleteSuccess / 失败原文
      std::thread([this, uid] {
        cmd::LongOperationGuard long_guard;
        auto r = cmd::CommandService::instance().invoke(
            "delete_profile", nlohmann::json::object({{"index", uid}}));
        const bool ok = r.is_ok();
        const std::string err = ok ? std::string() : r.error().message;
        UiPostbox::instance().post([this, ok, err] {
          if (ok) {
            events::EventBus::notice_message("profile_deleted::ok", "");
            if (settings_drawer_ && settings_drawer_->profile_card()) {
              settings_drawer_->profile_card()->reload_profiles();
              if (settings_drawer_->bounds().w > 0)
                settings_drawer_->layout_tree(settings_drawer_->bounds());
              settings_drawer_->invalidate();
            }
            reload_nodes();
          } else {
            events::EventBus::notice_message("update_failed", err);
          }
        });
      }).detach();
    };
    // 编辑订阅保存 → patch_profile（← src/pages/_layout/hooks/use-profile-actions.ts:412-435；payload 见
    // src/cmd/cmd_profile.cpp:339-359）
    settings_drawer_->on_patch_profile = [this](const std::string& uid,
                                                const std::string& name,
                                                const std::string& url, int interval) {
      // patch_item 是 option 整体替换（src/config/profiles.cpp:311），
      // 必须带上原有 option 字段，否则会清掉 user_agent / with_proxy 等。
      nlohmann::json opt = nlohmann::json::object();
      if (const config::PrfItem* cur =
              config::Config::instance().profiles().data_arc()->get_item(uid)) {
        if (cur->option) {
          const auto& o = *cur->option;
          if (o.user_agent) opt["user_agent"] = *o.user_agent;
          if (o.with_proxy) opt["with_proxy"] = *o.with_proxy;
          if (o.self_proxy) opt["self_proxy"] = *o.self_proxy;
          if (o.timeout_seconds) opt["timeout_seconds"] = *o.timeout_seconds;
          if (o.danger_accept_invalid_certs)
            opt["danger_accept_invalid_certs"] = *o.danger_accept_invalid_certs;
          if (o.allow_auto_update) opt["allow_auto_update"] = *o.allow_auto_update;
          if (o.merge) opt["merge"] = *o.merge;
          if (o.script) opt["script"] = *o.script;
          if (o.rules) opt["rules"] = *o.rules;
          if (o.proxies) opt["proxies"] = *o.proxies;
          if (o.groups) opt["groups"] = *o.groups;
        }
      }
      opt["update_interval"] = interval;
      nlohmann::json patch = {{"name", name}, {"url", url}, {"option", opt}};
      std::thread([this, uid, patch] {
        cmd::LongOperationGuard long_guard;
        auto r = cmd::CommandService::instance().invoke(
            "patch_profile",
            nlohmann::json::object({{"index", uid}, {"profile", patch}}));
        const bool ok = r.is_ok();
        const std::string err = ok ? std::string() : r.error().message;
        UiPostbox::instance().post([this, ok, err] {
          if (ok) {
            events::NoticeService::instance().push(
                "success", tr8("layout.notices.profileSaved"));
            if (settings_drawer_ && settings_drawer_->profile_card()) {
              settings_drawer_->profile_card()->reload_profiles();
              if (settings_drawer_->bounds().w > 0)
                settings_drawer_->layout_tree(settings_drawer_->bounds());
              settings_drawer_->invalidate();
            }
          } else {
            events::NoticeService::instance().push("error", err);
          }
        });
      }).detach();
      return true;
    };
    auto start_log_stream = [this] {
      if (ws_sub_logs_ != 0 || !ws_) return;
      ws_sub_logs_ = ws_->subscribe_logs(
          mihomo::LogLevel::Info, [this](const std::string& payload) {
            UiPostbox::instance().post([this, payload] {
              if (!log_viewer_) return;
              try {
                auto j = nlohmann::json::parse(payload);
                std::string type = j.value("type", "info");
                std::string msg = j.value("payload", payload);
                log_viewer_->push_log(type, msg);
              } catch (...) {
                log_viewer_->push_log_line(payload);
              }
            });
          });
    };
    auto stop_log_stream = [this] {
      if (ws_sub_logs_ != 0 && ws_) {
        ws_->unsubscribe(ws_sub_logs_);
        ws_sub_logs_ = 0;
      }
    };
    settings_drawer_->on_open_logs = [this, start_log_stream] {
      // ← Mini 调试日志：应用内 LogViewerModal（pages/logs.tsx），
      //   首开时回填 logs/clashwin.log 末尾并抓取 Mihomo 内核日志与实时 WS 流
      if (!log_viewer_) return;
      if (log_viewer_->log_count() == 0 && log_viewer_->incoming_count() == 0) {
        clashwin::Logger::instance().flush();
        std::wstring p1 = clashwin::util::dirs::exe_dir() + L"\\logs\\latest.log";
        std::wstring p2 = clashwin::util::dirs::app_latest_log();
        std::wstring path = fs::exists(p2) ? p2 : p1;
        std::ifstream in(fs::path(path), std::ios::in | std::ios::binary);
        // 豁免: Mini 无对应物 — 使用 fs::path 打开宽字符日志路径（出处: 无）
        if (in) {
          std::deque<std::string> tail;
          std::string line;
          while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) tail.push_back(std::move(line));
            if (tail.size() > scene::LogViewerModal::kMaxLogLines) tail.pop_front();
          }
          for (const auto& l : tail) log_viewer_->push_log_line(l);
        }
        // 拉取 Mihomo 内核运行日志（调用 get_clash_logs 命令）
        auto kr = cmd::CommandService::instance().invoke("get_clash_logs", {});
        if (kr.is_ok() && kr.value().is_array()) {
          for (const auto& item : kr.value()) {
            if (item.is_string()) {
              log_viewer_->push_log_line(item.get<std::string>());
            } else if (item.is_object()) {
              log_viewer_->push_log(item.value("type", "info"), item.value("payload", item.dump()));
            }
          }
        }
        if (log_viewer_->log_count() == 0 && log_viewer_->incoming_count() == 0) {
          log_viewer_->push_log("info", "Clash Win runtime logger active: " + utf::utf16_to_utf8(path));
        }
      }
      // 订阅实时内核日志流（src: use-log-data.ts:25-45，仅非暂停态自动连接）
      if (!log_viewer_->is_paused()) start_log_stream();
      log_viewer_->flush_incoming();
      log_viewer_->open();
    };
    // 日志弹窗装载（覆盖抽屉，与 Mini 的独立 logs modal 一致）
    {
      auto lv = std::make_unique<scene::LogViewerModal>();
      log_viewer_ = lv.get();
      lv->set_z_order(1001);
      lv->on_dismiss = [this, stop_log_stream] {
        stop_log_stream();
        root_.invalidate();
      };
      lv->on_pause_changed = [this, start_log_stream, stop_log_stream](bool paused) {
        if (paused) {
          stop_log_stream();
        } else if (log_viewer_ && log_viewer_->is_open()) {
          start_log_stream();
        }
      };
      root_.add_child(std::move(lv));
    }
    // 添加网址弹窗（Mini：AddUrlDialog 挂在抽屉右列，_layout.tsx:1485-1488）
    {
      auto au = std::make_unique<scene::AddUrlDialog>();
      add_url_ = au.get();
      au->set_z_order(960);  // ctor 已设；显式声明与 root_ 其它弹窗层序
      auto au_menu = std::make_unique<ContextMenu>();
      au_menu->set_z_order(965);
      add_url_->set_context_menu(au_menu.get());
      root_.add_child(std::move(au));
      root_.add_child(std::move(au_menu));
    }
    // 客户端/内核更新弹窗（Mini：_layout.tsx:1948/1956，帮助菜单点击才弹）
    {
      auto cu = std::make_unique<scene::ClientUpdateModal>();
      client_update_ = cu.get();
      cu->set_z_order(1001);
      root_.add_child(std::move(cu));
    }
    {
      auto co = std::make_unique<scene::CoreUpdateModal>();
      core_update_ = co.get();
      co->set_z_order(1001);
      root_.add_child(std::move(co));
    }
    // 检查软件更新（src: use-client-update.ts:32-54）：有新版才开弹窗。
    // F-D05：检查期间禁用「检查更新」菜单项（对齐 Mini use-client-update.ts 的 checking 状态）。
    settings_drawer_->on_check_app_update = [this] {
      if (!client_update_) return;
      settings_drawer_->set_app_update_checking(true);
      std::thread([this] {
        auto r = cmd::CommandService::instance().invoke(
            "check_app_update", nlohmann::json::object());
        if (!r.is_ok()) {
          // ← use-client-update.ts:46-50 clientCheckFailed
          CWLOG_ERROR("on_check_app_update failed: " + r.error().message);
          events::NoticeService::instance().push(
              "error", tr8("layout.notices.clientCheckFailed", {{"message", r.error().message}}));
          UiPostbox::instance().post([this] { settings_drawer_->set_app_update_checking(false); });
          return;
        }
        const auto& v = r.value();
        if (!v.value("has_update", false)) {
          // ← use-client-update.ts:43-45 clientUpToDate
          events::NoticeService::instance().push("info", tr8("layout.notices.clientUpToDate"));
          UiPostbox::instance().post([this] { settings_drawer_->set_app_update_checking(false); });
          return;
        }
        const std::string dl_url = v.value("download_url", std::string());
        const std::string dl_sha = v.value("sha256", std::string());
        const std::string dl_sig = v.value("signature", std::string());
        UiPostbox::instance().post([this, v, dl_url, dl_sha, dl_sig] {
          if (!client_update_) return;
          pending_client_sha256_ = dl_sha;
          pending_client_signature_ = dl_sig;
          settings_drawer_->set_app_update_checking(false);
          client_update_->open(v.value("current_version", std::string()),
                               v.value("latest_version", std::string()),
                               v.value("notes", std::string()), dl_url);
        });
      }).detach();
    };
    // 检查内核更新（src: use-core-update.ts:64-86）：版本不同才开弹窗。
    // F-D05：检查期间禁用「检查内核更新」菜单项（对齐 Mini use-core-update.ts 的 checking 状态）。
    settings_drawer_->on_check_core_update = [this] {
      if (!core_update_) return;
      settings_drawer_->set_core_update_checking(true);
      std::thread([this] {
        std::string cur;
        auto m = cmd::mihomo();
        if (m.is_ok()) {
          auto v = m.value()->get_version(300);
          if (v.is_ok()) cur = v.value().version;
        }
        auto r = cmd::CommandService::instance().invoke(
            "check_core_update", nlohmann::json::object());
        if (!r.is_ok()) {
          // ← use-core-update.ts:78-82 coreCheckFailed
          events::NoticeService::instance().push(
              "error", tr8("layout.notices.coreCheckFailed", {{"message", r.error().message}}));
          UiPostbox::instance().post([this] { settings_drawer_->set_core_update_checking(false); });
          return;
        }
        const std::string latest = r.value().value("tag_name", std::string());
        auto strip = [](std::string s) {
          if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
          return s;
        };
        if (latest.empty() || strip(cur) == strip(latest)) {
          // ← use-core-update.ts:69-71 coreUpToDate
          events::NoticeService::instance().push("info", tr8("layout.notices.coreUpToDate"));
          UiPostbox::instance().post([this] { settings_drawer_->set_core_update_checking(false); });
          return;
        }
        const std::string rel_json = r.value().dump();
        UiPostbox::instance().post([this, cur, latest, rel_json] {
          if (!core_update_) return;
          pending_core_release_json_ = rel_json;
          settings_drawer_->set_core_update_checking(false);
          core_update_->open(cur, latest);
        });
      }).detach();
    };
    // 客户端更新包下载+安装（src: use-client-update.ts:56-125 downloadAndInstall）。
    if (client_update_) {
      client_update_->on_start_update = [this] {
        if (!client_update_ || client_update_->download_url().empty()) return;
        const std::string url = client_update_->download_url();
        const std::string sha = pending_client_sha256_;
        const std::string sig = pending_client_signature_;
        std::thread([this, url, sha, sig] {
          auto r = cmd::CommandService::instance().invoke(
              "download_app_update",
              nlohmann::json::object({{"url", url}, {"sha256", sha}, {"signature", sig}}));
          if (!r.is_ok()) {
            CWLOG_ERROR("client download_app_update failed: " + r.error().message);
            const std::string msg =
                tr8("layout.notices.clientUpdateFailed", {{"message", r.error().message}});
            // ← use-client-update.ts:114-123 clientUpdateFailed（进度行 + 通知双显）
            events::NoticeService::instance().push("error", msg);
            UiPostbox::instance().post([this, msg] {
              if (client_update_) client_update_->handle_progress_event("error", 0, msg);
            });
            return;
          }
          // ← updater.rs:843-854：启动安装器（/UPDATE /ARGS）后 exit(0)
          const std::wstring installer =
              utf::utf8_to_utf16(r.value().value("path", std::string()));
          auto ir = update::ClientUpdater::instance().install_app_update(installer);
          if (!ir.is_ok()) {
            CWLOG_ERROR("install_app_update failed: " + ir.error().message);
            events::NoticeService::instance().push(
                "error", tr8("layout.notices.clientUpdateFailed", {{"message", ir.error().message}}));
            return;
          }
          // Mini 同款：立即退出（updater.rs:853 std::process::exit(0)）
          std::exit(0);
        }).detach();
      };
    }
    // 启动内核热升级（src: use-core-update.ts:88-120 start_core_upgrade）：
    //   把检查时拿到的 release 原样回传，避免二次请求 GitHub API。
    if (core_update_) {
      core_update_->on_start_upgrade = [this] {
        if (!core_update_) return;
        const std::string rel_json = pending_core_release_json_;
        std::thread([this, rel_json] {
          nlohmann::json args = nlohmann::json::object();
          try {
            if (!rel_json.empty()) args = nlohmann::json::parse(rel_json);
          } catch (...) {
          }
          auto r = cmd::CommandService::instance().invoke("start_core_upgrade", args);
          if (!r.is_ok()) {
            const std::string msg = "启动失败: " + r.error().message;
            // ← use-core-update.ts:107-118 coreStartFailed（进度行）+
            //   coreUpgradeStartFailed（通知）
            events::NoticeService::instance().push(
                "error",
                tr8("layout.notices.coreUpgradeStartFailed", {{"message", r.error().message}}));
            UiPostbox::instance().post([this, msg] {
              if (core_update_) core_update_->handle_progress_event("error", 0, msg);
            });
          }
        }).detach();
      };
    }
    // 首次运行向导（Mini：无真实订阅时 500ms 后弹出，_layout.tsx:347-355）
    {
      auto fr = std::make_unique<scene::FirstRunGuideDialog>();
      first_run_ = fr.get();
      fr->set_z_order(980);
      fr->on_import_async =
          [this](const std::string& url,
                 std::function<void(bool ok, const std::string& err)> done) {
            run_import_profile_async(url, "", std::move(done));
          };
      fr->on_patch_system_proxy = [this](bool enable) {
        std::thread([this, enable] {
          cmd::LongOperationGuard long_guard;
          auto r = cmd::CommandService::instance().invoke(
              "patch_verge_config",
              nlohmann::json::object({{"enable_system_proxy", enable},
                                      {"enable_tun_mode", false}}));
          const bool ok = r.is_ok();
          UiPostbox::instance().post([this, enable, ok] {
            if (ok && settings_drawer_) {
              settings_drawer_->sync_takeover(enable, false);
            }
          });
        }).detach();
      };
      // src: :236-245 就绪步轮询 refreshProxy（getProxies 全量重载）
      fr->on_refresh_proxy = [this] { sync_first_run_proxy(); };
      // src: :188-225 重测并优选：trigger_auto_select + forceFull 刷新（异步）
      fr->on_retest_nodes = [this] {
        std::thread([this] {
          std::string uid;
          if (auto p = config::Config::instance().profiles().data_arc();
              p && p->current) {
            uid = *p->current;
          }
          if (!uid.empty()) {
            cmd::CommandService::instance().invoke(
                "trigger_auto_select",
                nlohmann::json::object(
                    {{"profile_uid", uid}, {"sort_type", 0}, {"select", true}}));
          }
          UiPostbox::instance().post([this] {
            sync_first_run_proxy();
            if (first_run_) first_run_->on_retest_finished();
          });
        }).detach();
      };
      fr->on_dismiss = [] {
        scene::FirstRunGuideDialog::set_first_run_completed(true);
      };
      // ★ ❌9 点「稍后」完成引导后自动滑开左侧设置抽屉，指引订阅卡位置（Mini 同构）
      fr->on_dismiss_later = [this] {
        if (settings_drawer_) settings_drawer_->set_open(true);
      };
      // BUG-395：Ready 步第 3 张安全软件卡片已删除，防误杀指南改由帮助菜单承载
      root_.add_child(std::move(fr));
    }
    // 帮助菜单已改自绘（SettingsDrawer::open_help_menu → ContextMenu），
    //   不再走原生 HMENU（TrackPopupMenu 无法截图/样式不一致）。
    // 代理方式第三段 → 添加网址弹窗（Mini：handleAddUrlClick 先 patch
    // rule_fallback=addurl，再 setAddUrlOpen(true)，_layout.tsx:427-430）
    settings_drawer_->on_add_rule = [this] {
      if (add_url_) add_url_->open();
    };
    // 齿轮入口已统一到 quickbar（Mini 同构）；标题栏仅三键
    // BUG-358：皮肤滑块拖动 200ms 防抖写盘定时器接线（src: src/pages/_layout/hooks/use-skin-controls.ts:4）
    settings_drawer_->on_arm_skin_persist_timer = [this](uint32_t ms) {
      if (hwnd_) SetTimer(hwnd_, detail::kTimerSkinPersist, ms, nullptr);
    };
    settings_drawer_->on_cancel_skin_persist_timer = [this] {
      if (hwnd_) KillTimer(hwnd_, detail::kTimerSkinPersist);
    };
    settings_drawer_->wire_callbacks();
    // 恢复上次皮肤/因子（clashwin-skin.json）
    settings_drawer_->restore_prefs();
    // verdict: Mini 的 theme_mode 存在 verge.yaml，是单一事实源；
    //   本地 skin.json 只是缓存。启动期以 verge 为准回填（不触发回写）。
    try {
      const auto& v = *config::Config::instance().verge().latest_arc();
      // BUG-340：启动恢复窗口置顶状态（src: src/pages/_layout.tsx:446）
      pinned_ = v.enable_always_on_top.value_or(false);
      if (hwnd_) {
        SetWindowPos(hwnd_, pinned_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      }
      if (quick_bar_) quick_bar_->set_pinned(pinned_);
      if (v.theme_mode.has_value())
        settings_drawer_->sync_theme_mode(utf::utf8_to_utf16(*v.theme_mode));
      settings_drawer_->sync_takeover(v.enable_system_proxy.value_or(false),
                                      v.enable_tun_mode.value_or(false));
      settings_drawer_->sync_routing(v.rule_fallback.value_or("direct"));
      // ★ ❌7：自启开关读真实注册表状态（Run 键）而非 verge 配置值，
      //   避免「开关显示开、实际开机不自启」错位（← Mini autostart.rs:65 真实状态优先）。
      bool real_auto_launch = v.enable_auto_launch.value_or(false);
      auto launch_st = core::AutostartManager::instance().get_launch_status();
      if (launch_st.is_ok()) real_auto_launch = launch_st.value();
      // clash 侧 Allow LAN / IPv6 权威值（IClashTemp 存于 YAML mapping_，经 get_client_info 读取）
      bool allow_lan = false, ipv6 = false;
      int mixed_port = static_cast<int>(v.verge_mixed_port.value_or(0));
      auto cl = config::Config::instance().clash().latest_arc();
      if (cl) {
        auto info = cl->get_client_info();
        allow_lan = info.allow_lan;
        ipv6 = info.ipv6;
        if (mixed_port <= 0) mixed_port = static_cast<int>(info.mixed_port);
      }
      if (mixed_port <= 0) mixed_port = static_cast<int>(config::consts::kDefaultMixed);
      settings_drawer_->sync_basic(real_auto_launch,
                                   v.enable_silent_start.value_or(false),
                                   allow_lan, ipv6, mixed_port,
                                   v.enable_tun_sniffer);
      // 通知位置：verge.notice_position（缺省 top-right，verge.rs:353）
      if (snackbar_)
        snackbar_->set_position(v.notice_position.value_or("top-right"));
    } catch (...) {
    }
  }

  // ← Mini _layout.tsx:340-355：未完成向导且无真实订阅（remote/local）→
  //   500ms 后自动弹出首次引导；Merge/Script 内置项不算订阅。
  if (first_run_ && !scene::FirstRunGuideDialog::is_first_run_completed() &&
      !scene::FirstRunGuideDialog::has_real_profile()) {
    SetTimer(hwnd_, detail::kTimerFirstRun, 500, nullptr);
  }
}

void MainWindow::resync_settings_ui() {
  // ★ ❌5 设置开关失败回滚：patch 失败后用权威配置值回填抽屉开关。
  //   sync_basic/sync_takeover 内部 set_checked(…, false) 不触发回调，故不会二次 patch；
  //   对齐 Mini use-minimal-settings.ts:77-79 patch 失败 → mutateInfo() 权威值回滚、开关弹回。
  if (!settings_drawer_) return;
  try {
    const auto& v = *config::Config::instance().verge().latest_arc();
    // BUG-340：权威值回填置顶状态（src: src/pages/_layout.tsx:446）
    pinned_ = v.enable_always_on_top.value_or(false);
    if (hwnd_) {
      SetWindowPos(hwnd_, pinned_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (quick_bar_) quick_bar_->set_pinned(pinned_);
    if (v.theme_mode.has_value())
      settings_drawer_->sync_theme_mode(utf::utf8_to_utf16(*v.theme_mode));
    settings_drawer_->sync_takeover(v.enable_system_proxy.value_or(false),
                                    v.enable_tun_mode.value_or(false));
    settings_drawer_->sync_routing(v.rule_fallback.value_or("direct"));
    // ★ ❌7：自启开关读真实注册表状态（Run 键），非 verge 配置值
    bool real_auto_launch = v.enable_auto_launch.value_or(false);
    auto launch_st = core::AutostartManager::instance().get_launch_status();
    if (launch_st.is_ok()) real_auto_launch = launch_st.value();
    // clash 侧 Allow LAN / IPv6 权威值（IClashTemp 存于 YAML mapping_，经 get_client_info 读取）
    bool allow_lan = false, ipv6 = false;
    int mixed_port = static_cast<int>(v.verge_mixed_port.value_or(0));
    auto cl = config::Config::instance().clash().latest_arc();
    if (cl) {
      auto info = cl->get_client_info();
      allow_lan = info.allow_lan;
      ipv6 = info.ipv6;
      if (mixed_port <= 0) mixed_port = static_cast<int>(info.mixed_port);
    }
    if (mixed_port <= 0) mixed_port = static_cast<int>(config::consts::kDefaultMixed);
    settings_drawer_->sync_basic(real_auto_launch,
                                 v.enable_silent_start.value_or(false),
                                 allow_lan, ipv6, mixed_port,
                                 v.enable_tun_sniffer);
  } catch (...) {}
}

}  // namespace clashwin::ui::win
