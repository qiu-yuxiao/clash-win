// Segmented 实现（T10）。
#include "segmented.h"

#include <algorithm>

#include "i18n/i18n.h"
#include "i18n_text.h"
#include "soft_shadow.h"
#include "theme/theme.h"

namespace clashwin::ui {

namespace {
// 未接主题时（themed_=false 且 gold_3d_=true）的 retro-3d 选中块渐变兜底。
// src: button-styles.ts:1006-1007
const GradientStop kRetroActiveGrad[6] = {
    {0.00f, Color4f::hex(0xFFE082)}, {0.20f, Color4f::hex(0xFFD54F)},
    {0.45f, Color4f::hex(0xFFC107)}, {0.70f, Color4f::hex(0xFFB300)},
    {0.85f, Color4f::hex(0xD4AF37)}, {1.00f, Color4f::hex(0xB8860B)}};
}  // namespace

void Segmented::set_options(std::vector<std::wstring> opts, int sel) {
  options_ = std::move(opts);
  selected_ = sel;
  invalidate();
}

void Segmented::set_options_i18n(std::vector<const char*> keys, int sel) {
  option_keys_.assign(keys.begin(), keys.end());
  std::vector<std::wstring> opts;
  opts.reserve(keys.size());
  for (const char* k : keys) opts.push_back(tr(k));
  set_options(std::move(opts), sel);
  options_revision_ = clashwin::i18n::I18n::instance().revision();
}

void Segmented::set_tooltips_i18n(std::vector<const char*> keys) {
  tooltip_keys_.assign(keys.begin(), keys.end());
  tooltips_.clear();
  for (const char* k : keys) tooltips_.push_back(tr(k));
}

std::wstring Segmented::tooltip_text() const {
  if (hover_ >= 0 && hover_ < static_cast<int>(tooltip_keys_.size()))
    return tr(tooltip_keys_[static_cast<size_t>(hover_)].c_str());
  return hover_ >= 0 && hover_ < static_cast<int>(tooltips_.size())
             ? tooltips_[static_cast<size_t>(hover_)]
             : std::wstring();
}

void Segmented::set_selected(int i) {
  if (i < 0 || i >= static_cast<int>(options_.size()) || i == selected_) return;
  selected_ = i;
  invalidate();
  if (on_change) on_change(selected_);
}

void Segmented::select_silent(int i) {
  if (i < 0 || i >= static_cast<int>(options_.size()) || i == selected_) return;
  selected_ = i;
  invalidate();
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
  if (e.type == MouseEventType::Leave) {
    if (hover_ != -1) { hover_ = -1; invalidate(); }
    return false;
  }
  if (e.type == MouseEventType::Move) {
    int idx = hit_segment(e.pos);
    if (hover_ != idx) {
      hover_ = idx;
      invalidate();  // hover 换段 → Tooltip 文案随之切换
    }
    return true;
  }
  if (e.type == MouseEventType::Down) {
    int idx = hit_segment(e.pos);
    if (idx >= 0) {
      pressed_ = idx;
      invalidate();
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
  // F-D01：语言切换 → 重取分段文案（← _layout.tsx:478-483 重渲染）
  if (!option_keys_.empty()) {
    const uint64_t rev = clashwin::i18n::I18n::instance().revision();
    if (rev != options_revision_) {
      options_revision_ = rev;
      for (size_t i = 0; i < option_keys_.size() && i < options_.size(); ++i)
        options_[i] = tr(option_keys_[i].c_str());
    }
  }
  RectF r{0, 0, bounds().w, bounds().h};
  Color4f bg_c = bg_, act_bg = selected_bg_, act_txt = selected_text_color_,
         txt_c = text_color_, act_border{}, cont_border{};
  float act_radius = radius_ - 2;
  float cont_bw = gold_3d_ ? 1.0f : 0.0f;
  float cont_radius = radius_;
  // ★ d/v 消费（← button-styles.ts:932-990 容器 + :992-1103 选中块）
  bool cont_inset = false;
  Color4f cont_inset_color{}, cont_top_hi{};
  float cont_inset_dy = 0, cont_inset_blur = 0;
  bool act_has_bevel = false, act_has_glow = false;
  Color4f act_bevel_dark{}, act_glow_color{};
  float act_bevel_dy = 0, act_glow_blur = 0;
  const GradientStop* act_grad = nullptr;
  int act_grad_count = 0;
  float act_grad_angle = 135.0f;
  if (themed_) {
    // T12：ThemeService 查表（← get3DSegmented* 三函数）
    auto p = theme::ThemeService::instance().segmented_paint();
    bg_c = p.container_bg;
    act_bg = p.active_bg;
    act_txt = p.active_text;
    act_border = p.active_border;
    act_radius = p.active_radius;
    txt_c = p.text;
    cont_border = p.container_border;  // ← get3DSegmentedContainerStyle borderColor
    cont_bw = 1.0f;                    // ← 容器恒 1px
    cont_inset = p.container_inset;
    cont_inset_color = p.container_inset_color;
    cont_inset_dy = p.container_inset_dy;
    cont_inset_blur = p.container_inset_blur;
    cont_top_hi = p.container_top_hi;
    act_has_bevel = p.active_has_bevel;
    act_bevel_dark = p.active_bevel_dark;
    act_bevel_dy = p.active_bevel_dy;
    act_has_glow = p.active_has_glow;
    act_glow_color = p.active_glow_color;
    act_glow_blur = p.active_glow_blur;
    // ← :1006-1007 retro-3d 135° 六停金渐变（其余皮肤纯色）
    act_grad = p.active_grad;
    act_grad_count = p.active_grad_count;
    act_grad_angle = p.active_grad_angle;
    if (p.container_radius > 0.01f) cont_radius = p.container_radius;
  }
  Brush bg{bg_c};
  // src: button-styles.ts:939-945 容器 1px rgba(212,175,55,.35/.22)（gold_3d_）
  Color4f border_c = themed_ ? cont_border : act_border;
  if (!themed_ && gold_3d_)
    border_c = Color4f::hex(0xD4AF37, theme::ThemeService::instance().is_light()
                                          ? 0.35f
                                          : 0.22f);
  // 容器 inset 内凹阴影（← :934-938 retro-3d）
  rt.round_rect(r, cont_radius, {Color4f(0, 0, 0, 0), 0.0f}, &bg);
  if (cont_inset) {
    draw_top_inset_shadow(rt, r, cont_radius, cont_inset_color, cont_inset_dy,
                          cont_inset_blur);
    // src: button-styles.ts:934-938 inset 0 3d 5d 轨道四周内凹阴影（含左/右内边界阴影）
    draw_inner_glow(rt, r, cont_radius,
                    Color4f{cont_inset_color.r, cont_inset_color.g,
                            cont_inset_color.b, cont_inset_color.a * 0.65f},
                    cont_inset_blur * 0.75f, 4);
  }
  rt.round_rect(r, cont_radius, {border_c, cont_bw}, nullptr);
  // 容器顶部高光线（← 0 1px 0 rgba(255,255,255,.8|.08)）
  if (cont_top_hi.a > 0.004f)
    rt.draw_line({r.x + cont_radius, r.bottom() + 0.5f},
                 {r.right() - cont_radius, r.bottom() + 0.5f},
                 Pen{cont_top_hi, 1.0f});
  if (false)
    rt.draw_line({r.x + cont_radius, r.y + 0.5f},
                 {r.right() - cont_radius, r.y + 0.5f},
                 Pen{cont_top_hi, 1.0f});
  if (options_.empty()) return;
  float seg_w = r.w / static_cast<float>(options_.size());
  for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
    RectF seg{r.x + seg_w * i, r.y, seg_w, r.h};
    bool sel = i == selected_;
    if (sel) {
      // src: button-styles.ts:1005 滑块 radius 3；容器 p 1px → 内缩 1
      const float inset = gold_3d_ ? 1.0f : 2.0f;
      RectF sr{seg.x + inset, seg.y + inset, seg.w - 2 * inset, seg.h - 2 * inset};
      if (gold_3d_) {
        // src: button-styles.ts:1006-1007 135° 六停金渐变（retro-3d）
        //   themed_ 时取查表（单一真源），未接主题时用本地兜底。
        act_radius = 3.0f;
        const GradientStop* g =
            act_grad_count > 0 ? act_grad : kRetroActiveGrad;
        const int gc = act_grad_count > 0 ? act_grad_count : 6;
        const float ga = act_grad_count > 0 ? act_grad_angle : 135.0f;
        rt.fill_round_rect_gradient_angle(sr, act_radius, g, gc, ga);
        // src: button-styles.ts:1021-1022 边框 light '#8A6D00' / dark '#634E00'
        rt.round_rect(sr, act_radius,
                      Pen{act_border.a > 0 ? act_border
                                            : (theme::ThemeService::instance()
                                                   .is_light()
                                                   ? Color4f::hex(0x8A6D00)
                                                   : Color4f::hex(0x634E00)),
                          1.0f},
                      nullptr);
        // ★ 选中块硬浮雕 + 金辉光（← :1009-1020，×depth / ×vibrancy）
        //   boxShadow: 0 1d 0 0 bevelDark, 0 2d 0 0 bevelDark, …,
        //              0 0 calc(6px*v) rgba(255,193,7, calc(.2*v))
        if (act_has_bevel && act_bevel_dark.a > 0.004f) {
          const float by = act_bevel_dy;
          rt.draw_line({sr.x + act_radius, sr.bottom() + 0.5f},
                       {sr.right() - act_radius, sr.bottom() + 0.5f},
                       Pen{act_bevel_dark, 1.0f});
          if (by > 0.5f)
            rt.draw_line({sr.x + act_radius, sr.bottom() + by + 0.5f},
                         {sr.right() - act_radius, sr.bottom() + by + 0.5f},
                         Pen{act_bevel_dark, 1.0f});
        }
        if (act_has_glow)
          draw_glow(rt, sr, act_radius, act_glow_color, act_glow_blur);
      } else {
        Brush sel_bg{act_bg};
        if (act_has_glow)
          draw_glow(rt, sr, act_radius, act_glow_color, act_glow_blur);
        rt.round_rect(sr, act_radius,
                      themed_ && act_border.a > 0 ? Pen{act_border, 1}
                                                  : Pen{Color4f(0, 0, 0, 0), 0},
                      &sel_bg);
      }
    }
    TextStyle ts = style_;
    // src: routing-preference-card.tsx:110-137 未选中态 hover/active 文字色
    const bool rd = theme::ThemeService::instance().skin() == theme::SkinId::Retro3d;
    Color4f seg_txt = sel ? act_txt : txt_c;
    if (!sel && rd) {
      if (pressed_ == i) seg_txt = Color4f::hex(0x2C1F03, 0.5f);
      else if (hover_ == i) seg_txt = Color4f::hex(0x2C1F03);
    }
    ts.color = seg_txt;
    ts.weight = static_cast<int>(Weight::Bold);  // src: routing-preference-card.tsx:120
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    ts.ellipsis = true;
    rt.draw_text(options_[i], seg, ts);
  }
}

}  // namespace clashwin::ui
