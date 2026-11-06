// ScrollList 实现（T10）：虚拟滚动核心。
#include "scroll_list.h"

#include <algorithm>
#include <cmath>

#include "theme/theme.h"

namespace clashwin::ui {

void ScrollList::set_content(int count, float item_height, Factory factory,
                             int overscan) {
  item_count_ = count;
  item_height_ = item_height;
  factory_ = std::move(factory);
  overscan_ = overscan;
  offset_ = 0;
  rows_.clear();
  // 数据晚于布局到达（如抽屉打开时才 reload）时立即物化，不等下一次 arrange
  if (bounds().h > 0) {
    sync_children(bounds().h);
    float sb_w = show_scrollbar_ ? scrollbar_w_ : 0.0f;
    for (auto& [idx, w] : rows_)
      w->layout_tree({0, idx * item_height_ - offset_, bounds().w - sb_w,
                      item_height_});
  }
  invalidate();
}

float ScrollList::max_scroll() const {
  float content = item_count_ * item_height_;
  float vh = bounds().h;
  return content > vh ? content - vh : 0;
}

void ScrollList::set_scroll_offset(float v) {
  float m = max_scroll();
  v = v < 0 ? 0 : (v > m ? m : v);
  if (offset_ == v) return;
  offset_ = v;
  invalidate();
  if (on_scroll) on_scroll();
}

std::vector<int> ScrollList::materialized_indices() const {
  std::vector<int> out;
  out.reserve(rows_.size());
  for (const auto& [idx, w] : rows_) out.push_back(idx);
  std::sort(out.begin(), out.end());
  return out;
}

Widget* ScrollList::materialized_row(int index) const {
  auto it = rows_.find(index);
  return it != rows_.end() ? it->second.get() : nullptr;
}

SizeF ScrollList::on_measure(float max_w, float max_h) {
  // viewport 由父给（flex/stretch）；固有期望 = 一屏 item_height*8
  (void)max_w;
  float h = max_h > 0 && max_h < 1e5 ? max_h : item_height_ * 8;
  return {max_w > 0 ? max_w : 100, h};
}

void ScrollList::sync_children(float viewport_h) {
  if (item_height_ <= 0 || !factory_) return;
  int first = first_visible(viewport_h) - overscan_;
  int last = static_cast<int>((offset_ + viewport_h) / item_height_) + overscan_;
  if (first < 0) first = 0;
  if (last > item_count_ - 1) last = item_count_ - 1;
  if (last < first) last = first - 1;

  // 卸载窗口外的物化行（React unmount 等价）
  std::vector<int> stale;
  for (const auto& [idx, w] : rows_)
    if (idx < first || idx > last) stale.push_back(idx);
  for (int idx : stale) rows_.erase(idx);

  // 物化窗口内缺失行
  for (int i = first; i <= last; ++i) {
    if (rows_.count(i)) continue;
    if (auto w = factory_(i)) {
      w->set_parent(this);
      rows_.emplace(i, std::move(w));
    }
  }
}

void ScrollList::on_arrange(const RectF& area) {
  set_bounds(area);
  if (bounds().h > 0) {
    set_scroll_offset(offset_);  // clamp（内容变短时回弹）
    sync_children(area.h);
    float sb_w = show_scrollbar_ ? scrollbar_w_ : 0.0f;
    for (auto& [idx, w] : rows_) {
      float y = idx * item_height_ - offset_;
      w->layout_tree({0, y, area.w - sb_w, item_height_});
    }
  }
}

bool ScrollList::on_mouse(const MouseEvent& e) {
  if (e.type == MouseEventType::Wheel) {
    if (!enabled()) return true;
    // Windows：向下滚 delta 为负 → offset 应增大（与 utility_modals 口径一致）
    float lines = -static_cast<float>(e.wheel_delta) / 120.0f * 3.0f;
    set_scroll_offset(offset_ + lines * item_height_);
    sync_children(bounds().h);
    // 行位置随 offset 更新
    float sb_w = show_scrollbar_ ? scrollbar_w_ : 0.0f;
    for (auto& [idx, w] : rows_) {
      float y = idx * item_height_ - offset_;
      w->layout_tree({0, y, bounds().w - sb_w, item_height_});
    }
    return true;
  }
  return false;
}

void ScrollList::on_paint(IRenderTarget& rt) {
  RectF g = global_bounds();  // 行 paint 需要全局 clip（行 bounds 是本控件局部）
  rt.push_clip({0, 0, bounds().w, bounds().h});
  // 行子项绘制（paint 只处理直接子——rows_ 非常规 children，手动递归）
  for (auto& [idx, w] : rows_) w->paint(rt, g);
  // 滚动条（比例 thumb）
  if (show_scrollbar_) {
    float m = max_scroll();
    if (m > 0 && bounds().h > 0) {
      float vh = bounds().h;
      float content = item_count_ * item_height_;
      float thumb_h = std::max(vh * vh / content, 20.0f);
      float thumb_y = (offset_ / m) * (vh - thumb_h);
      Brush sb{themed_ ? theme::ThemeService::instance().vars().scrollbar_thumb
                       : scrollbar_};
      rt.round_rect({bounds().w - scrollbar_w_ - 1, thumb_y, scrollbar_w_,
                     thumb_h}, scrollbar_w_ / 2, {}, &sb);
    }
  }
  rt.pop_clip();
}

Widget* ScrollList::hit_test(PointF global) {
  if (!visible() || !enabled()) return nullptr;
  RectF g = global_bounds();
  if (!g.contains(global.x, global.y)) return nullptr;
  for (auto& [idx, w] : rows_) {
    if (w) {
      if (Widget* hit = w->hit_test(global)) return hit;
    }
  }
  return Widget::hit_test(global);
}

}  // namespace clashwin::ui
