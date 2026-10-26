// Segmented 实现（T10）。
#include "segmented.h"

#include <algorithm>

#include "theme/theme.h"

namespace clashwin::ui {

void Segmented::set_options(std::vector<std::wstring> opts, int sel) {
  options_ = std::move(opts);
  selected_ = sel;
  invalidate();
}

void Segmented::set_selected(int i) {
  if (i < 0 || i >= static_cast<int>(options_.size()) || i == selected_) return;
  selected_ = i;
  invalidate();
  if (on_change) on_change(selected_);
}

SizeF Segmented::on_measure(float max_w, float max_h) {
  ITextEngine* e = text_engine();
  float per = 0, text_h = 0;
  if (e) {
    for (const auto& o : options_) {
      SizeF s = e->measure_text(o, style_, 0);
      if (s.w > per) per = s.w;
      if (s.h > text_h) text_h = s.h;
    }
  }
  float w = static_cast<float>(options_.size()) * (per + item_padding_h * 2) +
            padding().h();
  float h = std::max(24.0f, text_h + 6) + padding().v();
  if (max_w > 0 && w > max_w) w = max_w;
  if (max_h > 0 && h > max_h) h = max_h;
  return {w, h};
}

int Segmented::hit_segment(const PointF& local) const {
  if (options_.empty() || bounds().w <= 0) return -1;
  float seg_w = bounds().w / static_cast<float>(options_.size());
  int idx = static_cast<int>(local.x / seg_w);
  if (idx < 0 || idx >= static_cast<int>(options_.size())) return -1;
  return idx;
}

bool Segmented::on_mouse(const MouseEvent& e) {
  if (e.type == MouseEventType::Down) {
    int idx = hit_segment(e.pos);
    if (idx >= 0) {
      pressed_ = idx;
      return true;
    }
  } else if (e.type == MouseEventType::Up) {
    int idx = hit_segment(e.pos);
    pressed_ = -1;
    if (idx >= 0) {
      set_selected(idx);
      return true;
    }
  }
  return false;
}

void Segmented::on_paint(IRenderTarget& rt) {
  RectF r{0, 0, bounds().w, bounds().h};
  Color4f bg_c = bg_, act_bg = selected_bg_, act_txt = selected_text_color_,
         txt_c = text_color_, act_border{};
  float act_radius = radius_ - 2;
  if (themed_) {
    // T12：ThemeService 查表（← get3DSegmented* 三函数）
    auto p = theme::ThemeService::instance().segmented_paint();
    bg_c = p.container_bg;
    act_bg = p.active_bg;
    act_txt = p.active_text;
    act_border = p.active_border;
    act_radius = p.active_radius;
    txt_c = p.text;
  }
  Brush bg{bg_c};
  rt.round_rect(r, radius_, themed_ ? Pen{bg_c.a > 0 && act_border.a > 0
                                                ? act_border
                                                : Color4f(0, 0, 0, 0), 1}
                                    : Pen{Color4f(0, 0, 0, 0), 0},
                &bg);
  if (options_.empty()) return;
  float seg_w = r.w / static_cast<float>(options_.size());
  for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
    RectF seg{r.x + seg_w * i, r.y, seg_w, r.h};
    bool sel = i == selected_;
    if (sel) {
      Brush sel_bg{act_bg};
      rt.round_rect({seg.x + 2, seg.y + 2, seg.w - 4, seg.h - 4}, act_radius,
                    themed_ && act_border.a > 0 ? Pen{act_border, 1}
                                                : Pen{Color4f(0, 0, 0, 0), 0},
                    &sel_bg);
    }
    TextStyle ts = style_;
    ts.color = sel ? act_txt : txt_c;
    ts.weight = sel ? static_cast<int>(Weight::Bold) : static_cast<int>(Weight::Normal);
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    ts.ellipsis = true;
    rt.draw_text(options_[i], seg, ts);
  }
}

}  // namespace clashwin::ui
