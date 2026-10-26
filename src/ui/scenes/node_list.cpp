// 节点列表实现（T13）：过滤条 + 虚拟滚动 + 行卡绘制（对照 proxy-item.tsx）。
#include "node_list.h"

#include <algorithm>
#include <cctype>
#include <memory>

#include "../theme/theme.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

using theme::ThemeService;  // T12 主题服务（scene 层全量使用）

namespace utf = clashwin::utf;

namespace {
Color4f with_alpha_bg(const Color4f& c, float a) { return {c.r, c.g, c.b, a}; }
}  // namespace

// ← formatDelayColor → MUI 色名 → defaultTheme 色值（light 主口径；
//   warning.dark 为 darken 近似，light 用 main）
Color4f delay_tier_color(DelayTier tier) {
  auto& svc = ThemeService::instance();
  switch (tier) {
    case DelayTier::Success: return Color4f::hex(0x06943D);  // success.main
    case DelayTier::Mid: return svc.primary();  // primary.main
    case DelayTier::Slow: return Color4f::hex(0xFF9500);     // warning.main
    case DelayTier::Timeout: return Color4f::hex(0xFF3B30);  // error.main
    default: return Color4f::hex(0x3C3C43, 0.60f);  // text.secondary（未测灰）
  }
}

// ---------- NodeRowWidget ----------
void NodeRowWidget::bind(bool is_group_head, const std::string& type,
                         const std::string& name, int delay, bool selected) {
  group_head_ = is_group_head;
  type_ = type;
  name_ = name;
  delay_ = delay;
  selected_ = selected;
  invalidate();
}

void NodeRowWidget::on_paint(IRenderTarget& rt) {
  const auto& svc = ThemeService::instance();
  RectF r = bounds();
  const auto& pal = svc.palette();
  const auto& vars = svc.vars();

  // 背景：分组头/选中 = alpha(primary,0.15)；奇偶行 rgba(0,120,215,.04)
  Color4f bg{0, 0, 0, 0};
  if (group_head_ || selected_)
    bg = with_alpha_bg(pal.primary_color, 0.15f);
  else if (index_in_group % 2 != 0)
    bg = Color4f::hex(0x0078D7, 0.04f);
  if (hover_ && !group_head_) bg = Color4f::hex(0x0078D7, 0.08f);
  if (bg.a > 0) rt.fill_rect(r, Brush{bg});

  // 选中左边框 3px primary（← '&.Mui-selected' borderLeft）
  if (selected_ && !group_head_)
    rt.fill_rect({r.x, r.y, kNodeSelectBorderW, r.h}, Brush{pal.primary_color});

  Brush divider{vars.divider_color};
  float x = r.x;
  float text_h = r.h;

  // 左列：类型（11px 灰大写右对齐、右边框 2px divider）
  RectF type_r{x, r.y, kNodeTypeColW, r.h};
  TextStyle ts;
  ts.size = 11;
  ts.color = Color4f::hex(0x3C3C43, 0.60f);  // text.secondary
  ts.align_v = AlignV::Center;
  ts.align_h = AlignH::Right;
  ts.ellipsis = true;
  rt.draw_text(utf::utf8_to_utf16(type_),
               {type_r.x, type_r.y, type_r.w - 4, text_h}, ts);
  rt.fill_rect({type_r.right() - kNodeBorderW, r.y, kNodeBorderW, r.h},
               divider);
  x = type_r.right();

  // 名称区（12px text.primary；selected bold；分组头=当前节点 primary 橙字）
  RectF name_r{x, r.y, r.w - kNodeTypeColW - kNodeDelayColW, r.h};
  TextStyle ns;
  ns.size = 12;
  ns.weight = (selected_ || group_head_) ? static_cast<int>(Weight::Bold)
                                         : static_cast<int>(Weight::Normal);
  ns.color = (group_head_ || selected_) ? pal.primary_color : pal.primary_text;
  ns.align_v = AlignV::Center;
  ns.ellipsis = true;
  rt.draw_text(utf::utf8_to_utf16(name_),
               {name_r.x + 8, name_r.y, name_r.w - 16, text_h}, ns);
  rt.fill_rect({name_r.right() - kNodeBorderW, r.y, kNodeBorderW, r.h},
               divider);

  // 右延迟列 65px（11px bold600 居中、五档色；分组头不显示）
  if (!group_head_) {
    RectF delay_r{name_r.right(), r.y, kNodeDelayColW, r.h};
    TextStyle ds;
    ds.size = 11;
    ds.weight = 600;
    ds.color = delay_tier_color(delay_tier(delay_));
    ds.align_h = AlignH::Center;
    ds.align_v = AlignV::Center;
    rt.draw_text(format_delay(delay_), delay_r, ds);
  }

  // 行底边框 2px divider
  rt.fill_rect({r.x, r.bottom() - kNodeBorderW, r.w, kNodeBorderW}, divider);
}

bool NodeRowWidget::on_mouse(const MouseEvent& e) {
  if (e.type == MouseEventType::Move) {
    hover_ = true;
    return true;
  }
  if (e.type == MouseEventType::Leave) {
    hover_ = false;
    invalidate();
    return true;
  }
  if (e.type == MouseEventType::Up) {
    if (on_click) on_click();
    return true;
  }
  return false;
}

