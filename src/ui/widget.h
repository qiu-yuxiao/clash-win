#pragma once
// Widget 基类（T10）：Retained Widget 树（§5.1 架构原则——对应 React 组件树，
// 但常驻内存由我方驱动布局/绘制）。
// - 逻辑几何 bounds（父坐标系，96dpi 逻辑像素）；可见/启用/父指针/z 序
// - 两阶段布局：measure（期望尺寸）→ layout_tree（落位并递归布置子）
// - 绘制：paint（可见+裁剪+递归）→ on_paint（子类画自己）
// - 输入：on_mouse/on_key；route_mouse_event/route_key_event 全局路由
//   （hover 追踪 / capture 抓取 / 焦点 / wheel 沿父链冒泡）
// - hit_test：全局坐标 → 最深可见启用 widget
// - invalidate：脏区上报（全局逻辑坐标汇入 FrameClock 合并）
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "render_target.h"

namespace clashwin::ui {

// ---------- 输入事件（合成消息；测试直接构造后走 route_*_event） ----------
enum class MouseEventType { Enter, Leave, Move, Down, Up, Wheel };
// buttons 位：1=左 2=右 4=中
inline constexpr int kMouseLeft = 1;    // src: Win32 MK_LBUTTON
inline constexpr int kMouseRight = 2;   // src: Win32 MK_RBUTTON
inline constexpr int kMouseMiddle = 4;  // src: Win32 MK_MBUTTON
// key：VK_*；Char 事件 ch 有效
enum class KeyEventType { Down, Up, Char };

struct MouseEvent {
  MouseEventType type;
  PointF pos;             // 目标 widget 局部坐标
  int buttons = 0;
  int wheel_delta = 0;    // 120 的倍数
  bool handled = false;
};

struct KeyEvent {
  KeyEventType type;
  uint32_t key = 0;       // VK_ 值（Down/Up）
  wchar_t ch = 0;         // 字符（Char）
  bool ctrl = false, shift = false, alt = false;
  bool handled = false;
};

// ---------- Win32 UI Automation 无障碍语义模型 ----------
// 豁免: Mini 无对应物 — 原生 UIA 控件角色与虚拟子项描述结构（出处: 无）
enum class A11yRole {
  None = 0,
  Window,
  Pane,
  Group,
  Dialog,
  TitleBar,
  ToolBar,
  StatusBar,
  Text,
  Button,
  CheckBox,
  RadioButton,
  Edit,
  Slider,
  ComboBox,
  List,
  ListItem,
  Menu,
  MenuItem,
  Tab,
  TabItem,
};

struct A11yRange {
  double val = 0.0;
  double min_val = 0.0;
  double max_val = 1.0;
  double small_step = 0.05;
  double large_step = 0.2;
};

struct A11yPosInSet {
  int pos = 0;   // 1-based 序号
  int size = 0;  // 集合总数
};

struct A11ySubItem {
  A11yRole role = A11yRole::Button;
  std::wstring name;
  std::wstring value;
  std::wstring help_text;
  std::wstring automation_id;
  RectF local_bounds{};  // 父 Widget 局部逻辑坐标 (DIP)
  bool enabled = true;
  bool focusable = true;
  bool has_focus = false;
  bool can_invoke = false;
  std::optional<bool> toggle_state;
  std::optional<bool> selected;
  std::optional<bool> expanded;
  bool can_set_value = false;
  int pos_in_set = 0;
  int size_of_set = 0;
};

class Widget {
 public:
  explicit Widget(Widget* parent = nullptr);
  virtual ~Widget();
  Widget(const Widget&) = delete;
  Widget& operator=(const Widget&) = delete;

  // ---- 树结构 ----
  Widget* parent() const { return parent_; }
  void set_parent(Widget* p) { parent_ = p; }
  size_t child_count() const { return children_.size(); }
  Widget* child(size_t i) const { return children_[i].get(); }
  // 尾部 = 最上层。【所有权移交】调用方 std::move 后不得再使用原指针；
  // 需要后续访问请在 move 前先取裸指针（Widget* raw = x.get()）。
  void add_child(std::unique_ptr<Widget> w);
  void insert_child(size_t index, std::unique_ptr<Widget> w);  // 保序挂载
  std::unique_ptr<Widget> remove_child(Widget* w);    // 卸载（React unmount 等价）
  // 绘制顺序 = z_order 升序稳定（同序按加入顺序，后加者在上）
  int z_order() const { return z_order_; }
  void set_z_order(int z) { z_order_ = z; }

