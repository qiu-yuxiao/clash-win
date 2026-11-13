#include "connections_panel.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

#include "cmd/cmd_support.h"
#include "core/quick_routing.h"
#include "events/event_bus.h"
#include "events/event_names.h"
#include "events/notice.h"
#include "mihomo/mihomo_client.h"
#include "ui/icon_w.h"
#include "ui/theme/theme.h"
#include "ui/soft_shadow.h"
#include "ui/ui_postbox.h"
#include "util/log.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01

namespace clashwin::ui::scene {

namespace {
std::wstring status_label_for(PathControlStatus st) {
  switch (st) {
    case PathControlStatus::Direct:
      return tr("connections.components.miniTable.chainsDirect");
    case PathControlStatus::Reject:
      return tr("connections.components.miniTable.chainsReject");
    case PathControlStatus::Disconnected:
      return tr("connections.components.miniTable.chainsDisconnected");
    case PathControlStatus::Proxy:
    default:
      return tr("connections.components.miniTable.chainsProxy");
  }
}

std::string row_host_display(const ConnItem& item) {
  std::string base = format_path_control_host_label(item);
  if (item.metadata.destination_port.empty()) return base;
  return base + ":" + item.metadata.destination_port;
}

void copy_address_to_clipboard(const std::string& address) {
  if (OpenClipboard(nullptr)) {
    EmptyClipboard();
    std::wstring waddr = utf::utf8_to_utf16(address);
    size_t bytes = (waddr.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
      void* ptr = GlobalLock(hMem);
      if (ptr) {
        memcpy(ptr, waddr.data(), bytes);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
      }
    }
    CloseClipboard();
  }
  // ← connection-table.tsx:301 showNotice.success('connections.notices.copied', { address }, 2000)
  //   载荷给地址原文，文案插值与时长由 notice_handler 的表驱动。
  events::EventBus::notice_message("copied", address);
}
}  // namespace

// ---------- ConnPanelRowWidget ----------
void ConnPanelRowWidget::bind(const ConnItem& item, bool is_closed, size_t row_index) {
  item_ = item;
  is_closed_ = is_closed;
  row_index_ = row_index;
  hover_ = false;
  close_hover_ = false;
}

void ConnPanelRowWidget::on_paint(IRenderTarget& rt) {
  const auto& theme = theme::ThemeService::instance();
  const auto& pal = theme.palette();
  const auto& vars = theme.vars();

  // 1. 斑马纹交替背景（局部坐标 {0,0,w,h}，严禁 bounds().x/y）
  Color4f bg = (row_index_ % 2 == 0)
                   ? vars.theme_panel_bg
                   : (theme.mode() == theme::ThemeMode::Light
                          ? Color4f::hex(0xEEF4FF)  // src: connection-table.tsx:152 hover 行 light
                          : Color4f::hex(0x232B3F));  // src: connection-table.tsx:152 dark
  if (hover_) {
    Color4f prim = theme.primary();
    bg = Color4f(prim.r, prim.g, prim.b, 0.08f);
  }
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{bg});

  // 底边框 1px
  rt.fill_rect({0, bounds().h - 1, bounds().w, 1}, Brush{vars.divider_color});

  // 2. 4 态路径控制色点 + 状态文字（src: connection-table.tsx:42 STATUS_COLOR_MAP）
  Color4f status_color = pal.primary_color;
  switch (item_.control_status) {
    case PathControlStatus::Direct:
      status_color = pal.success_color;
      break;
    case PathControlStatus::Reject:
      status_color = pal.error_color;
      break;
    case PathControlStatus::Disconnected:
      status_color = pal.warning_color;
      break;
    case PathControlStatus::Proxy:
    default:
      status_color = pal.primary_color;
      break;
  }
  Brush dot_brush{status_color};
  // src: connection-table.tsx:192-194
  rt.round_rect({4.0f, 6.0f, 8.0f, 8.0f}, 4.0f, {}, &dot_brush);

  // 3. Host 文本（flex 1，左对齐，省略号，src: connection-table.tsx:393-397 / :471-473）
  float host_w = bounds().w - 74.0f;
  TextStyle host_ts;
  host_ts.size = 11.0f;
  host_ts.color = pal.primary_text;
  host_ts.align_v = AlignV::Center;
  host_ts.ellipsis = true;
  rt.draw_text(utf::utf8_to_utf16(row_host_display(item_)),
               {16.0f, 0, host_w - 20.0f, bounds().h}, host_ts);

  // 4. Host 与 Chains 间垂直分割线（右侧 74px 处，src: connection-table.tsx:471-473）
  rt.fill_rect({bounds().w - 74.0f, 0, 1.0f, bounds().h}, Brush{vars.divider_color});

  // 5. Chains 列（固定 74px 居中，src: connection-table.tsx:415 / :471-473）
  float chains_x = bounds().w - 74.0f;
  bool show_close = (!is_closed_ && !item_.is_synthetic && hover_);
  float chains_w = show_close ? 54.0f : 74.0f;