// ---------- NodeList ----------
NodeList::NodeList(Widget* parent) : Widget(parent) {
  auto bar = std::make_unique<Row>();  // 过滤工具条 34px
  Row* bar_p = bar.get();
  bar_p->set_padding(4, 0, 4, 0);
  bar_p->spacing = 4;
  bar_p->set_visible(true);
  add_child(std::move(bar));

  // ⚡ 触发组测速
  auto bolt = std::make_unique<IconW>();
  bolt->set_icon("bolt");
  bolt->size_ = 14;
  bolt->flex_weight = 0;
  bolt->on_click = [this] {
    if (data_) data_->test_group();
  };
  bar_p->add_child(std::move(bolt));
  // ↓ 排序切换
  auto sort = std::make_unique<IconW>();
  sort->set_icon("arrowdownward");
  sort->size_ = 14;
  sort->on_click = [this] { toggle_sort(); };
  bar_p->add_child(std::move(sort));
  // 搜索框（"过滤节点..."）
  auto search = std::make_unique<TextField>();
  search_ = search.get();
  search_->flex_weight = 1;
  search_->set_placeholder(L"过滤节点...");  // ← i18n proxies.filter_ph 同文案
  search_->on_text_changed = [this](const std::wstring& t) {
    set_filter(clashwin::utf::utf16_to_utf8(t));
  };
  bar_p->add_child(std::move(search));

  // 虚拟滚动列表（行高 39）
  auto list = std::make_unique<ScrollList>();
  list_ = list.get();
  list_->flex_weight = 1;
  list_->themed_ = true;
  add_child(std::move(list));
}

NodeList::~NodeList() = default;

void NodeList::bind(NodeData* data) {
  data_ = data;
  if (!data_) return;
  data_->subscribe([this]() {
    rebuild_visible();
    sync_list();
  });
  rebuild_visible();
  sync_list();
}

void NodeList::set_group_title(const std::wstring& title) {
  group_title_ = title;
  rebuild_visible();
  sync_list();
}

void NodeList::set_filter(const std::string& text) {
  std::string lower;
  lower.reserve(text.size());
  for (char c : text)
    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (filter_ == lower) return;
  filter_ = lower;
  rebuild_visible();
  sync_list();
}

void NodeList::toggle_sort() {
  sort_by_delay_ = !sort_by_delay_;
  rebuild_visible();
  sync_list();
}

void NodeList::rebuild_visible() {
  visible_.clear();
  if (!data_) return;
  const auto& rows = data_->rows();
  // 分组头恒在首位（← use-render-list 组头行）
  visible_.push_back(-1);
  std::vector<int> idx;
  idx.reserve(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) {
    if (filter_.empty()) {
      idx.push_back(static_cast<int>(i));
      continue;
    }
    // name 子串大小写不敏感
    std::string lower;
    for (char c : rows[i].name)
      lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find(filter_) != std::string::npos)
      idx.push_back(static_cast<int>(i));
  }
  if (sort_by_delay_) {
    const auto& all_rows = data_->rows();
    std::stable_sort(idx.begin(), idx.end(), [this, &all_rows](int a, int b) {
      int da = data_->row_delay(all_rows[static_cast<size_t>(a)].name);
      int db = data_->row_delay(all_rows[static_cast<size_t>(b)].name);
      // Unknown(-1)/testing(-2) 沉底；其余升序
      auto key = [](int d) { return d < 0 ? 1 << 30 : d; };
      return key(da) < key(db);
    });
  }
  visible_.insert(visible_.end(), idx.begin(), idx.end());
}

void NodeList::sync_list() {
  if (!data_ || !list_) return;
  auto* nd = data_;
  auto* self = this;
  list_->set_content(
      visible_.size(), kNodeRowH,
      [nd, self](int index) {
        auto row = std::make_unique<NodeRowWidget>();
        int v = self->visible_row(static_cast<size_t>(index));
        if (v == -1) {
          // 分组头：组名 + 当前节点（橙浅底）
          row->bind(true, utf::utf16_to_utf8(self->group_title_), nd->now(),
                    -1, false);
        } else if (v >= 0) {
          const auto& r = nd->rows()[static_cast<size_t>(v)];
          row->bind(false, r.type, r.name, nd->row_delay(r.name),
                    r.name == nd->now());
          row->index_in_group = v;
          row->on_click = [self, name = r.name]() {
            // 失败回弹：select 返回 false 时不更新选中（NodeData.now_ 未变）
            if (self->select_node) self->select_node(name);
          };
        }
        return row;
      },
      5);
  list_->on_arrange(list_->bounds());
}

void NodeList::on_arrange(const RectF& area) {
  set_bounds(area);
  // 上过滤条 34px 固定，列表撑满
  float list_h = area.h - kFilterBarH;
  if (list_h < 0) list_h = 0;
  child(0)->layout_tree({area.x, area.y, area.w, kFilterBarH});
  list_->layout_tree({area.x, area.y + kFilterBarH, area.w, list_h});
}

void NodeList::on_paint(IRenderTarget& rt) {
  rt.fill_rect(bounds(), Brush{ThemeService::instance().palette().background_color});
}

}  // namespace clashwin::ui::scene