  // ---- 几何（父坐标系逻辑像素） ----
  const RectF& bounds() const { return bounds_; }
  void set_bounds(const RectF& r);
  PointF to_global(PointF local) const;
  PointF to_local(PointF global) const;
  RectF global_bounds() const;  // 以根坐标系（窗口客户区逻辑）表示

  // ---- 状态 ----
  bool visible() const { return visible_; }
  void set_visible(bool v);
  bool enabled() const { return enabled_; }
  void set_enabled(bool e);
  // 子树整体不透明度（0..1）：<1 时该 widget 与其全部子节点在一次合成里
  // 按 alpha 混合（← MUI Fade 过渡树）。用于 Dialog / Menu 的淡入淡出。
  float opacity() const { return opacity_; }
  void set_opacity(float a);
  bool focused() const;                 // route 层焦点 == 本 widget
  void request_focus();                 // wants_key 才有意义
  void blur();
  virtual bool wants_key() const { return false; }  // 焦点候选（textfield）
  virtual HCURSOR cursor(const PointF& local) const { (void)local; return nullptr; }
  std::function<void()> on_focus_lost;  // 焦点被其它控件接管时触发（MUI onBlur 语义）

  // ---- 两阶段布局 ----
  SizeF measure(float max_w, float max_h);   // 期望尺寸（含 padding）
  void layout_tree(const RectF& area);       // 落位 + 递归布置子
  void relayout();  // 根调用：按根 bounds 重排整树（measure+arrange 快捷方式）

  // ---- 绘制 ----
  bool clip_to_bounds() const { return clip_to_bounds_; }
  void set_clip_to_bounds(bool c) { clip_to_bounds_ = c; }
  void paint(IRenderTarget& rt, const RectF& clip);
  void paint_all(IRenderTarget& rt);  // 根入口（clip = 全域）
  virtual void on_paint(IRenderTarget& rt) { (void)rt; }

  // ---- i18n 文本绑定（F-D01）----
  // ← MF:pages/_layout.tsx:478-483：verge.language 变化 → React 整树重渲染。
  //   把控件自身的 wstring 文本槽绑到 Mini locale key；语言切换后由根节点
  //   在 paint_all 时一次性重取译文 + relayout，等价 React 重渲染。
  //   slot 必须指向本控件生命周期内的成员（通常为 Label::text / Button::text）。
  void bind_i18n_text(std::wstring* slot, const char* key);

  // ---- 输入（返回 true = 已处理，停止冒泡） ----
  virtual bool on_mouse(const MouseEvent& e) { (void)e; return false; }
  virtual bool on_key(const KeyEvent& e) { (void)e; return false; }

  // ---- 命中 ----
  // 全局坐标 → 最深可见启用 widget（可为子）；未命中返回 nullptr
  virtual Widget* hit_test(PointF global);
  // 矩形命中默认实现（local 为相对自身原点的坐标）；圆角/异形可重载
  // ★ 铺满型容器（整页浮层 / 通知层 / 菜单层）必须覆写本函数：
  //   命中测试的兜底是「自身矩形命中即返回自身」，铺满容器会截断整条命中链，
  //   使下层控件收不到鼠标事件。惯例：`return open_ && Widget::hit_self(local);`
  //   （对照 src/ui/snackbar_w.h / context_menu.h / utility_modals.h 等）。
  virtual bool hit_self(PointF local) const {
    return local.x >= 0 && local.y >= 0 && local.x < bounds_.w &&
           local.y < bounds_.h;
  }

  // ---- 脏区上报（全局逻辑坐标） ----
  void invalidate();                       // 整个自身
  void invalidate(const RectF& local);     // 局部（父坐标系内再转全局）

