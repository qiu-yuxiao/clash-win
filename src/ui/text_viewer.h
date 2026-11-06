#pragma once
// TextViewer（T10 / T21）：自绘只读文本查看器（Monaco 替代方案，用户裁定 B）。
// - 分行缓存 + 只绘可视行（10 万行级别不卡）
// - 滚轮滚动；"用系统编辑器打开"回调
// - T21 增强：Consolas 等宽字体、行号栏（Line numbers gutter）与分割线、自动换行（Word wrap）切换
#include <functional>
#include <string>
#include <vector>

#include "widget.h"

namespace clashwin::ui {

class TextViewer : public Widget {
 public:
  explicit TextViewer(Widget* parent = nullptr);

  void set_text(const std::string& utf8);   // 分行缓存
  const std::vector<std::wstring>& lines() const { return lines_; }
  size_t line_count() const { return lines_.size(); }

  float scroll_offset() const { return scroll_y_; }
  void set_scroll_offset(float v);
  float max_scroll() const;

  // T21 增强属性
  bool show_line_numbers() const { return show_line_numbers_; }
  void set_show_line_numbers(bool show);

  bool word_wrap() const { return word_wrap_; }
  void set_word_wrap(bool wrap);

  float gutter_width() const;

  const std::wstring& file_path() const { return file_path_; }
  void set_file_path(const std::wstring& path) { file_path_ = path; }

  std::function<void()> on_open_in_editor;  // "用系统编辑器打开"回调
  void open_in_system_editor(const std::wstring& file_path = L"");

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

  // 绘制参数
  TextStyle style_;                          // T21 缺省 Consolas 12pt
  Color4f bg_ = Color4f::hex(0xFFFFFF);  // ← 编辑器浅色纸底
  Color4f text_ = Color4f::hex(0x000000, 0.87f);  // src: MUI text.primary
  Color4f gutter_bg_ = Color4f::hex(0xF8F9FA);  // ← VS Code light 行号槽底
  Color4f gutter_text_ = Color4f::hex(0x9E9E9E);  // ← VS Code light 行号灰
  Color4f divider_color_ = Color4f::hex(0xE0E0E0);  // ← VS Code light 分栏线
  float line_spacing = 1.4f;  // ← 等宽日志行距 1.4
  float pad_ = 8;

 private:
  std::vector<std::wstring> lines_;
  float scroll_y_ = 0;
  float line_height_ = 18;  // measure 后更新
  bool show_line_numbers_ = true;
  bool word_wrap_ = false;
  std::wstring file_path_;
};

}  // namespace clashwin::ui
