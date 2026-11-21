// LogViewerModal 实现（拆分自 utility_modals.cpp）
// src: src/pages/_layout/components/dialogs/logs-dialog.tsx:1-73
#include "ui/scenes/dialogs/log_viewer_modal.h"

#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>

#include "ui/card_draw.h"
#include "ui/i18n_text.h"
#include "ui/icon_w.h"
#include "ui/theme/theme.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

using theme::ThemeService;

namespace {

std::string get_current_log_time() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  // src: src/components/log/log-item.tsx:45 dayjs MM-DD HH:mm:ss
  oss << std::put_time(&tm, "%m-%d %H:%M:%S");
  return oss.str();
}

std::wstring to_lower_w(const std::wstring& s) {
  std::wstring out = s;
  for (auto& ch : out) ch = static_cast<wchar_t>(::towlower(ch));
  return out;
}

bool icontains_ascii_or_utf8(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return true;
  const std::wstring wh = to_lower_w(utf::utf8_to_utf16(hay));
  const std::wstring wn = to_lower_w(utf::utf8_to_utf16(needle));
  return wh.find(wn) != std::wstring::npos;
}

void apply_menu_style_to_list(MenuList* ml, const MenuStyle& st) {
  if (!ml) return;
  ml->item_height = st.item_h;
  ml->sep_height_ = st.sep_h;
  ml->min_width_ = st.min_w;
  ml->radius_ = st.radius;
  ml->font_size_ = st.font_size;
  ml->font_weight_ = st.font_weight;
  ml->gold_hover_ = st.gold_hover;
  ml->gradient_panel_ = st.gradient_panel;
  ml->pad_top_ = st.pad_v;
  ml->pad_bottom_ = st.pad_v;
  ml->padding_h = st.padding_h;
  ml->hover_inset_ = st.hover_inset;
  ml->bg_ = st.bg;
  ml->border_ = st.border;
  ml->text_ = st.text;
  ml->disabled_text_ = st.disabled_text;
  ml->danger_text_ = st.danger_text;
  ml->selected_bg_ = st.selected_bg;
  ml->selected_text_ = st.selected_text;
  ml->hover_grad_ = st.hover_grad;
  ml->hover_n_ = st.hover_n;
  ml->hover_angle_ = st.hover_angle;
  ml->hover_bg_ = st.hover_bg;
  ml->hover_radius_ = st.hover_radius;
  ml->hover_text_ = st.hover_text;
  ml->hover_border_ = st.hover_border;
  ml->hover_border_w_ = st.hover_border_w;
  ml->hover_glow_ = st.hover_glow;
  ml->hover_glow_color_ = st.hover_glow_color;
  ml->hover_glow_blur_ = st.hover_glow_blur;
  ml->panel_grad_on_ = st.panel_grad_on;
  ml->panel_n_ = st.panel_n;
  ml->panel_angle_ = st.panel_angle;
  ml->panel_bg_ = st.panel_bg;
  ml->panel_border_ = st.panel_border;
  ml->panel_border_w_ = st.panel_border_w;
  for (int i = 0; i < 4; ++i) {  // src: src/utils/button-styles/button.ts:35
    ml->hover_stops_[i] = st.hover_stops[i];
    ml->panel_stops_[i] = st.panel_stops[i];
  }
}