  // ---- tooltip 钩子（TooltipHost 扫描用；返回空=无提示） ----
  void set_tooltip_text(std::wstring t) { tooltip_text_ = std::move(t); }
  void set_i18n_tooltip(const char* key) { bind_i18n_text(&tooltip_text_, key); }
  virtual std::wstring tooltip_text() const {
    ensure_i18n_fresh();
    return tooltip_text_;
  }
  // 可选：指定提示锚定矩形（全局 DIP 坐标，如分段器的当前悬停子段、输入框清空钮）
  virtual std::optional<RectF> tooltip_anchor_rect() const { return std::nullopt; }
  // 可选：对齐 Mini <Tooltip placement="top" arrow>（分段器悬停提示）
  virtual bool tooltip_prefer_top_arrow() const { return false; }

  // ---- Win32 UI Automation 无障碍钩子 ----
  void set_a11y_role(A11yRole r) { a11y_role_ = r; }
  A11yRole a11y_role() const {
    return a11y_role_ != A11yRole::None ? a11y_role_ : a11y_default_role();
  }
  void set_a11y_name(std::wstring n) { a11y_name_ = std::move(n); }
  void set_i18n_a11y_name(const char* key) { bind_i18n_text(&a11y_name_, key); }
  void set_a11y_automation_id(std::wstring id) { a11y_automation_id_ = std::move(id); }
  const std::wstring& a11y_automation_id() const { return a11y_automation_id_; }
  void set_a11y_labelled_by(const Widget* label_w) { a11y_labelled_by_ = label_w; }
  const Widget* a11y_labelled_by() const { return a11y_labelled_by_; }
  void set_a11y_hidden(bool h) { a11y_hidden_ = h; }
  bool a11y_hidden() const { return a11y_hidden_; }
  void set_a11y_toggle_getter(std::function<std::optional<bool>()> fn) {
    a11y_toggle_getter_ = std::move(fn);
  }
  void set_a11y_expanded_getter(std::function<std::optional<bool>()> fn) {
    a11y_expanded_getter_ = std::move(fn);
  }

  virtual bool a11y_subtree_visible() const { return visible_ && !a11y_hidden_; }
  virtual bool a11y_is_modal() const { return false; }
  virtual A11yRole a11y_default_role() const { return A11yRole::None; }
  virtual std::wstring a11y_default_name() const { return {}; }
  std::wstring a11y_name() const;
  virtual std::wstring a11y_value() const { return {}; }
  virtual std::wstring a11y_help_text() const;
  virtual bool a11y_is_focusable() const { return wants_key() && enabled_; }
  virtual bool a11y_can_invoke() const { return false; }
  virtual bool a11y_invoke() { return false; }
  virtual std::optional<bool> a11y_toggle_state() const {
    return a11y_toggle_getter_ ? a11y_toggle_getter_() : std::nullopt;
  }
  virtual bool a11y_toggle() {
    return a11y_toggle_state().has_value() ? a11y_invoke() : false;
  }
  virtual bool a11y_can_set_value() const { return false; }
  virtual bool a11y_set_value(const std::wstring& v) {
    (void)v;
    return false;
  }
  virtual std::optional<A11yRange> a11y_range() const { return std::nullopt; }
  virtual bool a11y_set_range(double v) {
    (void)v;
    return false;
  }
  virtual std::optional<bool> a11y_is_selected() const { return std::nullopt; }
  virtual bool a11y_select() { return false; }
  virtual std::optional<bool> a11y_expanded() const {
    return a11y_expanded_getter_ ? a11y_expanded_getter_() : std::nullopt;
  }
  virtual bool a11y_expand_collapse(bool expand) {
    (void)expand;
    return a11y_expanded().has_value() ? a11y_invoke() : false;
  }
  virtual std::optional<A11yPosInSet> a11y_pos_in_set() const { return std::nullopt; }

  // 自绘复合控件的虚拟无障碍子项（如 Segmented 子段、MenuList 菜单项、弹窗自绘钮）
  virtual size_t a11y_virtual_child_count() const { return 0; }
  virtual A11ySubItem a11y_virtual_child(size_t index) const {
    (void)index;
    return {};
  }
  virtual bool a11y_invoke_virtual_child(size_t index) {
    (void)index;
    return false;
  }
  virtual bool a11y_set_virtual_child_value(size_t index, const std::wstring& v) {
    (void)index;
    (void)v;
    return false;
  }
  void ensure_i18n_fresh() const;
  // 豁免: Mini 无对应物 — 虚拟滚动容器暴露当前物化行子控件供 UIA 与键盘导航遍历（出处: 无）
  virtual void collect_transient_children(std::vector<Widget*>& out) const {
    (void)out;
  }

