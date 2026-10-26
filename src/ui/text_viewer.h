#pragma once
// TextViewer（T10）：自绘只读文本查看器（Monaco 替代方案，用户裁定 B）。
// - 分行缓存 + 只绘可视行（10 万行级别不卡）
// - 滚轮滚动；"用系统编辑器打开"回调（按钮归 T21 弹窗宿主）
#include <string>
#include <vector>

#include "widget.h"

namespace clashwin::ui {

class TextViewer : public Widget {
 public:
  explicit TextViewer(Widget* parent = nullptr) : Widget(parent) {}

  void set_text(const std::string& utf8);   // 分行缓存
  const std::vector<std::wstring>& lines() const { return lines_; }
  size_t line_count() const { return lines_.size(); }

  float scroll_offset() const { return scroll_y_; }
  void set_scroll_offset(float v);
  float max_scroll() const;

  std::function<void()> on_open_in_editor;  // "用系统编辑器打开"

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

  // 绘制参数
  TextStyle style_;                          // 缺省 Segoe UI 12；T21 设 Consolas
  Color4f bg_ = Color4f::hex(0xFFFFFF);
  Color4f text_ = Color4f::hex(0x000000, 0.87f);
  float line_spacing = 1.4f;
  float pad_ = 8;

 private:
  std::vector<std::wstring> lines_;
  float scroll_y_ = 0;
  float line_height_ = 16;  // measure 后更新
};

}  // namespace clashwin::ui