// src: src/components/log/log-item.tsx:48-70 #ffeb3b 高亮块
void draw_text_with_highlights(IRenderTarget& rt, ITextEngine* eng,
                               const std::wstring& text, const std::wstring& query_w,
                               const RectF& box, const TextStyle& base_ts) {
  if (query_w.empty() || !eng || text.empty()) {
    rt.draw_text(text, box, base_ts);
    return;
  }
  const std::wstring q_low = to_lower_w(query_w);
  const std::wstring t_low = to_lower_w(text);
  if (t_low.find(q_low) == std::wstring::npos) {
    rt.draw_text(text, box, base_ts);
    return;
  }

  // src: src/components/log/log-item.tsx:58
  const Brush hl_bg{Color4f::hex(0xFFEB3B, 0.7f)};
  constexpr float kRowStep = 16.0f;  // src: src/components/log/log-item.tsx:11 lineHeight 16px
  std::vector<std::wstring> lines;
  if (!base_ts.wrap) {
    lines.push_back(text);
  } else {
    size_t pos = 0;
    const size_t n = text.size();
    while (pos < n) {
      size_t end = pos + 1;
      while (end < n) {
        if (text[end] == L'\n') break;
        const float w = eng->measure_text(text.substr(pos, end - pos + 1), base_ts, 0).w;
        if (w > box.w && end > pos) break;
        ++end;
      }
      lines.push_back(text.substr(pos, end - pos));
      pos = (end < n && text[end] == L'\n') ? (end + 1) : end;
    }
    if (lines.empty()) lines.push_back(text);
  }

  TextStyle row_ts = base_ts;
  row_ts.wrap = false;
  row_ts.ellipsis = false;
  TextStyle hl_ts = row_ts;
  hl_ts.color = Color4f::hex(0x000000);  // src: src/components/log/log-item.tsx:59

  const float row_h = base_ts.wrap ? kRowStep : box.h;
  for (size_t li = 0; li < lines.size(); ++li) {
    const float ry = box.y + static_cast<float>(li) * row_h;
    if (ry > box.bottom()) break;
    const RectF row_rc{box.x, ry, box.w, row_h};
    rt.draw_text(lines[li], row_rc, row_ts);

    const std::wstring row_low = to_lower_w(lines[li]);
    size_t search_at = 0;
    while (true) {
      const size_t hit = row_low.find(q_low, search_at);
      if (hit == std::wstring::npos) break;
      const float x0 = hit > 0 ? eng->measure_text(lines[li].substr(0, hit), row_ts, 0).w : 0.0f;
      const std::wstring match_s = lines[li].substr(hit, q_low.size());
      const float mw = eng->measure_text(match_s, row_ts, 0).w;
      const RectF hl_rc{box.x + x0, ry, mw, row_h};
      rt.round_rect(hl_rc, 2.0f, Pen{Color4f(0, 0, 0, 0), 0.0f}, &hl_bg);
      rt.draw_text(match_s, hl_rc, hl_ts);
      search_at = hit + q_low.size();
    }
  }
}

}  // namespace

// src: src/pages/_layout/components/dialogs/logs-dialog.tsx:1-73
LogViewerModal::LogViewerModal(Widget* parent) : Widget(parent) {
  set_z_order(960);
  line_height_ = 38.0f;
  last_flush_time_ = std::chrono::steady_clock::now();

  // src: src/components/base/base-search-box.tsx:117-126
  {
    auto sf = std::make_unique<TextField>(this);
    search_field_ = sf.get();
    search_field_->height_ = 28.0f;  // src: src/pages/logs.tsx:191-221
    search_field_->set_padding_x(6.0f);
    search_field_->style_.size = 11.0f;
    search_field_->set_placeholder(tr("shared.placeholders.filter"));
    search_field_->set_i18n_a11y_name("shared.placeholders.filter");
    search_field_->set_clear_button(true, tr("shared.placeholders.resetInput"));
    search_field_->apply_theme();
    search_field_->set_visible(false);
    search_field_->on_text_changed = [this](const std::wstring& w) {
      if (syncing_search_) return;
      set_search_query(utf::utf16_to_utf8(w));
    };
    search_field_->on_key_intercept = [this](const KeyEvent& e) {
      if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
        close();
        return true;
      }
      return false;
    };
    search_field_->on_context_menu = [this](PointF anchor) { open_search_menu(anchor); };
    add_child(std::move(sf));
  }

  // src: src/pages/logs.tsx:179-191
  auto menu = std::make_unique<MenuList>();
  lvl_menu_ = menu.get();
  {
    const wchar_t* labels[5] = {L"ALL", L"DEBUG", L"INFO", L"WARN", L"ERROR"};
    for (int k = 0; k < 5; ++k) {
      MenuItem it;
      it.label = labels[k];
      it.selected = (k == 0);
      menu->items.push_back(it);
    }
    MenuStyle mst;
    apply_menu_skin(mst);
    // src: src/pages/_layout/utils/style-helpers.tsx:126 MenuItem 32px
    mst.item_h = 32.0f;
    mst.min_w = 100.0f;
    apply_menu_style_to_list(lvl_menu_, mst);
    menu->set_visible(false);
    menu->on_select = [this](int i) {
      static const char* vals[5] = {"all", "debug", "info", "warn", "error"};
      if (lvl_menu_) {
        for (int k = 0; k < 5; ++k) lvl_menu_->items[k].selected = (k == i);
        lvl_menu_->set_visible(false);
      }
      set_level_filter(vals[i]);
    };
  }
  add_child(std::move(menu));

  auto sm = std::make_unique<ContextMenu>(this);
  search_menu_ = sm.get();
  search_menu_->set_z_order(1020);  // 豁免: Mini 无对应物 — 弹窗内搜索右键菜单叠在搜索框之上（出处: src/pages/logs.tsx:215）
  add_child(std::move(sm));
}