  // ---- flex 权重（Column/Row 主轴分配；0 = 固定尺寸） ----
  float flex_weight = 0;
  // ← CSS flex-shrink + min-width:0（ellipsis 截断场景）：flex 子自由份额
  //   小于期望宽时允许收缩到份额（如标题栏 Label），而非被 min 钳回溢出。
  bool shrink_to_free = false;

  // ---- 布局钩子 ----
  // 默认（叶/基类）：内容期望尺寸 {0,0}；容器重写
  virtual SizeF on_measure(float max_w, float max_h) {
    (void)max_w; (void)max_h;
    return {};
  }
  // 默认：子全部铺满本区域（Stack 语义）；Column/Row/ScrollList 重写
  virtual void on_arrange(const RectF& area);

  // 布局通用：padding + 背景（label/button 等无 padding 需求也复用 Box）
  struct Padding {
    float l = 0, t = 0, r = 0, b = 0;
    float h() const { return l + r; }
    float v() const { return t + b; }
  };
  const Padding& padding() const { return padding_; }
  void set_padding(float l, float t, float r, float b) {
    padding_ = {l, t, r, b};
  }
  RectF content_rect() const {  // bounds 去 padding
    return {bounds_.x + padding_.l, bounds_.y + padding_.t,
            bounds_.w - padding_.h(), bounds_.h - padding_.v()};
  }

 protected:
  virtual void on_child_added(Widget* w) { (void)w; }
  virtual void on_child_removed(Widget* w) { (void)w; }

 private:
  Widget* parent_ = nullptr;
  std::vector<std::unique_ptr<Widget>> children_;
  RectF bounds_{};
  Padding padding_{};
  bool visible_ = true;
  bool enabled_ = true;
  bool clip_to_bounds_ = false;
  float opacity_ = 1.0f;  // 子树不透明度（← MUI Fade）
  int z_order_ = 0;
  std::wstring tooltip_text_;

  A11yRole a11y_role_ = A11yRole::None;
  std::wstring a11y_name_;
  std::wstring a11y_automation_id_;
  const Widget* a11y_labelled_by_ = nullptr;
  bool a11y_hidden_ = false;
  std::function<std::optional<bool>()> a11y_toggle_getter_;
  std::function<std::optional<bool>()> a11y_expanded_getter_;

  struct I18nSlot {
    std::wstring* slot;
    std::string key;
  };
  std::vector<I18nSlot> i18n_slots_;
  uint64_t root_i18n_revision_ = 0;
  mutable uint64_t widget_i18n_rev_ = 0;
  void apply_i18n();
  void refresh_i18n_tree();
};

// ---------- 全局输入路由（窗口层把 WM_MOUSE*/WM_KEY* 转成事件后调） ----------
// 语义：
// - Move：hit_test 最深 widget → hover 变化时先 Leave 旧 / Enter 新 → Move 新
//   （Move 不冒泡；hover/tooltip 由消息驱动）
// - Down/Up：命中 widget 起沿父链冒泡找第一个处理者；Down 记录 capture，
//   Up 无论位置发给 capture（拖拽滑块/开关移出仍收 Up），并清 capture
// - Wheel：命中 widget 起沿父链冒泡（scroll_list 接收）
// 返回实际接收事件的 widget（未处理返回 nullptr）
Widget* route_mouse_event(Widget* root, MouseEvent& e);
Widget* route_key_event(Widget* root, KeyEvent& e);
// 当前 hover / capture / focus（测试与 TooltipHost 用）
Widget* hovered_widget();
Widget* captured_widget();
Widget* focused_widget();
void clear_mouse_hover();  // 鼠标移出窗口（WM_MOUSELEAVE）时派发 Leave 并清 g_hover
void clear_input_state();  // 树析构前调用防悬空

}  // namespace clashwin::ui
