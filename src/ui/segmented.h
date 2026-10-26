#pragma once
// Segmented（T10）：分段切换器——MAN/SYS/TUN 三段（takeover-mode-card）与
// DIR/RUL/PRX 三段（routing-preference-card）的底座。
// 对照 MUI Segmented/ToggleButton：选中态白底 + 主色字 + 粗体（原版硬指标）。
#include <string>
#include <vector>

#include "widget.h"

namespace clashwin::ui {

class Segmented : public Widget {
 public:
  explicit Segmented(Widget* parent = nullptr) : Widget(parent) {}

  void set_options(std::vector<std::wstring> opts, int sel);
  void set_selected(int i);  // 触发 on_change
  int selected() const { return selected_; }
  size_t option_count() const { return options_.size(); }

  std::function<void(int)> on_change;

  // 绘制参数（T12 皮肤覆盖）
  TextStyle style_;
  Color4f bg_ = Color4f::hex(0x000000, 0.06f);
  Color4f selected_bg_ = Color4f::hex(0xFFFFFF);
  Color4f selected_text_color_ = Color4f::hex(0x007AFF);
  Color4f text_color_ = Color4f::hex(0x000000, 0.62f);
  float radius_ = 6;
  float item_padding_h = 10;

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

 private:
  int hit_segment(const PointF& local) const;
  std::vector<std::wstring> options_;
  int selected_ = 0;
  int pressed_ = -1;
};

}  // namespace clashwin::ui