// src: src/pages/_layout/components/dialogs/logs-dialog.tsx:1-73
void LogViewerModal::open() {
  open_ = true;
  fade_.show();
  flush_incoming();
  hover_close_x_ = false;
  hover_pause_ = false;
  hover_order_ = false;
  hover_clear_ = false;
  if (search_field_) {
    search_field_->apply_theme();
    search_field_->style_.size = 11.0f;  // src: src/components/base/base-search-box.tsx:117
    search_field_->set_visible(true);
  }
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

void LogViewerModal::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();
  if (lvl_menu_) lvl_menu_->set_visible(false);
  if (search_menu_ && search_menu_->is_open()) search_menu_->close();
  if (search_field_) {
    search_field_->blur();
    search_field_->set_visible(false);
  }
  invalidate();
  if (on_dismiss) on_dismiss();
}

void LogViewerModal::set_paused(bool p) {
  if (paused_ == p) return;
  paused_ = p;
  if (!paused_) flush_incoming();
  invalidate();
  if (on_pause_changed) on_pause_changed(paused_);
}

// src: src/pages/logs.tsx:215
void LogViewerModal::open_search_menu(PointF anchor) {
  if (!search_menu_ || !search_field_ || !search_field_->enabled()) return;
  MenuStyle st;
  apply_menu_skin(st);
  // src: src/pages/_layout/utils/style-helpers.tsx:126 菜单行高 32
  st.item_h = 32.0f;
  st.min_w = 160.0f;
  st.font_size = 13.0f;
  st.padding_h = 14.0f;

  std::vector<MenuItem> items;
  items.push_back({tr("shared.actions.cut"), !search_field_->can_cut(), false, L"", false});
  items.push_back({tr("shared.actions.copy"), !search_field_->can_copy(), false, L"", false});
  items.push_back({tr("shared.actions.paste"), !search_field_->can_paste(), false, L"", false});
  items.push_back({tr("shared.actions.selectAll"), !search_field_->can_select_all(), false, L"", false});
  items.push_back({L"", false, true, L"", false});
  items.push_back({tr("shared.actions.clearInput"), !search_field_->can_clear(), false, L"", true});

  search_menu_->style_ = st;
  search_menu_->on_select = [this](int idx) {
    if (!search_field_) return;
    switch (idx) {
      case 0: search_field_->cut(); break;
      case 1: search_field_->copy(); break;
      case 2: search_field_->paste(); break;
      case 3: search_field_->select_all(); break;
      case 5: search_field_->clear_text(); break;
      default: break;
    }
  };
  PointF local = anchor;
  if (search_menu_->parent()) {
    local = search_menu_->parent()->to_local(anchor);
  }
  search_menu_->open(items, local);
}

void LogViewerModal::push_log(const std::string& level, const std::string& msg,
                              const std::string& time_str) {
  LogEntry entry;
  entry.level = level.empty() ? "info" : level;
  entry.message = msg;
  entry.time = time_str.empty() ? get_current_log_time() : time_str;

  incoming_queue_.push_back(std::move(entry));

  auto now = std::chrono::steady_clock::now();
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush_time_).count();
  if (elapsed_ms >= static_cast<int64_t>(kFlushDelayMs)) {
    flush_incoming();
  }
}

void LogViewerModal::push_log_line(const std::string& raw_line) {
  std::string lvl = "info";
  if (raw_line.find("level=debug") != std::string::npos) lvl = "debug";
  else if (raw_line.find("level=warn") != std::string::npos) lvl = "warn";
  else if (raw_line.find("level=error") != std::string::npos) lvl = "error";
  push_log(lvl, raw_line);
}