  TextStyle chains_ts;
  chains_ts.size = 11.0f;
  chains_ts.weight = 600;  // src: connection-table.tsx:415
  chains_ts.color = status_color;
  chains_ts.align_h = AlignH::Center;
  chains_ts.align_v = AlignV::Center;
  chains_ts.ellipsis = true;
  // src: locales/zh/connections.json:37-40 chainsDirect/Reject/Proxy/Disconnected
  std::wstring cat_text = status_label_for(item_.control_status);
  rt.draw_text(cat_text, {chains_x, 0, chains_w, bounds().h}, chains_ts);

  // 6. 悬停关闭按钮 ✕ (非合成的活跃连接时在最右侧 18px 呈现)
  if (show_close) {
    RectF close_rect{bounds().w - 18.0f, 2.0f, 16.0f, 16.0f};
    if (close_hover_) {
      Brush ch_b{Color4f::hex(0xFF3B30, 0.15f)};  // src: _theme.tsx:8 error_color 15%
      rt.round_rect(close_rect, 3.0f, {}, &ch_b);
    }
    TextStyle close_ts;
    close_ts.size = 10.0f;
    close_ts.color = close_hover_ ? Color4f::hex(0xFF3B30)  // src: _theme.tsx:8 error_color
                                  : pal.secondary_text;
    close_ts.align_h = AlignH::Center;
    close_ts.align_v = AlignV::Center;
    rt.draw_text(L"✕", close_rect, close_ts);
  }
}

