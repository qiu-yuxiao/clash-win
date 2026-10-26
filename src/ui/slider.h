#pragma once
// Slider（T10）：滑块——值/步进/拖拽；双滑块预留（value2）。
// 对照 MUI Slider：step 量化、active_thumb、拖拽中回调。
#include <functional>
#include <optional>

#include "widget.h"

namespace clashwin::ui {

class Slider : public Widget {
 public:
  explicit Slider(Widget* parent = nullptr) : Widget(parent) {}

  float min_ = 0, max_ = 100, step_ = 1;
  float value_ = 0;
  std::optional<float> value2_;   // 双滑块预留（T17 Depth/Vibrancy 单滑块）
  int active_thumb_ = 0;          // 0 = value_，1 = value2_
  bool dragging() const { return dragging_; }

  std::function<void(float)> on_change;

  void set_value(float v, bool fire = true);

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

  // 绘制参数
  Color4f track_ = Color4f::hex(0x000000, 0.20f);
  Color4f fill_ = Color4f::hex(0x007AFF);
  Color4f thumb_ = Color4f::hex(0x007AFF);
  float thickness_ = 4;
  float thumb_d_ = 14;

 private:
  float value_at(float local_x);   // x → 量化值
  void update(float local_x);
  bool dragging_ = false;
};

}  // namespace clashwin::ui