// src: src/pages/logs.tsx:74
void LogViewerModal::flush_incoming() {
  last_flush_time_ = std::chrono::steady_clock::now();
  if (incoming_queue_.empty()) return;

  if (!paused_) {
    const float item_w = std::max(40.0f, log_area_rect_.w - 12.0f);  // src: src/components/log/log-item.tsx:10
    float added_h = 0.0f;
    float evicted_h = 0.0f;
    for (auto& item : incoming_queue_) {
      if (matches_filter(item)) {
        added_h += measure_entry_height(item, item_w);
      }
      logs_.push_back(std::move(item));
      if (logs_.size() > kMaxLogLines) {
        if (matches_filter(logs_.front())) {
          evicted_h += measure_entry_height(logs_.front(), item_w);
        }
        logs_.pop_front();
      }
    }
    recompute_total_height();
    const float max_s = max_scroll();
    if (desc_) {
      if (scroll_y_ > 4.0f) {  // src: src/pages/logs.tsx:74
        scroll_y_ = std::clamp(scroll_y_ + added_h, 0.0f, max_s);
      } else {
        scroll_y_ = 0.0f;
      }
    } else {
      if (stick_to_bottom_) {
        scroll_y_ = max_s;
      } else {
        scroll_y_ = std::clamp(scroll_y_ - evicted_h, 0.0f, max_s);
      }
    }
  }
  incoming_queue_.clear();
  invalidate();
}

void LogViewerModal::clear_logs() {
  logs_.clear();
  incoming_queue_.clear();
  total_content_h_ = 0.0f;
  scroll_y_ = 0.0f;
  stick_to_bottom_ = true;
  invalidate();
}

void LogViewerModal::set_desc(bool d) {
  if (desc_ == d) return;
  desc_ = d;
  recompute_total_height();
  if (desc_) {
    scroll_y_ = 0.0f;
  } else {
    stick_to_bottom_ = true;
    scroll_y_ = max_scroll();
  }
  invalidate();
}

void LogViewerModal::set_level_filter(const std::string& lvl) {
  if (level_filter_ == lvl) return;
  level_filter_ = lvl;
  recompute_total_height();
  if (!desc_ && stick_to_bottom_) {
    scroll_y_ = max_scroll();
  } else {
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
  }
  invalidate();
}

void LogViewerModal::set_search_query(const std::string& q) {
  if (search_query_ == q) return;
  search_query_ = q;
  if (search_field_ && !syncing_search_) {
    const std::wstring wq = utf::utf8_to_utf16(q);
    if (search_field_->text() != wq) {
      syncing_search_ = true;
      search_field_->set_text_silent(wq);
      syncing_search_ = false;
    }
  }
  recompute_total_height();
  if (!desc_ && stick_to_bottom_) {
    scroll_y_ = max_scroll();
  } else {
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
  }
  invalidate();
}

float LogViewerModal::max_scroll() const {
  return std::max(0.0f, total_content_h_ - std::max(0.0f, log_area_rect_.h));
}

// src: src/pages/logs.tsx:74
void LogViewerModal::set_scroll_y(float y) {
  const float max_s = max_scroll();
  scroll_y_ = std::clamp(y, 0.0f, max_s);
  if (!desc_) {
    stick_to_bottom_ = (max_s - scroll_y_ <= 48.0f);  // src: src/pages/logs.tsx:74
  }
  invalidate();
}

// src: src/components/log/log-item.tsx:9-13
float LogViewerModal::measure_entry_height(const LogEntry& e, float item_w) const {
  TextStyle log_ts;
  log_ts.size = 10.0f;  // src: src/components/log/log-item.tsx:13
  log_ts.weight = static_cast<int>(Weight::Normal);
  log_ts.align_v = AlignV::Center;
  log_ts.wrap = true;  // src: src/components/log/log-item.tsx:38
  log_ts.ellipsis = false;
  float msg_h = 16.0f;  // src: src/components/log/log-item.tsx:11
  if (ITextEngine* eng = text_engine()) {
    msg_h = eng->measure_text(utf::utf8_to_utf16(e.message), log_ts, item_w).h;
  }
  if (msg_h < 16.0f) msg_h = 16.0f;  // src: src/components/log/log-item.tsx:11
  return 17.0f + msg_h + 5.0f;  // src: src/components/log/log-item.tsx:9
}

// src: src/components/log/log-item.tsx:9-10
void LogViewerModal::recompute_total_height() {
  const float item_w = std::max(40.0f, log_area_rect_.w - 12.0f);  // src: src/components/log/log-item.tsx:10
  float sum = 0.0f;
  size_t count = 0;
  for (const auto& item : logs_) {
    if (!matches_filter(item)) continue;
    sum += measure_entry_height(item, item_w);
    ++count;
  }
  total_content_h_ = count > 0 ? (sum + 12.0f) : 0.0f;  // src: src/components/log/log-item.tsx:9
}

