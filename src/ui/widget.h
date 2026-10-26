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
#include <string>
#include <vector>

#include "render_target.h"

namespace clashwin::ui {

// ---------- 输入事件（合成消息；测试直接构造后走 route_*_event） ----------
enum class MouseEventType { Enter, Leave, Move, Down, Up, Wheel };
// buttons 位：1=左 2=右 4=中
inline constexpr int kMouseLeft = 1;
inline constexpr int kMouseRight = 2;
inline constexpr int kMouseMiddle = 4;
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

class Widget {
 public:
  explicit Widget(Widget* parent = nullptr);
  virtual ~Widget();
  Widget(const Widget&) = delete;
  Widget& operator=(const Widget&) = delete;

  // ---- 树结构 ----
  Widget* parent() const { return parent_; }
  size_t child_count() const { return children_.size(); }
  Widget* child(size_t i) const { return children_[i].get(); }
  void add_child(std::unique_ptr<Widget> w);          // 尾部 = 最上层
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
  bool focused() const;                 // route 层焦点 == 本 widget
  void request_focus();                 // wants_key 才有意义
  void blur();
  virtual bool wants_key() const { return false; }  // 焦点候选（textfield）

  // ---- 两阶段布局 ----
  SizeF measure(float max_w, float max_h);   // 期望尺寸（含 padding）
  void layout_tree(const RectF& area);       // 落位 + 递归布置子
  void relayout();  // 根调用：按根 bounds 重排整树（measure+arrange 快捷方式）

  // ---- 绘制 ----
  void paint(IRenderTarget& rt, const RectF& clip);
  void paint_all(IRenderTarget& rt);  // 根入口（clip = 全域）
  virtual void on_paint(IRenderTarget& rt) { (void)rt; }

  // ---- 输入（返回 true = 已处理，停止冒泡） ----
  virtual bool on_mouse(const MouseEvent& e) { (void)e; return false; }
  virtual bool on_key(const KeyEvent& e) { (void)e; return false; }

  // ---- 命中 ----
  // 全局坐标 → 最深可见启用 widget（可为子）；未命中返回 nullptr
  Widget* hit_test(PointF global);
  // 矩形命中默认实现（local 为相对自身原点的坐标）；圆角/异形可重载
  virtual bool hit_self(PointF local) const {
    return local.x >= 0 && local.y >= 0 && local.x < bounds_.w &&
           local.y < bounds_.h;
  }

  // ---- 脏区上报（全局逻辑坐标） ----
  void invalidate();                       // 整个自身
  void invalidate(const RectF& local);     // 局部（父坐标系内再转全局）

  // ---- tooltip 钩子（TooltipHost 扫描用；返回空=无提示） ----
  virtual std::wstring tooltip_text() const { return {}; }

  // ---- flex 权重（Column/Row 主轴分配；0 = 固定尺寸） ----
  float flex_weight = 0;

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
  int z_order_ = 0;
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
void clear_input_state();  // 树析构前调用防悬空

}  // namespace clashwin::ui
