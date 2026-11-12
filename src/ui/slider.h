#pragma once
// Slider（T10）：滑块——值/步进/拖拽；双滑块预留（value2）。
// 对照 MUI Slider：step 量化、active_thumb、拖拽中回调。
#include <cstdio>
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
  // ★ thumb 悬停态（← Mini `.MuiSlider-thumb:hover`）——测试检查点
  bool thumb_hovered() const { return thumb_hovered_; }
  // ★ 键盘焦点（← Mini `.Mui-focusVisible`）——测试检查点
  bool key_focused() const { return key_focused_; }

  std::function<void(float)> on_change;
  // BUG-358: MUI Slider onChangeCommitted 等价回调（松手或键盘单步调节完成时触发）
  std::function<void(float)> on_commit;

  void set_value(float v, bool fire = true);

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;
  void on_paint(IRenderTarget& rt) override;
  bool wants_key() const override { return true; }

  A11yRole a11y_default_role() const override { return A11yRole::Slider; }
  std::wstring a11y_value() const override {
    wchar_t buf[32]{};
    swprintf_s(buf, L"%.2f", static_cast<double>(value_));
    return buf;
  }
  std::optional<A11yRange> a11y_range() const override {
    const double sp = step_ > 0.0f ? static_cast<double>(step_) : 0.05;
    const double lp = (max_ > min_) ? static_cast<double>(max_ - min_) * 0.1 : sp;
    return A11yRange{static_cast<double>(value_), static_cast<double>(min_),
                     static_cast<double>(max_), sp, lp};
  }
  bool a11y_set_range(double v) override {
    if (!enabled()) return false;
    set_value(static_cast<float>(v), true);
    if (on_commit) on_commit(value_);
    return true;
  }

  // 绘制参数
  Color4f track_ = Color4f::hex(0x000000, 0.20f);  // ← 滑轨 20% 黑
  Color4f fill_ = Color4f::hex(0x007AFF);  // ← 主色缺省（主题注入前）
  Color4f thumb_ = Color4f::hex(0x007AFF);  // ← 主色缺省（主题注入前）
  float thickness_ = 4;  // ← 轨厚 4
  float thumb_d_ = 14;  // ← 滑块直径 14
  // 金色立体（3D 停靠）← get3DSliderStyle retro-3d；true 时轨道/滑块走金渐变+描边
  bool gold_3d_ = false;
  // T12：themed_ = 走 ThemeService::slider_paint()（6 套皮肤查表）
  bool themed_ = false;
  void apply_theme();

 private:
  float value_at(float local_x);   // x → 量化值
  void update(float local_x);
  void update_hover();             // 依据 last_mouse_x_ 刷新 thumb 悬停态
  bool dragging_ = false;
  // ★ thumb 悬停判定（Mini `.MuiSlider-thumb:hover`）
  //   last_mouse_x_ 记录最近一次鼠标位置；on_paint 时判断是否落在某个 thumb 上。
  float last_mouse_x_ = -1.0f;
  bool has_mouse_ = false;
  bool thumb_hovered_ = false;
  // ★ 键盘焦点（MUI `.Mui-focusVisible` 与 `:hover` 共用同一组样式）
  bool key_focused_ = false;
};

}  // namespace clashwin::ui