bool LogViewerModal::matches_filter(const LogEntry& entry) const {
  if (level_filter_ != "all") {
    std::string want = level_filter_;
    if (want == "warn") want = "warning";
    const bool hit = entry.level == want ||
                     (want == "error" && entry.level == "err");
    if (!hit) return false;
  }
  if (!search_query_.empty()) {
    if (!icontains_ascii_or_utf8(entry.time, search_query_) &&
        !icontains_ascii_or_utf8(entry.level, search_query_) &&
        !icontains_ascii_or_utf8(entry.message, search_query_)) {
      return false;
    }
  }
  return true;
}

// src: src/pages/_layout/components/dialogs/logs-dialog.tsx:1-73
void LogViewerModal::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: src/utils/popup-styles.ts:43-50
  const float cw = std::min(area.w, theme::kPopupPaneWidth);
  const float ch = std::min(480.0f, area.h - 16.0f);
  float cx = 0.0f;
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 30.0f, cy + 10.0f, 20.0f, 22.0f};
  btn_clear_rect_ = {btn_close_x_rect_.x - 4.0f - 40.0f, cy + 10.0f, 40.0f, 24.0f};
  btn_order_rect_ = {btn_clear_rect_.x - 4.0f - 26.0f, cy + 9.0f, 26.0f, 26.0f};
  btn_pause_rect_ = {btn_order_rect_.x - 4.0f - 26.0f, cy + 9.0f, 26.0f, 26.0f};

  // src: src/pages/logs.tsx:178-191
  toolbar_rect_ = {cx + 10.0f, cy + 44.0f, cw - 20.0f, 39.0f};
  seg_level_rect_ = {toolbar_rect_.x, toolbar_rect_.y + 5.0f, 95.0f, 28.0f};
  search_box_rect_ = {seg_level_rect_.x + seg_level_rect_.w + 6.0f, toolbar_rect_.y + 5.0f,
                      toolbar_rect_.w - (seg_level_rect_.w + 6.0f), 28.0f};
  if (search_field_) search_field_->layout_tree(search_box_rect_);
  if (search_menu_) search_menu_->layout_tree({0.0f, 0.0f, area.w, area.h});

  log_area_rect_ = {cx + 10.0f, cy + 87.0f, cw - 20.0f, ch - 95.0f};
  recompute_total_height();
  if (!desc_ && stick_to_bottom_) {
    scroll_y_ = max_scroll();
  } else {
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
  }
}

