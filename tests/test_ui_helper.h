#pragma once
// UI 测试公共设施（T10）：离屏渲染单例 + 输入合成 + 测量桩。
#pragma once

#include <string>
#include <vector>

#include "test_util.h"
#include "ui/d2d_context.h"
#include "ui/widget.h"

namespace ui_test {

using namespace clashwin::ui;

// 进程级 D2D 上下文 + 渲染目标（永不销毁，text_engine 指针安全）
inline D2dContext& ctx() {
  static D2dContext c;
  static bool ok = c.init();
  (void)ok;
  return c;
}

inline D2dRenderTarget& rt() {
  static D2dRenderTarget r(ctx());
  return r;
}

// 建离屏 target（WIC 软件渲染，dpi 模拟 96/144）
inline bool make_offscreen(int w = 200, int h = 100, float dpi = 96.0f) {
  return rt().create_offscreen(w, h, dpi);
}

// 读像素（BGRA）；越界返回 {0,0,0,0}（每次现读当前 target，测试不缓存）
struct Px {
  uint8_t b, g, r, a;
};
inline Px pixel(int x, int y) {
  std::vector<uint8_t> buf;
  int w = 0, h = 0;
  if (!rt().read_pixels(buf, w, h)) return {0, 0, 0, 0};
  if (x < 0 || y < 0 || x >= w || y >= h) return {0, 0, 0, 0};
  const uint8_t* p = buf.data() + (static_cast<size_t>(y) * w + x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

// ---------- 固定尺寸测量桩（布局数值断言用） ----------
class FixedBox : public Widget {
 public:
  explicit FixedBox(float w, float h, Widget* parent = nullptr)
      : Widget(parent), w_(w), h_(h) {}
  SizeF on_measure(float, float) override { return {w_, h_}; }
  // 记录 arrange 结果与绘制次数
  int paint_count = 0;
  void on_paint(IRenderTarget&) override { ++paint_count; }

 private:
  float w_, h_;
};

// ---------- 输入合成（走真实 route 路径） ----------
inline void move_to(Widget* root, float x, float y, int buttons = 0) {
  MouseEvent e{MouseEventType::Move, {x, y}, buttons};
  route_mouse_event(root, e);
}

inline void press(Widget* root, float x, float y) {
  MouseEvent e{MouseEventType::Down, {x, y}, kMouseLeft};
  route_mouse_event(root, e);
}

inline void release(Widget* root, float x, float y) {
  MouseEvent e{MouseEventType::Up, {x, y}, kMouseLeft};
  route_mouse_event(root, e);
}

inline void click(Widget* root, float x, float y) {
  press(root, x, y);
  release(root, x, y);
}

inline void wheel_at(Widget* root, float x, float y, int delta) {
  MouseEvent e{MouseEventType::Wheel, {x, y}, 0, delta};
  route_mouse_event(root, e);
}

inline void key(Widget* root, uint32_t vk, bool ctrl = false,
                bool shift = false) {
  KeyEvent e{KeyEventType::Down, vk, 0, ctrl, shift};
  route_key_event(root, e);
}

inline void chr(Widget* root, wchar_t c, bool ctrl = false,
                bool shift = false) {
  KeyEvent e{KeyEventType::Char, 0, c, ctrl, shift};
  route_key_event(root, e);
}

inline bool near_f(float a, float b, float eps = 0.51f) {
  return a - eps <= b && b <= a + eps;
}

}  // namespace ui_test
