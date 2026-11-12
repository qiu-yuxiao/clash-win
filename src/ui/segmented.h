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
  // 逐段 Tooltip 文案（src: routing-preference-card.tsx:95/145/195）
  void set_tooltips(std::vector<std::wstring> tips) { tooltips_ = tips; }

  // F-D01：以 Mini locale key 设置分段文案 / tooltip。
  // 语言切换时 options 在 on_paint 按 revision 重取，tooltip 在读取时实时取，
  // 等价 MF:pages/_layout.tsx:478-483 的整树重渲染。
  void set_options_i18n(std::vector<const char*> keys, int sel);
  void set_tooltips_i18n(std::vector<const char*> keys);
  std::wstring tooltip_text() const override;
  // src: routing-preference-card.tsx:94-241 <Tooltip placement="top" arrow> 锚定悬停分段
  std::optional<RectF> tooltip_anchor_rect() const override;
  bool tooltip_prefer_top_arrow() const override { return true; }
  void set_selected(int i);  // 触发 on_change
  void select_silent(int i);  // 仅同步外观（外部配置回填用，不触发回调）
  int selected() const { return selected_; }
  size_t option_count() const { return options_.size(); }

  std::function<void(int)> on_change;
  // 点击当前已选中分段时的附加回调（如代理方式第 3 段「添加网址」重复点击仍需开弹窗）
  std::function<void(int)> on_reclick;

  // T12：主题接入开关（true 时 on_paint 走 ThemeService 查表）
  bool themed_ = false;
  // 金色立体（3D 停靠）：true 时选中块走 135° 金渐变 + 金描边
  bool gold_3d_ = false;

  // 绘制参数（T12 皮肤覆盖；themed_ 时被查表覆盖）
  TextStyle style_;
  Color4f bg_ = Color4f::hex(0x000000, 0.06f);  // ← 分段容器底 6% 黑
  Color4f selected_bg_ = Color4f::hex(0xFFFFFF);  // ← 选中项纸底白
  Color4f selected_text_color_ = Color4f::hex(0x007AFF);  // ← 选中主色缺省（主题注入前）
  Color4f text_color_ = Color4f::hex(0x000000, 0.62f);  // ← 未选中文字 62% 黑
  float radius_ = 6;  // ← 分段圆角 6
  float item_padding_h = 10;  // ← 分段水平内距 10

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

  A11yRole a11y_default_role() const override { return A11yRole::Group; }
  size_t a11y_virtual_child_count() const override;
  A11ySubItem a11y_virtual_child(size_t index) const override;
  bool a11y_invoke_virtual_child(size_t index) override;

 private:
  void sync_i18n_options() const;
  int hit_segment(const PointF& local) const;
  mutable std::vector<std::wstring> options_;
  std::vector<std::wstring> tooltips_;
  std::vector<std::string> option_keys_;
  std::vector<std::string> tooltip_keys_;
  mutable uint64_t options_revision_ = 0;
  int selected_ = 0;
  int pressed_ = -1;
  int hover_ = -1;
};

}  // namespace clashwin::ui