// src: src/pages/_layout/components/dialogs/logs-dialog.tsx:1-73
void LogViewerModal::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();
  set_opacity(fade_.value());
  if (fade_.hidden()) return;

  // src: src/pages/logs.tsx:106
  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  rt.fill_rect({0, 0, pane_w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  {
    const auto& svc = ThemeService::instance();
    const auto& pal = svc.palette();
    const auto& vars = svc.vars();
    const auto pp = svc.popup_card_paint();
    if (!pp.has_shadow && !pp.has_glow) {
      Brush shadow_b{Color4f(0, 0, 0, 0.22f)};
      rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5},
                    pp.radius, {}, &shadow_b);
    }
    ::clashwin::ui::draw_card(rt, card_rect_, pp);

    TextStyle title_ts;
    title_ts.size = 15.0f;
    title_ts.weight = 600;
    title_ts.color = pal.primary_text;
    title_ts.align_v = AlignV::Center;
    // src: locales/zh/logs.json:3
    rt.draw_text(tr("logs.page.title"), {card_rect_.x + 12.0f, card_rect_.y, btn_pause_rect_.x - (card_rect_.x + 16.0f), 44.0f},
                 title_ts);

    const Color4f action_hover =
        svc.is_light() ? Color4f::hex(0x000000, 0.04f)
                        : Color4f::hex(0xFFFFFF, 0.08f);
    Color4f x_bg = hover_close_x_ ? action_hover : Color4f(0, 0, 0, 0);
    Brush xb{x_bg};
    rt.round_rect(btn_close_x_rect_, 3.0f, Pen{Color4f(0, 0, 0, 0), 0.0f}, &xb);
    TextStyle x_ts;
    x_ts.size = 11.5f;
    x_ts.color = pal.primary_text;
    x_ts.align_h = AlignH::Center;
    x_ts.align_v = AlignV::Center;
    rt.draw_text(L"✕", btn_close_x_rect_, x_ts);

    rt.fill_rect({card_rect_.x, card_rect_.y + 44.0f, card_rect_.w, 1.0f},
                 Brush{vars.theme_border});

    const Color4f warn_c = svc.is_light() ? Color4f::hex(0xE65100)
                                          : Color4f::hex(0xFFD54F);
    auto draw_tool_btn = [&](const RectF& r, const std::wstring& label, bool hover,
                             bool active = false) {
      Color4f bg = active ? Color4f(0, 0, 0, 0)
                          : (hover ? action_hover : Color4f(0, 0, 0, 0));
      Brush b{bg};
      Pen p{active ? warn_c : Color4f(0, 0, 0, 0), active ? 1.0f : 0.0f};
      rt.round_rect(r, 4.0f, p, &b);
      TextStyle ts;
      ts.size = 11.0f;  // src: src/pages/logs.tsx:172
      ts.color = active ? warn_c : pal.primary_text;
      ts.align_h = AlignH::Center;
      ts.align_v = AlignV::Center;
      rt.draw_text(label, r, ts);
    };

    // src: src/pages/logs.tsx:118-152
    auto draw_icon_btn = [&](const RectF& r, const char* icon, bool hover, bool active,
                             bool flip) {
      Color4f bg = hover ? action_hover : Color4f(0, 0, 0, 0);
      Brush b{bg};
      rt.round_rect(r, r.w * 0.5f, Pen{Color4f(0, 0, 0, 0), 0.0f}, &b);
      IconW::paint_at(rt, icon,
                      {r.x + (r.w - 18.0f) * 0.5f, r.y + (r.h - 18.0f) * 0.5f, 18.0f, 18.0f},
                      active ? warn_c : pal.primary_text, flip);
    };
    draw_icon_btn(btn_pause_rect_, paused_ ? "playcircleoutline" : "pausecircleoutline",
                  hover_pause_, paused_, false);
    draw_icon_btn(btn_order_rect_, "swapvert", hover_order_, desc_, desc_);
    // src: locales/zh/shared.json:24
    draw_tool_btn(btn_clear_rect_, tr("shared.actions.clear"), hover_clear_);

    TextStyle lvl_ts;
    lvl_ts.size = 10.0f;
    lvl_ts.color = pal.secondary_text;
    lvl_ts.align_v = AlignV::Center;
    // src: locales/zh/shared.json:137-141
    std::wstring lvl_str;
    if (level_filter_ == "all") lvl_str = L"ALL";
    else if (level_filter_ == "debug") lvl_str = L"DEBUG";
    else if (level_filter_ == "info") lvl_str = L"INFO";
    else if (level_filter_ == "warn") lvl_str = L"WARN";
    else lvl_str = L"ERROR";
    Brush lb{vars.theme_input_bg};
    rt.round_rect(seg_level_rect_, 4.0f, {vars.theme_input_border, 1.0f}, &lb);
    lvl_ts.color = pal.primary_text;
    rt.draw_text(lvl_str, {seg_level_rect_.x + 8.0f, seg_level_rect_.y, seg_level_rect_.w - 22.0f, seg_level_rect_.h}, lvl_ts);
    TextStyle arrow_ts;
    arrow_ts.size = 8.5f;
    arrow_ts.color = pal.secondary_text;
    arrow_ts.align_h = AlignH::Right;
    arrow_ts.align_v = AlignV::Center;
    rt.draw_text(L"▼", {seg_level_rect_.x, seg_level_rect_.y, seg_level_rect_.w - 6.0f, seg_level_rect_.h}, arrow_ts);

    if (search_field_) {
      search_field_->apply_theme();
      // src: src/components/base/base-search-box.tsx:117 fontSize 11px
      search_field_->style_.size = 11.0f;
      search_field_->set_placeholder(tr("shared.placeholders.filter"));
    }
  }

  // src: src/components/log/log-item.tsx:8-14
  Brush log_bg{theme::ThemeService::instance().vars().theme_panel_bg};
  rt.round_rect(log_area_rect_, 4.0f,
                {theme::ThemeService::instance().vars().theme_border, 1.0f}, &log_bg);

  std::vector<const LogEntry*> visible_entries;
  visible_entries.reserve(logs_.size());
  if (desc_) {
    for (auto it = logs_.rbegin(); it != logs_.rend(); ++it) {
      if (matches_filter(*it)) visible_entries.push_back(&*it);
    }
  } else {
    for (const auto& item : logs_) {
      if (matches_filter(item)) visible_entries.push_back(&item);
    }
  }

  rt.push_clip(log_area_rect_);
  float y = log_area_rect_.y + 6.0f - scroll_y_;
  TextStyle log_ts;
  log_ts.size = 10.5f;
  // src: src/components/log/log-item.tsx:14
  log_ts.align_v = AlignV::Center;
  log_ts.wrap = true;
  log_ts.ellipsis = false;
  const auto& pal = theme::ThemeService::instance().palette();
  const std::wstring q_w = utf::utf8_to_utf16(search_query_);

  float x = log_area_rect_.x + 6.0f;
  float item_w = log_area_rect_.w - 12.0f;
  for (size_t i = 0; i < visible_entries.size(); ++i) {
    const auto* e = visible_entries[i];
    const std::wstring t_msg = utf::utf8_to_utf16(e->message);

    log_ts.size = 10.0f;
    log_ts.weight = static_cast<int>(Weight::Normal);
    log_ts.color = pal.primary_text;
    float msg_h = text_engine()->measure_text(t_msg, log_ts, item_w).h;
    if (msg_h < 16.0f) msg_h = 16.0f;
    const float item_h = 17.0f + msg_h + 5.0f;

    if (y + item_h >= log_area_rect_.y && y <= log_area_rect_.y + log_area_rect_.h) {
      const std::wstring t_time = utf::utf8_to_utf16(e->time);
      std::wstring t_type = utf::utf8_to_utf16(e->level);
      for (auto& c : t_type) c = towupper(c);
      Color4f type_c = (e->level == "error" || e->level == "err")
                           ? pal.error_color
                           : ((e->level == "warn" || e->level == "warning")
                                  ? pal.warning_color
                                  : pal.info_color);

      log_ts.size = 9.5f;
      log_ts.weight = static_cast<int>(Weight::Normal);
      log_ts.color = pal.secondary_text;
      draw_text_with_highlights(rt, text_engine(), t_time, q_w,
                                {x, y + 2.0f, item_w, 14.0f}, log_ts);

      float time_w = text_engine()->measure_text(t_time, log_ts, 0).w;
      log_ts.weight = static_cast<int>(Weight::Bold);
      log_ts.color = type_c;
      draw_text_with_highlights(rt, text_engine(), t_type, q_w,
                                {x + time_w + 8.0f, y + 2.0f, item_w - time_w - 8.0f, 14.0f},
                                log_ts);

      log_ts.size = 10.0f;
      log_ts.weight = static_cast<int>(Weight::Normal);
      log_ts.color = pal.primary_text;
      draw_text_with_highlights(rt, text_engine(), t_msg, q_w,
                                {x, y + 17.0f, item_w, msg_h}, log_ts);

      rt.fill_rect({x, y + item_h - 1.0f, item_w, 1.0f},
                   Brush{theme::ThemeService::instance().vars().theme_border});
    }
    y += item_h;
    if (y > log_area_rect_.y + log_area_rect_.h) break;
  }
  rt.pop_clip();
}

