// 连接面板实现（T14）。
#include "connections_view.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <memory>

#include "../theme/theme.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01

namespace clashwin::ui::scene {

using theme::ThemeService;

namespace utf = clashwin::utf;

// chains 分类色（← MUI 语义色映射到 defaultTheme：Proxy=primary/Direct=
// success/Reject=error；Mini 原版为文字分类，色点由本组件自绘）
Color4f chains_color(const std::string& cat) {
  if (cat == "Proxy") return ThemeService::instance().primary();
  if (cat == "Direct") return Color4f::hex(0x06943D);
  if (cat == "Reject") return Color4f::hex(0xFF3B30);
  return Color4f::hex(0x3C3C43, 0.6f);
}

// ---------- ConnRowWidget ----------
void ConnRowWidget::bind(const ConnRow& row) {
  row_ = row;
  invalidate();
}

void ConnRowWidget::on_paint(IRenderTarget& rt) {
  const auto& svc = ThemeService::instance();
  // 【PAINTFIX】局部坐标 {0,0,w,h}（同 title_bar：bounds() 会二次叠加原点）
  RectF r{0, 0, bounds().w, bounds().h};
  if (hover_) rt.fill_rect(r, Brush{Color4f::hex(0x000000, 0.04f)});  // src: MUI action.hover rgba(0,0,0,.04)

  // 协议色点（chains 分类；8px 圆点 x=4）
  Brush dot{chains_color(row_.chains_cat)};
  rt.round_rect({r.x + 4, r.y + r.h / 2 - 4, 8, 8}, 4, {}, &dot);

  TextStyle ts;
  ts.size = 11;
  ts.align_v = AlignV::Center;
  ts.ellipsis = true;
  // host:port
  ts.color = svc.palette().primary_text;
  rt.draw_text(utf::utf8_to_utf16(row_.host), {r.x + 16, r.y, 130, r.h}, ts);
  // chains 分类文字
  ts.color = chains_color(row_.chains_cat);
  rt.draw_text(utf::utf8_to_utf16(row_.chains_cat), {r.x + 150, r.y, 60, r.h},
               ts);
  // 速率列（cur 增量）
  ts.color = Color4f::hex(0x006064);  // src: mini-traffic-panel.tsx:122 --download-text
  rt.draw_text(utf::utf8_to_utf16(
                   row_.cur_download > 0
                       ? std::to_string(static_cast<long long>(row_.cur_download))
                       : std::string("-")),
               {r.right() - 90, r.y, 40, r.h}, ts);
  ts.color = Color4f::hex(0x996E00);  // src: index.scss:311 --upload-text hsl(38,100%,30%)
  rt.draw_text(utf::utf8_to_utf16(
                   row_.cur_upload > 0
                       ? std::to_string(static_cast<long long>(row_.cur_upload))
                       : std::string("-")),
               {r.right() - 46, r.y, 40, r.h}, ts);

  // 关闭 ✕（右侧 16px 热区）
  if (hover_) {
    TextStyle xs;
    xs.font_family = L"Segoe Fluent Icons";
    xs.size = 8;
    xs.align_h = AlignH::Center;
    xs.align_v = AlignV::Center;
    xs.color = close_hover_ ? Color4f::hex(0xFF3B30)  // src: _theme.tsx:8 error_color
                            : Color4f::hex(0x000000, 0.5f);  // 豁免: 关闭图标缺省 50% 黑
    rt.draw_text(L"\uE711", {r.right() - 18, r.y, 18, r.h}, xs);
  }
}

bool ConnRowWidget::on_mouse(const MouseEvent& e) {
  if (e.type == MouseEventType::Move) {
    hover_ = true;
    close_hover_ = e.pos.x > bounds().w - 18;
    invalidate();
    return true;
  }
  if (e.type == MouseEventType::Leave) {
    hover_ = false;
    close_hover_ = false;
    invalidate();
    return true;
  }
  if (e.type == MouseEventType::Up) {
    if (close_hover_) {
      if (on_close) on_close();
    } else if (on_click) {
      on_click();
    }
    return true;
  }
  return false;
}

// ---------- ConnectionsView ----------
ConnectionsView::ConnectionsView(Widget* parent) : Widget(parent) {
  auto bar = std::make_unique<Row>();
  Row* bar_p = bar.get();
  bar_p->set_padding(4, 0, 4, 0);  // 豁免: Mini 无对应物 — 工具条内距源码用 CSS 类无字面量（出处: connections 视图无 padding 常量）
  bar_p->spacing = 4;
  add_child(std::move(bar));

  auto badge = std::make_unique<Label>();
  badge_ = badge.get();
  badge_->style.size = 11;  // ← 连接表徽标字号 11（MUI body2 小号）
  badge_->style.weight = static_cast<int>(Weight::Bold);
  badge_->set_text(L"0");
  bar_p->add_child(std::move(badge));

  auto search = std::make_unique<TextField>();
  search_ = search.get();
  search_->flex_weight = 1;
  search_->set_placeholder(L"过滤连接...");
  // 清空按钮（opt-in）：src: connections-panel.tsx:182 BaseSearchBox
  search_->set_clear_button(true, tr("shared.placeholders.resetInput"));
  search_->on_text_changed = [this](const std::wstring& t) {
    set_filter(clashwin::utf::utf16_to_utf8(t));
  };
  bar_p->add_child(std::move(search));

  auto list = std::make_unique<ScrollList>();
  list_ = list.get();
  list_->flex_weight = 1;
  list_->themed_ = true;
  add_child(std::move(list));
}

ConnectionsView::~ConnectionsView() = default;

void ConnectionsView::bind(ConnectionData* data) {
  data_ = data;
  if (!data_) return;
  data_->subscribe([this]() {
    badge_->set_text(std::to_wstring(data_->active_count()));
    rebuild_visible();
    sync_list();
  });
  badge_->set_text(std::to_wstring(data_->active_count()));
  rebuild_visible();
  sync_list();
}

void ConnectionsView::set_filter(const std::string& text) {
  std::string lower;
  for (char c : text)
    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (filter_ == lower) return;
  filter_ = lower;
  rebuild_visible();
  sync_list();
}

void ConnectionsView::rebuild_visible() {
  visible_.clear();
  if (!data_) return;
  const auto& rows = data_->active();
  for (size_t i = 0; i < rows.size(); ++i) {
    if (filter_.empty()) {
      visible_.push_back(i);
      continue;
    }
    std::string lower;
    for (char c : rows[i].host)
      lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find(filter_) != std::string::npos) visible_.push_back(i);
  }
}

void ConnectionsView::sync_list() {
  if (!data_ || !list_) return;
  auto* nd = data_;
  auto* self = this;
  list_->set_content(
      visible_.size(), kConnRowH,
      [nd, self](int index) {
        auto row = std::make_unique<ConnRowWidget>();
        size_t v = self->visible_[static_cast<size_t>(index)];
        row->bind(nd->active()[v]);
        row->on_click = [self, id = nd->active()[v].id]() {
          if (self->on_row_click) self->on_row_click(id);
        };
        row->on_close = [self, id = nd->active()[v].id]() {
          if (self->close_connection) self->close_connection(id);
        };
        return row;
      },
      5);
  list_->on_arrange(list_->bounds());
}

void ConnectionsView::on_arrange(const RectF& area) {
  set_bounds(area);
  float bar_h = 24;
  child(0)->layout_tree({area.x, area.y, area.w, bar_h});
  list_->layout_tree({area.x, area.y + bar_h, area.w,
                      std::max(area.h - bar_h, 0.0f)});
}

void ConnectionsView::on_paint(IRenderTarget& rt) {
  rt.fill_rect(bounds(),
               Brush{ThemeService::instance().palette().background_color});
}

}  // namespace clashwin::ui::scene
