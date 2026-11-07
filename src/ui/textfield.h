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
#include "ui/i18n_text.h"  // F-D01

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
  std::function<void(PointF global)> on_context_menu;  // 右键（宿主建菜单）
  std::function<void()> on_focus_lost;

  // 剪贴板钩子（全局注入：生产由 MainWindow 接 Win32，测试注入桩）
  static void set_clipboard_hooks(
      std::function<std::wstring()> get_fn, std::function<void(const std::wstring&)> set_fn);

  // ---- 清空按钮（opt-in，默认关；仅 Mini BaseSearchBox 场景开） ----
  // src: base-search-box.tsx:121-140（endAdornment ClearRounded 18 / pr 4 /
  //   disabled={!text} / opacity .5 / Tooltip shared.placeholders.resetInput）
  // 台账 1029 调用点清单（只在这些点开，通用控件不全局开）：
  //   - node_list.cpp 节点搜索（← proxy-head.tsx:149 BaseSearchBox）
  //   - connections_panel.cpp 连接面板搜索（← connections-panel.tsx:182）
  //   - connections_view.cpp 连接页搜索（同一 connections-panel 搜索语义）
  //   - profile_import_card URL / 设置端口等普通 TextField 不传 onClear → 保持关
  void set_clear_button(bool on, std::wstring tooltip = tr("shared.placeholders.resetInput"));
  bool clear_button() const { return clear_button_; }
  RectF clear_button_rect() const;  // 局部坐标（点击/悬停判定）
  std::wstring tooltip_text() const override;

  // ---- 编辑命令（对应 Win32 EM_* / WM_CUT 等语义；台账 1670-1674） ----
  bool can_undo() const { return false; }  // EM_CANUNDO：本控件无撤销栈 → 恒 false
  bool can_cut() const;                    // EM_CUT：有选区才可用
  bool can_copy() const;                   // EM_COPY：有选区才可用
  bool can_paste() const;                  // EM_CANPASTE：编辑控件恒可粘贴
  bool can_select_all() const;             // EM_SELALL：非空才可用（源码 disabled={!text}）
  bool can_clear() const;                  // 清空：非空才可用（源码 disabled={!text}）
  void cut();
  void copy();
  void paste();
  void select_all();
  void clear_text();

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

  // 按当前皮肤/滑块刷新输入框绘制参数（← get3DInputStyle 6 套皮肤查表）。
  // 调用点：各处创建 TextField 后统一调用，替代手写 bg_/border_ 字面量。
  void apply_theme();

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

  // 绘制参数（T12/T13 覆盖）
  TextStyle style_;
  Color4f bg_ = Color4f::hex(0xFFFFFF);  // src: MUI background.paper #fff
  Color4f border_ = Color4f::hex(0x000000, 0.23f);  // src: MUI OutlinedInput 23%
  Color4f border_focused_ = Color4f::hex(0x007AFF);  // ← 聚焦主色缺省（主题注入前）
  // src: index.scss:28-31 ::selection { color: var(--selection-color);
  //      background-color: var(--primary-main); } → 不透明主色底（非 25% alpha）
  Color4f selection_bg_ = Color4f::hex(0x5B5C9D);  // src: index.scss:19 --primary-main 缺省 #5b5c9d
  // src: index.scss:29 color: var(--selection-color)（:21 缺省 #f5f5f5；
  //      use-custom-theme.ts:365 selectColor 亮 #f5f5f5 / 暗 #3E3E3E → :371 setProperty）
  Color4f selection_text_ = Color4f::hex(0xF5F5F5);  // src: index.scss:21
  Color4f placeholder_color_ = Color4f::hex(0x000000, 0.38f);  // src: MUI text.disabled
  Color4f caret_color_ = Color4f::hex(0x007AFF);  // ← 光标主色缺省
  float radius_ = 4;  // ← MUI shape.borderRadius 4
  float border_w_ = 1;  // ← MUI OutlinedInput 缺省 1px；retro-3d outline 2px
  float border_w_focused_ = 0;  // 0 = 沿用 border_w_（← get3DInputStyle focus 单独宽）
  // inset 内凹阴影 / 聚焦外发光（← get3DInputStyle boxShadow；D2D 用 soft_shadow 等效实现）
  bool has_inset_ = false;
  Color4f inset_color_ = Color4f(0, 0, 0, 0);
  float inset_dy_ = 0;
  float inset_blur_ = 0;
  Color4f inset_top_hi_ = Color4f(0, 0, 0, 0);
  bool focus_glow_ = false;
  Color4f focus_glow_color_ = Color4f(0, 0, 0, 0);
  float focus_glow_blur_ = 0;
  float focus_ring_ = 0;
  float height_ = 28;  // ← 行内输入框高 28（源码 size=small 布局）
  static constexpr int kCaretBlinkMs = 500;  // 豁免: Mini 无对应物 — 光标闪烁半周期（MUI InputBase 无闪烁字面量）

 private:
  void insert_text(const std::wstring& s);
  void erase_selection();
  bool has_selection() const;
  int hit_index(float x);  // x → 光标 index（前缀宽测量）
  void clamp_scroll();
  // 光标 index 的像素 x（局部，含 scroll 偏移）
  float cursor_x(int index);
  float text_right_pad() const;  // 右侧留白（清空按钮开启时 32，否则 8）

  std::wstring text_;
  std::wstring placeholder_;
  std::wstring composition_;
  int composition_cursor_ = 0;
  int cursor_ = 0;
  int anchor_ = 0;
  float scroll_x_ = 0;
  bool clear_button_ = false;
  bool clear_hover_ = false;
  std::wstring clear_tooltip_ = tr("shared.placeholders.resetInput");
};

}  // namespace clashwin::ui