// src: src/pages/_layout/components/dialogs/logs-dialog.tsx:1-73
bool LogViewerModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (lvl_menu_ && lvl_menu_->visible()) {
    if (lvl_menu_->bounds().contains(pt.x, pt.y)) {
      MouseEvent child_e = e;
      child_e.pos.x -= lvl_menu_->bounds().x;
      child_e.pos.y -= lvl_menu_->bounds().y;
      return lvl_menu_->on_mouse(child_e);
    }
    if (e.type == MouseEventType::Up || e.type == MouseEventType::Down) {
      if (!seg_level_rect_.contains(pt.x, pt.y)) {
        lvl_menu_->set_visible(false);
        invalidate();
        return true;
      }
    }
  }

  if (e.type == MouseEventType::Move) {
    bool hx = btn_close_x_rect_.contains(pt.x, pt.y);
    bool hp = btn_pause_rect_.contains(pt.x, pt.y);
    bool ho = btn_order_rect_.contains(pt.x, pt.y);
    const bool hc = btn_clear_rect_.contains(pt.x, pt.y);
    if (hx != hover_close_x_ || hp != hover_pause_ || ho != hover_order_ || hc != hover_clear_) {
      hover_close_x_ = hx;
      hover_pause_ = hp;
      hover_order_ = ho;
      hover_clear_ = hc;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Wheel) {
    float delta = static_cast<float>(e.wheel_delta) / 120.0f * line_height_ * 3.0f;  // src: src/pages/logs.tsx:227
    set_scroll_y(scroll_y_ - delta);
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_x_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (btn_pause_rect_.contains(pt.x, pt.y)) {
      set_paused(!paused_);
      return true;
    }
    if (btn_order_rect_.contains(pt.x, pt.y)) {
      set_desc(!desc_);
      return true;
    }
    if (btn_clear_rect_.contains(pt.x, pt.y)) {
      clear_logs();
      return true;
    }
    if (seg_level_rect_.contains(pt.x, pt.y)) {
      // src: src/pages/logs.tsx:179-191
      if (lvl_menu_) {
        const bool opening = !lvl_menu_->visible();
        if (opening) {
          MenuStyle st;
          apply_menu_skin(st);
          // src: src/pages/_layout/utils/style-helpers.tsx:126 MenuItem 32px
          st.item_h = 32.0f;
          st.min_w = 100.0f;
          apply_menu_style_to_list(lvl_menu_, st);
          const float menu_w = 100.0f;
          const float menu_h = lvl_menu_->content_height() + 2.0f;
          float menu_x = std::max(card_rect_.x + 8.0f,
                                  std::min(seg_level_rect_.x, card_rect_.x + card_rect_.w - menu_w - 8.0f));
          float menu_y = (seg_level_rect_.bottom() + 2.0f + menu_h <= card_rect_.y + card_rect_.h - 8.0f)
                             ? (seg_level_rect_.bottom() + 2.0f)
                             : (seg_level_rect_.y - menu_h - 2.0f);
          lvl_menu_->layout_tree({menu_x, menu_y, menu_w, menu_h});
          lvl_menu_->set_z_order(1000);
        }
        lvl_menu_->set_visible(opening);
      }
      invalidate();
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }

  return true;
}

bool LogViewerModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
    close();
    return true;
  }
  return false;
}

