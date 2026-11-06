// SwitchW 实现（T10）。
#include "switch_w.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "theme/theme.h"
#include "util/log.h"

namespace clashwin::ui {

bool SwitchW::set_checked(bool v, bool fire) {
  if (checked_ == v) return false;
  checked_ = v;
  anim_ = v ? 1.0f : 0.0f;  // 即时落位（拖拽进度由 move 驱动）
  invalidate();
  if (fire && on_change) on_change(checked_);
  return true;
}

SizeF SwitchW::on_measure(float max_w, float max_h) {
  (void)max_w; (void)max_h;
  return {width_, height_};
}

void SwitchW::update_drag(float local_x) {
  float range = width_ - height_;
  float t = range > 0 ? (local_x - height_ / 2) / range : 0.0f;
  anim_ = t < 0 ? 0 : (t > 1 ? 1 : t);
  dragging_ = true;
  drag_moved_ = true;
  invalidate();
}

bool SwitchW::on_mouse(const MouseEvent& e) {
  switch (e.type) {
    case MouseEventType::Down:
      if (!enabled()) return true;
      press_x_ = e.pos.x;
      drag_moved_ = false;
      return true;
    case MouseEventType::Move:
      if (!enabled()) return false;
      if ((e.buttons & kMouseLeft) &&
          (drag_moved_ || std::fabs(e.pos.x - press_x_) > 3.0f)) {
        update_drag(e.pos.x);  // 超过阈值才算拖动（单击不动）
        return true;
      }
      return drag_moved_;
    case MouseEventType::Up:
      if (!enabled()) return true;
      if (drag_moved_) {
        dragging_ = false;
        drag_moved_ = false;
        set_checked(anim_ >= 0.5f, true);  // <0.5 回弹
      } else {
        set_checked(!checked_, true);  // 单击切换（无拖动位移）
      }
      return true;
    default:
      return false;
  }
}

void SwitchW::on_paint(IRenderTarget& rt) {
  // 【DPIARCH】← base-switch.tsx：padding 0，thumb=全高（14），track r=h/2（7），
  // checked 位移 translateX(w-h)=14（:21-34/104 结构）。
  auto& svc = theme::ThemeService::instance();
  const bool light = svc.is_light();
  float h = bounds().h, w = bounds().w;
  float thumb_d = h;
  // MUI disabled：轨道/旋钮按 0.38 叠乘（MUI Switch `.Mui-disabled` 轨道
  //   opacity .38，thumb 同档）——src: MUI Switch disabled 语义。
  const float dis = enabled() ? 1.0f : 0.38f;
  // 轨道底（未选中）src: base-switch.tsx:104-109 bgcolor rgba(212,175,55,.25/.15)
  //   opacity .85；border 1px rgba(212,175,55,.3/.18)
  Color4f off = light ? Color4f::hex(0xD4AF37, 0.25f) : Color4f::hex(0xD4AF37, 0.15f);
  off.a *= (1.0f - anim_) * 0.85f * dis;
  Color4f edge_c = light ? Color4f::hex(0xD4AF37, 0.3f) : Color4f::hex(0xD4AF37, 0.18f);
  edge_c.a *= dis;
  Pen edge{edge_c, 1.0f};
  Brush off_b{off};
  rt.round_rect({0, 0, w, h}, h / 2, edge, &off_b);
  // 选中轨道 src: base-switch.tsx:99-103
  //   linear-gradient(to bottom,#FFE082 0%,#FFC107 50%,#B8860B 100%) opacity .85
  if (anim_ > 0.001f) {
    // src: base-switch.tsx:99-103 135°/to bottom 3 停靠（#FFE082/#FFC107/#B8860B α.85）
    GradientStop st[3] = {{0.0f, Color4f::hex(0xFFE082, 0.85f * anim_ * dis)},
                          {0.5f, Color4f::hex(0xFFC107, 0.85f * anim_ * dis)},
                          {1.0f, Color4f::hex(0xB8860B, 0.85f * anim_ * dis)}};
    // 角度 180 = CSS to bottom；src: base-switch.tsx:99
    rt.fill_round_rect_gradient_angle({0, 0, w, h}, h / 2, st, 3, 180.0f);
  }
  float tx = anim_ * (w - h);
  // 滑块 src: base-switch.tsx:76-80 checked 金 radial(circle at 35% 35%)；
  //   未选中 :90-92。CSS circle 半径取 farthest-corner（≈0.65×边长×√2）
  GradientStop th[4];
  if (anim_ > 0.5f) {
    th[0] = {0.00f, light ? Color4f::hex(0xFFFDE7) : Color4f::hex(0xFFF9C4)};
    th[1] = {0.45f, light ? Color4f::hex(0xFFD54F) : Color4f::hex(0xFFC107)};
    th[2] = {0.80f, Color4f::hex(0xFFA000)};
    th[3] = {1.00f, light ? Color4f::hex(0xB8860B) : Color4f::hex(0x8A6D00)};
    for (auto& s : th) s.color.a *= dis;
  } else {
    th[0] = {0.00f, light ? Color4f::hex(0xFFFDE7) : Color4f::hex(0xFFF9C4)};
    th[1] = {0.60f, light ? Color4f::hex(0xD4AF37, 0.3f)
                          : Color4f::hex(0x8A6D00, 0.3f)};
    th[2] = {1.00f, light ? Color4f::hex(0x8A6D00, 0.45f)
                          : Color4f::hex(0x634E00, 0.55f)};
    th[3] = {1.00f, th[2].color};
    for (auto& s : th) s.color.a *= dis;
  }
  const float corner_r = std::sqrt(2.0f) * (thumb_d * 0.65f);
  rt.fill_round_rect_radial({tx, 0, thumb_d, thumb_d}, thumb_d / 2, th, 4,
                            0.35f, 0.35f, corner_r, corner_r);
  Pen tedge{anim_ > 0.5f
                ? (light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00))
                : (light ? Color4f::hex(0x8A6D00, 0.4f)
                         : Color4f::hex(0x634E00, 0.45f)),
            1.0f};
  tedge.color.a *= dis;
  rt.round_rect({tx, 0, thumb_d, thumb_d}, thumb_d / 2, tedge, nullptr);
}

}  // namespace clashwin::ui
