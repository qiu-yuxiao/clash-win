// 容器布局实现（T10）：Column/Row/Stack——flex 子集两阶段布局。
#include "container.h"

#include <algorithm>

namespace clashwin::ui {

namespace {

// 主轴布置核心：返回各子在主轴上的位置与尺寸（pos 数组与 children 对齐）
struct Slot {
  float pos = 0, size = 0;
};

void distribute_main_axis(const std::vector<Widget*>& kids, float avail_main,
                          float spacing, MainAlign align, bool is_row,
                          std::vector<Slot>& slots) {
  const size_t n = kids.size();
  slots.resize(n);
  if (n == 0) return;

  // 1) 固定子取 measure 期望；flex 子也测出最小期望，arrange 时按权重扩
  float fixed_sum = 0, flex_weight_sum = 0;
  std::vector<SizeF> want(n);
  for (size_t i = 0; i < n; ++i) {
    if (!kids[i]->visible()) { want[i] = {}; continue; }
    float max_main = is_row ? avail_main : 1e6f;
    float max_cross = is_row ? 1e6f : avail_main;
    want[i] = kids[i]->measure(max_main, max_cross);
    if (kids[i]->flex_weight > 0) {
      flex_weight_sum += kids[i]->flex_weight;
    } else {
      fixed_sum += is_row ? want[i].w : want[i].h;
    }
  }
  int visible_cnt = 0;
  for (size_t i = 0; i < n; ++i)
    if (kids[i]->visible()) ++visible_cnt;
  float spacing_total =
      spacing * static_cast<float>(visible_cnt > 0 ? visible_cnt - 1 : 0);
  float free_space = avail_main - fixed_sum - spacing_total;
  if (free_space < 0) free_space = 0;

  // 2) flex 分配（不小于其最小期望）
  float flex_total = 0;
  for (size_t i = 0; i < n; ++i) {
    if (kids[i]->flex_weight > 0 && flex_weight_sum > 0) {
      slots[i].size = free_space * (kids[i]->flex_weight / flex_weight_sum);
      float min_main = is_row ? want[i].w : want[i].h;
      if (slots[i].size < min_main) slots[i].size = min_main;
      flex_total += slots[i].size;
    } else {
      slots[i].size = is_row ? want[i].w : want[i].h;
    }
  }

  // 3) 主轴起始（对齐）
  float content = fixed_sum + flex_total + spacing_total;
  float cursor = 0;
  switch (align) {
    case MainAlign::Start: cursor = 0; break;
    case MainAlign::End: cursor = avail_main - content; break;
    case MainAlign::Center: cursor = (avail_main - content) / 2; break;
    case MainAlign::SpaceBetween:
      cursor = 0;
      if (n > 1 && free_space > 0) {
        // 剩余自由空间全部进间隙
        float gap = free_space / static_cast<float>(n - 1);
        for (size_t i = 0; i < n; ++i) {
          slots[i].pos = cursor;
          cursor += slots[i].size + gap;
        }
        return;
      }
      break;
  }
  for (size_t i = 0; i < n; ++i) {
    slots[i].pos = cursor;
    cursor += slots[i].size + spacing;
  }
}

}  // namespace

// ---------- Column ----------
SizeF Column::on_measure(float max_w, float max_h) {
  const size_t n = child_count();
  float total_h = padding().v(), max_want = 0;
  bool any_visible = false;
  for (size_t i = 0; i < n; ++i) {
    if (!child(i)->visible()) continue;
    SizeF s = child(i)->measure(max_w - padding().h(), 1e6f);
    total_h += s.h + (any_visible ? spacing : 0.0f);
    any_visible = true;
    if (s.w > max_want) max_want = s.w;
  }
  // flex 子至少占一份自由空间（max_h 未知时给 0 尺寸是不对的——期望值即
  // 固定子之和，flex 部分在 arrange 展开）
  SizeF out{max_want + padding().h(), total_h};
  if (max_h > 0 && out.h > max_h) out.h = max_h;
  if (max_w > 0 && out.w > max_w) out.w = max_w;
  return out;
}

void Column::on_arrange(const RectF& area) {
  // 【坐标约定】area 为父局部坐标；子 layout_tree 传本 widget 局部坐标
  //（global_bounds 由父链累加求全局，禁止在此叠加 area.x/y）
  RectF content{padding().l, padding().t, area.w - padding().h(),
                area.h - padding().v()};
  std::vector<Widget*> kids;
  for (size_t i = 0; i < child_count(); ++i)
    if (child(i)->visible()) kids.push_back(child(i));
  std::vector<Slot> slots;
  distribute_main_axis(kids, content.h, spacing, main_align, false, slots);
  for (size_t i = 0; i < kids.size(); ++i) {
    float x = 0, w = content.w;
    SizeF want = kids[i]->measure(content.w, 1e6f);
    switch (cross_align) {
      case CrossAlign::Start: x = 0; w = want.w; break;
      case CrossAlign::Center: x = (content.w - want.w) / 2; w = want.w; break;
      case CrossAlign::End: x = content.w - want.w; w = want.w; break;
      case CrossAlign::Stretch: x = 0; w = content.w; break;
    }
    if (x < 0) x = 0;
    kids[i]->layout_tree({content.x + x, content.y + slots[i].pos, w,
                          slots[i].size});
  }
}

// ---------- Row ----------
SizeF Row::on_measure(float max_w, float max_h) {
  const size_t n = child_count();
  float total_w = padding().h(), max_want = 0;
  bool any_visible = false;
  for (size_t i = 0; i < n; ++i) {
    if (!child(i)->visible()) continue;
    SizeF s = child(i)->measure(1e6f, max_h - padding().v());
    total_w += s.w + (any_visible ? spacing : 0.0f);
    any_visible = true;
    if (s.h > max_want) max_want = s.h;
  }
  SizeF out{total_w, max_want + padding().v()};
  if (max_w > 0 && out.w > max_w) out.w = max_w;
  if (max_h > 0 && out.h > max_h) out.h = max_h;
  return out;
}

void Row::on_arrange(const RectF& area) {
  // 【坐标约定】同 Column：子布局一律本 widget 局部坐标
  RectF content{padding().l, padding().t, area.w - padding().h(),
                area.h - padding().v()};
  std::vector<Widget*> kids;
  for (size_t i = 0; i < child_count(); ++i)
    if (child(i)->visible()) kids.push_back(child(i));
  std::vector<Slot> slots;
  distribute_main_axis(kids, content.w, spacing, main_align, true, slots);
  for (size_t i = 0; i < kids.size(); ++i) {
    float y = 0, h = content.h;
    SizeF want = kids[i]->measure(1e6f, content.h);
    switch (cross_align) {
      case CrossAlign::Start: y = 0; h = want.h; break;
      case CrossAlign::Center: y = (content.h - want.h) / 2; h = want.h; break;
      case CrossAlign::End: y = content.h - want.h; h = want.h; break;
      case CrossAlign::Stretch: y = 0; h = content.h; break;
    }
    if (y < 0) y = 0;
    kids[i]->layout_tree({content.x + slots[i].pos, content.y + y,
                          slots[i].size, h});
  }
}

// ---------- Stack ----------
SizeF Stack::on_measure(float max_w, float max_h) {
  float mw = 0, mh = 0;
  for (size_t i = 0; i < child_count(); ++i) {
    if (!child(i)->visible()) continue;
    SizeF s = child(i)->measure(max_w, max_h);
    if (s.w > mw) mw = s.w;
    if (s.h > mh) mh = s.h;
  }
  return {mw + padding().h(), mh + padding().v()};
}

void Stack::on_arrange(const RectF& area) {
  // AbsoluteOverlay：全部铺满内容区（覆盖层/弹窗宿主语义）
  // 【坐标约定】子 layout_tree 传本 widget 局部坐标
  RectF content{padding().l, padding().t, area.w - padding().h(),
                area.h - padding().v()};
  for (size_t i = 0; i < child_count(); ++i)
    if (child(i)->visible()) child(i)->layout_tree(content);
}

}  // namespace clashwin::ui