std::wstring LogViewerModal::a11y_default_name() const {
  return tr("settings.mini.debugLogs");
}

A11ySubItem LogViewerModal::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  if (idx == 0) {
    sub.role = A11yRole::Button;
    sub.name = tr("layout.a11y.closeLogsDialog");
    sub.local_bounds = btn_close_x_rect_;
    sub.can_invoke = true;
  } else if (idx == 1) {
    sub.role = A11yRole::Button;
    sub.name = paused_ ? tr("shared.actions.resume") : tr("shared.actions.pause");
    sub.local_bounds = btn_pause_rect_;
    sub.can_invoke = true;
  } else if (idx == 2) {
    sub.role = A11yRole::Button;
    sub.name = desc_ ? tr("logs.actions.showAscending") : tr("logs.actions.showDescending");
    sub.local_bounds = btn_order_rect_;
    sub.can_invoke = true;
  } else if (idx == 3) {
    sub.role = A11yRole::Button;
    sub.name = tr("shared.actions.clear");
    sub.local_bounds = btn_clear_rect_;
    sub.can_invoke = true;
  } else if (idx == 4) {
    sub.role = A11yRole::ComboBox;
    sub.name = utf::utf8_to_utf16(level_filter_);
    sub.local_bounds = seg_level_rect_;
    sub.can_invoke = true;
    sub.expanded = (lvl_menu_ && lvl_menu_->visible());
    sub.value = utf::utf8_to_utf16(level_filter_);
  } else {
    sub.role = A11yRole::Edit;
    sub.name = tr("shared.placeholders.filter");
    sub.local_bounds = search_box_rect_;
    sub.focusable = true;
    sub.can_set_value = true;
    sub.value = utf::utf8_to_utf16(search_query_);
  }
  return sub;
}

bool LogViewerModal::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  switch (idx) {
    case 0: close(); return true;
    case 1: set_paused(!paused_); return true;
    case 2: set_desc(!desc_); return true;
    case 3: clear_logs(); return true;
    case 4:
      if (lvl_menu_) {
        lvl_menu_->set_visible(!lvl_menu_->visible());
        invalidate();
      }
      return true;
    default: return false;
  }
}

bool LogViewerModal::a11y_set_virtual_child_value(size_t idx, const std::wstring& v) {
  if (!open_ || !enabled() || idx != 5) return false;
  set_search_query(utf::utf16_to_utf8(v));
  return true;
}

}  // namespace clashwin::ui::scene
