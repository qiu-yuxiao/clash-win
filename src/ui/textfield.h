#pragma once
// TextField（T10）：单行输入——光标/选区/placeholder/右键粘贴 + IME 预留。
// 对照 MUI TextField（单行 variant）：
// - 键盘：字符插入、Left/Right（Shift 扩选）、Home/End、Backspace/Delete、
//   Ctrl+A/C/V（剪贴板经钩子注入，测试不碰真实剪贴板）
// - 焦点：request_focus 后显示光标；失焦清选区
// - 右键：on_context_menu 回调（T13 接 context_menu：剪切/复制/粘贴）
// - IME 兼容预留：set_composition/clear_composition（composition 串替换渲染，
//   WM_IME_* 接线归 T11/T13）
#include <functional>

#include "widget.h"

namespace clashwin::ui {

class TextField : public Widget {
 public:
  explicit TextField(Widget* parent = nullptr) : Widget(parent) {}

  const std::wstring& text() const { return text_; }
  void set_text(const std::wstring& t);
  void set_placeholder(const std::wstring& p) {
    placeholder_ = p;
    invalidate();
  }

  int cursor() const { return cursor_; }
  int anchor() const { return anchor_; }  // 选区锚（==cursor 表示无选区）
  std::wstring selection() const;
  bool focused() const { return Widget::focused(); }

  std::function<void(const std::wstring&)> on_text_changed;
  std::function<void()> on_submit;          // Enter
  std::function<void(PointF global)> on_context_menu;  // 右键（T13 接菜单）
  std::function<void()> on_focus_lost;

  // 剪贴板钩子（全局注入；真实 Win32 剪贴板实现归 T13，测试注入桩）
  static void set_clipboard_hooks(
      std::function<std::wstring()> get_fn, std::function<void(const std::wstring&)> set_fn);

  // IME 预留接口
  void set_composition(const std::wstring& s, int comp_cursor) {
    composition_ = s;
    composition_cursor_ = comp_cursor;
    invalidate();
  }
  void clear_composition() {
    composition_.clear();
    invalidate();
  }
  const std::wstring& composition() const { return composition_; }

  bool wants_key() const override { return true; }

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

  // 绘制参数（T12/T13 覆盖）
  TextStyle style_;
  Color4f bg_ = Color4f::hex(0xFFFFFF);
  Color4f border_ = Color4f::hex(0x000000, 0.23f);
  Color4f border_focused_ = Color4f::hex(0x007AFF);
  Color4f selection_bg_ = Color4f::hex(0x007AFF, 0.25f);
  Color4f placeholder_color_ = Color4f::hex(0x000000, 0.38f);
  Color4f caret_color_ = Color4f::hex(0x007AFF);
  float radius_ = 4;
  float height_ = 28;
  static constexpr int kCaretBlinkMs = 500;

 private:
  void insert_text(const std::wstring& s);
  void erase_selection();
  int hit_index(float x);  // x → 光标 index（前缀宽测量）
  void clamp_scroll();
  // 光标 index 的像素 x（局部，含 scroll 偏移）
  float cursor_x(int index);

  std::wstring text_;
  std::wstring placeholder_;
  std::wstring composition_;
  int composition_cursor_ = 0;
  int cursor_ = 0;
  int anchor_ = 0;
  float scroll_x_ = 0;
};

}  // namespace clashwin::ui