bool ConnPanelRowWidget::on_mouse(const MouseEvent& e) {
  PointF pt = e.pos;
  const bool can_close = !is_closed_ && !item_.is_synthetic;

  if (e.type == MouseEventType::Move) {
    bool ch = (can_close && pt.x >= bounds().w - 20.0f);
    if (!hover_ || close_hover_ != ch) {
      hover_ = true;
      close_hover_ = ch;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Leave) {
    if (hover_ || close_hover_) {
      hover_ = false;
      close_hover_ = false;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Down) {
    if (e.buttons & kMouseRight) {
      if (on_context_menu) on_context_menu(item_, to_global(pt), global_bounds());
      return true;
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (e.buttons & kMouseRight) {
      return true;
    }
    if (can_close && pt.x >= bounds().w - 20.0f) {
      if (on_close) on_close(item_);
    } else {
      if (on_click) on_click(item_, global_bounds());
    }
    return true;
  }

  return false;
}

// ---------- ConnectionsPanel ----------
ConnectionsPanel::ConnectionsPanel(Widget* parent) : Widget(parent) {
  // 1. 搜索框
  auto search = std::make_unique<TextField>();
  search_ = search.get();
  // src: locales/zh/shared.json:64 「"filter": "过滤条件"」（base-search-box.tsx:117）
  search_->set_placeholder(tr("shared.placeholders.filter"));
  search_->set_i18n_a11y_name("shared.placeholders.filter");
  // 清空按钮（opt-in）：src: connections-panel.tsx:182 BaseSearchBox
  //   → base-search-box.tsx:121-140 endAdornment ClearRounded / resetInput tooltip
  search_->set_clear_button(true, tr("shared.placeholders.resetInput"));
  search_->apply_theme();
  search_->style_.size = 12;
  search_->caret_color_ = search_->style_.color;
  search_->on_text_changed = [this](const std::wstring& text) {
    set_filter(utf::utf16_to_utf8(text));
  };
  add_child(std::move(search));

  // 2. 断开全部按钮（src: connections-panel.tsx:193 get3DButtonStyle contained primary）
  auto btn_disc = std::make_unique<Button>();
  btn_disc->set_i18n_text("settings.mini.connectionsDisconnectAll");
  btn_disconnect_all_ = btn_disc.get();
  btn_disconnect_all_->themed_ = true;
  btn_disconnect_all_->theme_color_type_ = "primary";
  btn_disconnect_all_->text_style.size = 11.0f;  // src: connections-panel.tsx:189 fontSize '11px'
  btn_disconnect_all_->text_style.weight = 700;  // src: connections-panel.tsx:190 fontWeight 700
  btn_disconnect_all_->on_click = [this] {
    if (on_close_all) on_close_all();
  };
  add_child(std::move(btn_disc));

  // 3. 清空历史按钮（src: connections-panel.tsx:210 get3DButtonStyle contained primary）
  auto btn_clr = std::make_unique<Button>();
  btn_clr->set_i18n_text("settings.mini.connectionsClearHistory");
  btn_clear_history_ = btn_clr.get();
  btn_clear_history_->themed_ = true;
  btn_clear_history_->theme_color_type_ = "primary";
  btn_clear_history_->text_style.size = 11.0f;  // src: connections-panel.tsx:206 fontSize '11px'
  btn_clear_history_->text_style.weight = 700;  // src: connections-panel.tsx:207 fontWeight 700
  btn_clear_history_->on_click = [this] {
    path_store_.clear_all();
    if (on_clear_history) on_clear_history();
    refresh_visible();
  };
  btn_clear_history_->set_visible(false);
  add_child(std::move(btn_clr));

  // 4. 虚拟滚动列表 (ScrollList: show_scrollbar = false, row_height = 20)
  auto list = std::make_unique<ScrollList>();
  list_ = list.get();
  list_->set_show_scrollbar(false);
  list_->set_a11y_role(A11yRole::List);
  list_->set_i18n_a11y_name("settings.mini.pathControl");
  add_child(std::move(list));

  // 4.1 外框 + 空态覆盖层（须在列表之上）
  auto overlay = std::make_unique<ConnTableOverlay>();
  table_overlay_ = overlay.get();
  add_child(std::move(overlay));

  // 5. 右键菜单
  auto menu = std::make_unique<ContextMenu>();
  context_menu_ = menu.get();
  add_child(std::move(menu));

  // 6. 详情弹窗
  auto detail = std::make_unique<ConnectionDetailDialog>();
  detail_dialog_ = detail.get();
  detail_dialog_->on_close_connection = [this](const std::string& id) {
    for (const auto& it : visible_items_) {
      if (it.id == id) {
        apply_disconnect_item(it);
        return;
      }
    }
    if (on_close_one) on_close_one(id);
  };
  add_child(std::move(detail));

  // 7. 初始化 4 态路径控制状态并监听 Merge / Clash 配置刷新
  path_store_.load_default();
  reload_managed_rules();
  auto alive = alive_;
  bus_sub_clash_ = events::EventBus::instance().subscribe(
      events::names::kRefreshClash,
      [this, alive](const events::Payload&) {
        UiPostbox::instance().post([this, alive] {
          if (!alive->load(std::memory_order_acquire)) return;
          reload_managed_rules();
          refresh_visible();
        });
      });
  bus_sub_profile_ = events::EventBus::instance().subscribe(
      events::names::kProfileChanged,
      [this, alive](const events::Payload&) {
        UiPostbox::instance().post([this, alive] {
          if (!alive->load(std::memory_order_acquire)) return;
          reload_managed_rules();
          refresh_visible();
        });
      });
}

ConnectionsPanel::~ConnectionsPanel() {
  alive_->store(false, std::memory_order_release);
  if (bus_sub_clash_ != 0) {
    events::EventBus::instance().unsubscribe(bus_sub_clash_);
    bus_sub_clash_ = 0;
  }
  if (bus_sub_profile_ != 0) {
    events::EventBus::instance().unsubscribe(bus_sub_profile_);
    bus_sub_profile_ = 0;
  }
  if (data_ && data_sub_ != 0) {
    data_->unsubscribe(data_sub_);
    data_sub_ = 0;
  }
}

void ConnectionsPanel::bind(ConnectionData* data) {
  if (data_ && data_sub_ != 0) {
    data_->unsubscribe(data_sub_);
    data_sub_ = 0;
  }
  data_ = data;
  if (data_) {
    data_sub_ = data_->subscribe([this] { refresh_visible(); });
  }
  refresh_visible();
}

void ConnectionsPanel::set_tab(Tab tab) {
  if (tab_ == tab) return;
  tab_ = tab;
  btn_clear_history_->set_visible(tab_ == Tab::Closed);
  refresh_visible();
  invalidate();
}

void ConnectionsPanel::set_filter(const std::string& text) {
  if (filter_ == text) return;
  filter_ = text;
  refresh_visible();
}

void ConnectionsPanel::cycle_sort_host() {
  if (sort_col_ != SortCol::Host) {
    sort_col_ = SortCol::Host;
    sort_dir_ = SortDir::Asc;
  } else {
    if (sort_dir_ == SortDir::Asc) {
      sort_dir_ = SortDir::Desc;
    } else if (sort_dir_ == SortDir::Desc) {
      sort_dir_ = SortDir::None;
      sort_col_ = SortCol::None;
    } else {
      sort_dir_ = SortDir::Asc;
    }
  }
  refresh_visible();
  invalidate();
}

void ConnectionsPanel::cycle_sort_chains() {
  if (sort_col_ != SortCol::Chains) {
    sort_col_ = SortCol::Chains;
    sort_dir_ = SortDir::Asc;
  } else {
    if (sort_dir_ == SortDir::Asc) {
      sort_dir_ = SortDir::Desc;
    } else if (sort_dir_ == SortDir::Desc) {
      sort_dir_ = SortDir::None;
      sort_col_ = SortCol::None;
    } else {
      sort_dir_ = SortDir::Asc;
    }
  }
  refresh_visible();
  invalidate();
}

void ConnectionsPanel::reload_managed_rules() {
  managed_rules_ = core::quick_routing::read_managed_merge_rules();
}

void ConnectionsPanel::refresh_visible() {
  visible_items_.clear();
  if (table_overlay_) table_overlay_->empty = true;
  if (!data_) {
    active_composed_count_ = 0;
    list_->set_content(0, kConnPanelRowH, nullptr);
    return;
  }

  // 1. 对账已断连目标 & 合成 4 态活跃 / 历史连接列表（← use-path-control.ts:102-143）
  const auto& raw_active = data_->active();
  path_store_.reconcile(raw_active);
  auto composed_active = compose_active_path_control_connections(
      raw_active, managed_rules_, path_store_.state());
  active_composed_count_ = composed_active.size();

  if (tab_ == Tab::Active) {
    visible_items_ = std::move(composed_active);
  } else {
    std::vector<ConnItem> raw_closed(data_->closed().begin(), data_->closed().end());
    visible_items_ = enrich_closed_path_control_connections(
        raw_closed, managed_rules_, path_store_.state());
  }

  // 2. 过滤
  if (!filter_.empty()) {
    std::string lower_filter = filter_;
    for (auto& c : lower_filter) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::vector<ConnItem> filtered;
    for (const auto& it : visible_items_) {
      std::string h = row_host_display(it);
      std::string p = it.metadata.process;
      std::string c = it.chains_formatted();
      std::string dst = it.metadata.destination_ip;
      for (auto& ch : h) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      for (auto& ch : p) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      for (auto& ch : c) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      for (auto& ch : dst) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

      if (h.find(lower_filter) != std::string::npos || p.find(lower_filter) != std::string::npos ||
          c.find(lower_filter) != std::string::npos || dst.find(lower_filter) != std::string::npos) {
        filtered.push_back(it);
      }
    }
    visible_items_ = std::move(filtered);
  }

  // 3. 排序 (Host / Chains 循环排序)
  if (sort_col_ == SortCol::Host && sort_dir_ != SortDir::None) {
    std::stable_sort(visible_items_.begin(), visible_items_.end(),
                     [this](const ConnItem& a, const ConnItem& b) {
                       std::string ha = row_host_display(a);
                       std::string hb = row_host_display(b);
                       return sort_dir_ == SortDir::Asc ? (ha < hb) : (ha > hb);
                     });
  } else if (sort_col_ == SortCol::Chains && sort_dir_ != SortDir::None) {
    std::stable_sort(visible_items_.begin(), visible_items_.end(),
                     [this](const ConnItem& a, const ConnItem& b) {
                       std::string sa = path_control_status_key(a.control_status);
                       std::string sb = path_control_status_key(b.control_status);
                       return sort_dir_ == SortDir::Asc ? (sa < sb) : (sa > sb);
                     });
  }

  // 4. 同步给 ScrollList 工厂
  if (table_overlay_) table_overlay_->empty = visible_items_.empty();
  list_->set_content(static_cast<int>(visible_items_.size()), kConnPanelRowH,
                     [this](int idx) -> std::unique_ptr<Widget> {
                       if (idx < 0 || idx >= static_cast<int>(visible_items_.size()))
                         return std::unique_ptr<Widget>();
                       auto row = std::make_unique<ConnPanelRowWidget>();
                       row->bind(visible_items_[static_cast<size_t>(idx)], tab_ == Tab::Closed,
                                 static_cast<size_t>(idx));
                       row->on_click = [this](const ConnItem& it, RectF row_global) {
                         detail_dialog_->open(it, tab_ == Tab::Closed, row_global);
                       };
                       row->on_close = [this](const ConnItem& it) {
                         apply_disconnect_item(it);
                       };
                       row->on_context_menu = [this](const ConnItem& it, PointF pt,
                                                     RectF row_global) {
                         show_context_menu(it, pt, row_global);
                       };
                       return row;
                     });

  invalidate();
}

void ConnectionsPanel::show_context_menu(const ConnItem& item, PointF anchor, RectF row_global) {
  std::vector<MenuItem> items;
  // src: locales/zh/connections.json:42-49 六/七项文案（connection-table.tsx:566-623）
  items.push_back({tr("connections.components.contextMenu.setDirect"), false, false, L""});
  items.push_back({tr("connections.components.contextMenu.setProxy"), false, false, L""});
  items.push_back({tr("connections.components.contextMenu.setReject"), false, false, L""});
  const bool disc_disabled = (tab_ == Tab::Closed || item.is_synthetic);
  items.push_back({tr("connections.components.contextMenu.disconnect"), disc_disabled, false, L""});
  const bool has_restore =
      item.managed_rule.has_value() || item.control_status == PathControlStatus::Disconnected;
  if (has_restore) {
    items.push_back({tr("connections.components.contextMenu.restoreDefault"), false, false, L""});
  }
  items.push_back({tr("connections.components.contextMenu.copyAddress"), false, false, L""});
  items.push_back({tr("connections.components.contextMenu.viewDetail"), false, false, L""});

  context_menu_->open(items, to_local(anchor));
  context_menu_->on_select = [this, item, row_global, has_restore](int idx) {
    if (idx == 0) {
      apply_quick_route(item, 0);
    } else if (idx == 1) {
      apply_quick_route(item, 1);
    } else if (idx == 2) {
      apply_quick_route(item, 2);
    } else if (idx == 3) {
      apply_disconnect_item(item);
    } else if (has_restore && idx == 4) {
      apply_restore_default(item);
    } else {
      const int copy_idx = has_restore ? 5 : 4;
      const int detail_idx = has_restore ? 6 : 5;
      if (idx == copy_idx) {
        std::string addr = row_host_display(item);
        if (addr == "--") addr.clear();
        if (!addr.empty()) copy_address_to_clipboard(addr);
      } else if (idx == detail_idx) {
        detail_dialog_->open(item, tab_ == Tab::Closed, row_global);
      }
    }
  };
}

void ConnectionsPanel::apply_disconnect_item(const ConnItem& item) {
  if (item.is_synthetic) return;
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
  path_store_.mark_disconnected(item, now_ms);
  refresh_visible();
  if (on_close_one) on_close_one(item.id);
}

void ConnectionsPanel::apply_restore_default(const ConnItem& item) {
  auto target = extract_connection_target(item, item.managed_rule);
  if (item.managed_rule.has_value()) {
    auto res = core::quick_routing::remove_quick_routing_rule(item.managed_rule->raw);
    if (!res.ok && !res.error.empty()) {
      events::NoticeService::instance().push("error", res.error);
    }
  }
  const std::string tkey =
      target.has_value() ? target->key : item.disconnected_key;
  path_store_.clear_disconnected_target(tkey, item.id);
  reload_managed_rules();
  refresh_visible();
}

void ConnectionsPanel::apply_quick_route(const ConnItem& item, int target_code) {
  core::quick_routing::RuleTarget rule_target = core::quick_routing::RuleTarget::Direct;
  if (target_code == 1) rule_target = core::quick_routing::RuleTarget::Proxy;
  if (target_code == 2) rule_target = core::quick_routing::RuleTarget::Reject;

  auto target = extract_connection_target(item, item.managed_rule);
  if (!target.has_value()) return;

  std::vector<std::string> replace_raw;
  if (item.is_synthetic && item.managed_rule.has_value()) {
    replace_raw.push_back(item.managed_rule->raw);
  }
  auto res = core::quick_routing::add_quick_routing_rule(
      target->type, target->value, rule_target, replace_raw);
  if (res.ok) {
    path_store_.clear_disconnected_target(target->key, item.id);
    reload_managed_rules();
    refresh_visible();

    ConnectionTarget close_target = *target;
    std::vector<ConnItem> snapshot_active =
        data_ ? data_->active() : std::vector<ConnItem>{};
    std::thread([close_target, snapshot_active = std::move(snapshot_active)] {
      auto mc = cmd::mihomo();
      if (!mc.is_ok()) return;
      std::vector<std::string> match_ids;
      auto conns = mc.value()->get_connections(1500);
      if (conns.is_ok()) {
        for (const auto& c : conns.value().connections) {
          ConnItem probe;
          probe.id = c.id;
          probe.metadata.host = c.metadata.host;
          probe.metadata.destination_ip = c.metadata.destination_ip;
          probe.metadata.remote_destination = c.metadata.remote_destination;
          probe.metadata.process = c.metadata.process;
          if (matches_connection_target(probe, close_target) &&
              !c.id.empty() && c.id.rfind("synthetic:", 0) != 0) {
            match_ids.push_back(c.id);
          }
        }
      } else {
        for (const auto& c : snapshot_active) {
          if (matches_connection_target(c, close_target) &&
              !c.id.empty() && c.id.rfind("synthetic:", 0) != 0) {
            match_ids.push_back(c.id);
          }
        }
      }
      for (const auto& id : match_ids) {
        (void)mc.value()->close_connection(id, 1500);
      }
    }).detach();
  } else if (!res.error.empty()) {
    events::NoticeService::instance().push("error", res.error);
  }
}

void ConnTableOverlay::on_paint(IRenderTarget& rt) {
  const auto& vars = theme::ThemeService::instance().vars();
  // src: connection-table.tsx:48 border '5px double var(--theme-border)' radius 4
  //   CSS double = 两条 1.5px 线 + 2px 间隔（总 5px）
  RectF r{0, 0, bounds().w, bounds().h};
  auto stroke = [&](float inset) {
    RectF rr{r.x + inset, r.y + inset, r.w - inset * 2, r.h - inset * 2};
    rt.round_rect(rr, std::max(0.0f, 4.0f - inset), Pen{vars.theme_border, 1.5f}, nullptr);
  };
  stroke(0.75f);
  stroke(3.25f);
  if (!empty) return;
  // src: base-empty.tsx:19-45 居中「空空如也」（shared.json:52）：
  //   :35 <InboxRounded fontSize='4em' />（4em × 16px = 64px）
  //   :36 <Typography fontSize='1.25em' />（1.25em × 16px = 20px）
  //   :32 color: alpha(palette.text.secondary, 0.75)
  Color4f c = theme::ThemeService::instance().palette().secondary_text;
  c.a *= 0.75f;  // src: base-empty.tsx:32
  const float icon_sz = 64.0f;  // src: base-empty.tsx:35 '4em'
  const float cx = r.x + r.w / 2.0f;
  const float cy = r.y + r.h / 2.0f;
  IconW::paint_at(rt, "inbox",
                  {cx - icon_sz / 2.0f, cy - icon_sz + 4.0f, icon_sz, icon_sz}, c);
  TextStyle ts;
  ts.size = 20.0f;  // src: base-empty.tsx:36 '1.25em'
  ts.color = c;
  ts.align_h = AlignH::Center;
  ts.align_v = AlignV::Center;
  rt.draw_text(tr("shared.statuses.empty"),
               {r.x, cy + 8.0f, r.w, 24.0f}, ts);
}

void ConnectionsPanel::on_arrange(const RectF& area) {
  set_bounds(area);

  // 1. 顶部 Header 行 (高 22px): Subtitle + 3D Toggle
  // src: connections-panel.tsx:65-73 justifyContent: 'flex-start', gap: 1 (8px)
  TextStyle title_ts;
  title_ts.size = 11.0f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  float title_w = 145.0f;
  if (auto* te = text_engine()) {
    title_ts.wrap = false;
    title_w = te->measure_text(tr("settings.mini.pathControl"), title_ts, 0).w + 4.0f;  // 豁免: Mini 无对应物 — 防止 DWrite 字距微偏截断末尾字（出处: 无）
  }
  float seg_w = 140.0f;
  float seg_h = 18.0f;
  float seg_x = 4.0f + title_w + 8.0f;
  tab_active_rect_ = {seg_x, 2.0f, seg_w / 2.0f, seg_h};
  tab_closed_rect_ = {seg_x + seg_w / 2.0f, 2.0f, seg_w / 2.0f, seg_h};

  // 2. 工具栏行 (高 24px, y = 24px): 搜索框 + 断开全部 [+ 清空历史]
  float tool_y = 24.0f;
  float btn_w = 64.0f;
  float btn_h = 20.0f;

  if (tab_ == Tab::Closed) {
    float sw = area.w - btn_w * 2 - 16.0f;
    if (sw < 60.0f) sw = 60.0f;
    search_->layout_tree({4.0f, tool_y, sw, btn_h});
    btn_disconnect_all_->layout_tree({4.0f + sw + 4.0f, tool_y, btn_w, btn_h});
    btn_clear_history_->layout_tree({4.0f + sw + 4.0f + btn_w + 4.0f, tool_y, btn_w, btn_h});
    btn_clear_history_->set_visible(true);
  } else {
    float sw = area.w - btn_w - 12.0f;
    if (sw < 60.0f) sw = 60.0f;
    search_->layout_tree({4.0f, tool_y, sw, btn_h});
    btn_disconnect_all_->layout_tree({4.0f + sw + 4.0f, tool_y, btn_w, btn_h});
    btn_clear_history_->set_visible(false);
  }

  // 3. 表头行 (高 20px, y = 48px): Host (flex 1) + Chains (74px, src: connection-table.tsx:192-194)
  header_host_rect_ = {0, 48.0f, area.w - 74.0f, 20.0f};
  header_chains_rect_ = {area.w - 74.0f, 48.0f, 74.0f, 20.0f};

  // 4. 虚拟列表 (y = 68px, 高 area.h - 68px)
  float list_h = area.h - 68.0f;
  if (list_h < 0) list_h = 0;
  list_->layout_tree({0, 68.0f, area.w, list_h});
  if (table_overlay_) table_overlay_->layout_tree({0, 68.0f, area.w, list_h});

  // 5. 覆盖层 (详情弹窗占满本面板局部坐标空间)
  detail_dialog_->layout_tree({0, 0, area.w, area.h});
}

void ConnectionsPanel::on_paint(IRenderTarget& rt) {
  const auto& theme = theme::ThemeService::instance();
  const auto& pal = theme.palette();
  const auto& vars = theme.vars();

  // 1. 顶部 Header 行 (局部坐标 {0, y}，绝无 bounds().x/y)
  TextStyle title_ts;
  title_ts.size = 11.0f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = pal.primary_text;
  title_ts.align_v = AlignV::Center;
  float title_w = tab_active_rect_.x - 4.0f - 8.0f;
  if (title_w <= 0.0f) title_w = 145.0f;
  title_ts.wrap = false;
  rt.draw_text(tr("settings.mini.pathControl"), {4.0f, 2.0f, title_w + 4.0f, 18.0f}, title_ts);  // src: connections-panel.tsx:69-73

  // 3D Toggle (140×18px)
  // src: connections-panel.tsx:75-100 get3DSegmentedContainerStyle(theme) + get3DSegmentedActiveStyle(theme)
  float seg_x = tab_active_rect_.x;
  float seg_w = 140.0f;
  float seg_h = 18.0f;
  const auto sp = theme.segmented_paint();
  const float cont_radius = sp.container_radius > 0.01f ? sp.container_radius : 4.0f;
  if (search_) {
    search_->apply_theme();
    search_->style_.size = 12;
    search_->caret_color_ = search_->style_.color;
  }
  RectF cont_r{seg_x, 2.0f, seg_w, seg_h};
  Brush cont_bg{sp.container_bg};
  rt.round_rect(cont_r, cont_radius, Pen{sp.container_border, 1.0f}, &cont_bg);
  if (sp.container_inset) {
    draw_top_inset_shadow(rt, cont_r, cont_radius, sp.container_inset_color,
                          sp.container_inset_dy, sp.container_inset_blur);
  }
  if (sp.container_top_hi.a > 0.004f) {
    rt.draw_line({cont_r.x + cont_radius, cont_r.bottom() + 0.5f},
                 {cont_r.right() - cont_radius, cont_r.bottom() + 0.5f},
                 Pen{sp.container_top_hi, 1.0f});
  }

  // 滑块指示背景
  RectF active_seg = (tab_ == Tab::Active) ? tab_active_rect_ : tab_closed_rect_;
  RectF pill{active_seg.x + 1.0f, active_seg.y + 1.0f, active_seg.w - 2.0f, active_seg.h - 2.0f};
  const bool has_border = sp.active_border.a > 0.001f;
  if (sp.active_grad_count > 0) {
    rt.fill_round_rect_gradient_angle(pill, sp.active_radius,
                                      sp.active_grad, sp.active_grad_count,
                                      sp.active_grad_angle);
    if (has_border) {
      rt.round_rect(pill, sp.active_radius, Pen{sp.active_border, 1.0f}, nullptr);
    }
  } else {
    Brush sel_bg{sp.active_bg};
    rt.round_rect(pill, sp.active_radius,
                  Pen{sp.active_border, has_border ? 1.0f : 0.0f}, &sel_bg);
  }
  if (sp.active_has_bevel && sp.active_bevel_dark.a > 0.004f) {
    const float by = sp.active_bevel_dy;
    rt.draw_line({pill.x + sp.active_radius, pill.bottom() + 0.5f},
                 {pill.right() - sp.active_radius, pill.bottom() + 0.5f},
                 Pen{sp.active_bevel_dark, 1.0f});
    if (by > 0.5f) {
      rt.draw_line({pill.x + sp.active_radius, pill.bottom() + by + 0.5f},
                   {pill.right() - sp.active_radius, pill.bottom() + by + 0.5f},
                   Pen{sp.active_bevel_dark, 1.0f});
    }
  }
  if (sp.active_has_glow && sp.active_glow_color.a > 0.004f) {
    draw_glow(rt, pill, sp.active_radius, sp.active_glow_color, sp.active_glow_blur);
  }

  size_t act_num = data_ ? active_composed_count_ : 0;
  size_t cls_num = data_ ? data_->closed().size() : 0;

  std::wstring act_text = tr("settings.mini.connectionsActive") + L" (" +
                          std::to_wstring(act_num) + L")";
  std::wstring cls_text = tr("settings.mini.connectionsHistory") + L" (" +
                          std::to_wstring(cls_num) + L")";

  TextStyle tab_act_ts;
  tab_act_ts.size = 10.5f;
  tab_act_ts.weight = static_cast<int>(Weight::Bold);
  tab_act_ts.color = (tab_ == Tab::Active) ? sp.active_text : pal.secondary_text;
  tab_act_ts.align_h = AlignH::Center;
  tab_act_ts.align_v = AlignV::Center;
  rt.draw_text(act_text, tab_active_rect_, tab_act_ts);

  TextStyle tab_cls_ts;
  tab_cls_ts.size = 10.5f;
  tab_cls_ts.weight = static_cast<int>(Weight::Bold);
  tab_cls_ts.color = (tab_ == Tab::Closed) ? sp.active_text : pal.secondary_text;
  tab_cls_ts.align_h = AlignH::Center;
  tab_cls_ts.align_v = AlignV::Center;
  rt.draw_text(cls_text, tab_closed_rect_, tab_cls_ts);

  // 2. 表头背景 (y = 48px, 高 20px)
  Brush th_bg{vars.theme_panel_bg};
  rt.fill_rect({0, 48.0f, bounds().w, 20.0f}, Brush{th_bg});
  rt.fill_rect({0, 67.0f, bounds().w, 1.0f}, Brush{vars.divider_color});

  // Host 表头
  // src: locales/zh/connections.json:35 miniTable.hostColumn "链接目标"
  std::wstring host_title = tr("connections.components.miniTable.hostColumn");
  if (sort_col_ == SortCol::Host) {
    if (sort_dir_ == SortDir::Asc) host_title += L" ▲";
    if (sort_dir_ == SortDir::Desc) host_title += L" ▼";
  }
  TextStyle th_host_ts;
  th_host_ts.size = 11.0f;
  th_host_ts.weight = static_cast<int>(Weight::Bold);
  th_host_ts.color = pal.secondary_text;
  th_host_ts.align_v = AlignV::Center;
  // src: connection-table.tsx:192-194 Chains 宽 74px
  rt.draw_text(host_title, {16.0f, 48.0f, bounds().w - 74.0f - 20.0f, 20.0f}, th_host_ts);

  // 表头垂直分割线
  rt.fill_rect({bounds().w - 74.0f, 48.0f, 1.0f, 20.0f}, Brush{vars.divider_color});

  // Chains 表头 (固定 74px 居中)
  std::wstring chains_title = tr("connections.components.miniTable.chainsColumn");
  if (sort_col_ == SortCol::Chains) {
    if (sort_dir_ == SortDir::Asc) chains_title += L" ▲";
    if (sort_dir_ == SortDir::Desc) chains_title += L" ▼";
  }
  TextStyle th_chains_ts;
  th_chains_ts.size = 11.0f;
  th_chains_ts.weight = static_cast<int>(Weight::Bold);
  th_chains_ts.color = pal.secondary_text;
  th_chains_ts.align_h = AlignH::Center;
  th_chains_ts.align_v = AlignV::Center;
  // src: connection-table.tsx:192-194
  rt.draw_text(chains_title, {bounds().w - 74.0f, 48.0f, 74.0f, 20.0f}, th_chains_ts);

  // 空态：src: locales/zh/shared.json:52 「"empty": "空空如也"」（base-empty.tsx:16 textKey）
  if (false && visible_items_.empty()) {
    const RectF lr = list_->bounds();
    Color4f ec(pal.secondary_text.r, pal.secondary_text.g, pal.secondary_text.b,
               0.75f);  // src: base-empty.tsx:32 alpha(text.secondary, .75)
    const float icon_sz = 64.0f;  // src: base-empty.tsx:35 '4em'
    const float cx = lr.x + lr.w / 2.0f;
    const float cy = lr.y + lr.h / 2.0f;
    IconW::paint_at(rt, "inbox",
                    {cx - icon_sz / 2.0f, cy - icon_sz + 4.0f, icon_sz, icon_sz}, ec);
    TextStyle empty_ts;
    empty_ts.size = 20.0f;  // src: base-empty.tsx:36 '1.25em'
    empty_ts.color = ec;
    empty_ts.align_h = AlignH::Center;
    empty_ts.align_v = AlignV::Center;
    rt.draw_text(tr("shared.statuses.empty"),
                 {lr.x, cy + 8.0f, lr.w, 24.0f}, empty_ts);
  }
}

bool ConnectionsPanel::on_mouse(const MouseEvent& e) {
  if (e.type == MouseEventType::Up) {
    PointF pt = e.pos;
    if (tab_active_rect_.contains(pt.x, pt.y)) {
      set_tab(Tab::Active);
      return true;
    }
    if (tab_closed_rect_.contains(pt.x, pt.y)) {
      set_tab(Tab::Closed);
      return true;
    }
    if (header_host_rect_.contains(pt.x, pt.y)) {
      cycle_sort_host();
      return true;
    }
    if (header_chains_rect_.contains(pt.x, pt.y)) {
      cycle_sort_chains();
      return true;
    }
  }

  return Widget::on_mouse(e);
}

std::wstring ConnTableOverlay::a11y_default_name() const {
  return tr("shared.statuses.empty");
}

std::wstring ConnPanelRowWidget::a11y_default_name() const {
  return utf::utf8_to_utf16(row_host_display(item_)) + L" - " +
         status_label_for(item_.control_status);
}

std::wstring ConnectionsPanel::a11y_default_name() const {
  return tr("settings.mini.pathControl");
}

A11ySubItem ConnectionsPanel::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  sub.can_invoke = enabled();
  size_t act_num = data_ ? active_composed_count_ : 0;
  size_t cls_num = data_ ? data_->closed().size() : 0;
  if (idx == 0) {
    sub.role = A11yRole::TabItem;
    sub.name = tr("settings.mini.connectionsActive") + L" (" +
               std::to_wstring(act_num) + L")";
    sub.local_bounds = tab_active_rect_;
    sub.selected = (tab_ == Tab::Active);
    sub.pos_in_set = 1;
    sub.size_of_set = 2;
  } else if (idx == 1) {
    sub.role = A11yRole::TabItem;
    sub.name = tr("settings.mini.connectionsHistory") + L" (" +
               std::to_wstring(cls_num) + L")";
    sub.local_bounds = tab_closed_rect_;
    sub.selected = (tab_ == Tab::Closed);
    sub.pos_in_set = 2;
    sub.size_of_set = 2;
  } else if (idx == 2) {
    sub.role = A11yRole::Button;
    sub.name = tr("connections.components.miniTable.hostColumn");
    sub.local_bounds = header_host_rect_;
  } else {
    sub.role = A11yRole::Button;
    sub.name = tr("connections.components.miniTable.chainsColumn");
    sub.local_bounds = header_chains_rect_;
  }
  return sub;
}

bool ConnectionsPanel::a11y_invoke_virtual_child(size_t idx) {
  if (!enabled()) return false;
  switch (idx) {
    case 0: set_tab(Tab::Active); return true;
    case 1: set_tab(Tab::Closed); return true;
    case 2: cycle_sort_host(); return true;
    case 3: cycle_sort_chains(); return true;
    default: return false;
  }
}

}  // namespace clashwin::ui::scene
