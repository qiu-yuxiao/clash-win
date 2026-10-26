#pragma once
// 渲染目标接口层（T10）：Widget 树唯一绘制面 + 30fps 合帧时钟。
// - 基础类型：PointF/SizeF/RectF/Color4f/Pen/Brush/TextStyle（逻辑像素坐标）
// - IRenderTarget：fill/round/line/text/bitmap/clip，DPI 缩放由实现内部承担
//   （§3.3 接口原样，draw_bitmap 的位图注册归 T12 icon_store）
// - ITextEngine：文本测量（DirectWrite，不依赖 render target 可独立调用）
// - FrameClock：脏区合并 + 30fps 合帧泵的调度核心（§5.4：事件驱动失效，
//   WM_TIMER 33ms 合帧；本类只管合并与派发，定时器由 T11 窗口层驱动 flush()）
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace clashwin::ui {

struct PointF {
  float x = 0, y = 0;
};

struct SizeF {
  float w = 0, h = 0;
};

struct RectF {
  float x = 0, y = 0, w = 0, h = 0;
  float right() const { return x + w; }
  float bottom() const { return y + h; }
  bool is_empty() const { return w <= 0 || h <= 0; }
  bool contains(float px, float py) const {
    return px >= x && px < right() && py >= y && py < bottom();
  }
  bool contains(const RectF& o) const {
    return o.x >= x && o.right() <= right() && o.y >= y && o.bottom() <= bottom();
  }
  bool intersects(const RectF& o) const {
    return !is_empty() && !o.is_empty() && x < o.right() && o.x < right() &&
           y < o.bottom() && o.y < bottom();
  }
  RectF intersect(const RectF& o) const {
    if (!intersects(o)) return {};
    float nx = x > o.x ? x : o.x, ny = y > o.y ? y : o.y;
    return {nx, ny, (right() < o.right() ? right() : o.right()) - nx,
            (bottom() < o.bottom() ? bottom() : o.bottom()) - ny};
  }
  static RectF union_of(const RectF& a, const RectF& b) {
    if (a.is_empty()) return b;
    if (b.is_empty()) return a;
    float nx = a.x < b.x ? a.x : b.x, ny = a.y < b.y ? a.y : b.y;
    return {nx, ny, (a.right() > b.right() ? a.right() : b.right()) - nx,
            (a.bottom() > b.bottom() ? a.bottom() : b.bottom()) - ny};
  }
  float area() const { return is_empty() ? 0 : w * h; }
};

struct Color4f {
  float r = 0, g = 0, b = 0, a = 1;
  static constexpr Color4f hex(uint32_t rgb, float alpha = 1.0f) {
    return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
            (rgb & 0xFF) / 255.0f, alpha};
  }
  // 线性插值（switch/segmented 动画进度用）
  static Color4f lerp(const Color4f& a, const Color4f& b, float t);
};

// 字体粗细（DirectWrite DWRITE_FONT_WEIGHT 同值）
enum class Weight : int { Normal = 400, Medium = 500, Bold = 700 };
enum class AlignH { Left, Center, Right };
enum class AlignV { Top, Center, Bottom };

struct TextStyle {
  std::wstring font_family = L"Segoe UI";
  float size = 12;                 // 逻辑 px（MUI 基准 fontSize 12）
  int weight = static_cast<int>(Weight::Normal);
  bool italic = false;
  Color4f color{0, 0, 0, 1};
  AlignH align_h = AlignH::Left;
  AlignV align_v = AlignV::Top;
  bool wrap = false;               // 自动换行
  bool ellipsis = false;           // 末尾省略号（trimming）
  int max_lines = 0;               // 0 = 不限；配合 wrap 生效
};

struct Brush {
  Color4f color;
};

struct Pen {
  Color4f color;
  float width = 1;
};

// ---------- Widget 树唯一绘制接口（逻辑像素；实现负责乘 DPI scale） ----------
class IRenderTarget {
 public:
  virtual ~IRenderTarget() = default;
  virtual void clear(const Color4f& c) = 0;
  virtual void fill_rect(const RectF& r, const Brush& b) = 0;
  virtual void round_rect(const RectF& r, float radius, const Pen& p,
                          const Brush* fill) = 0;
  virtual void draw_line(const PointF& a, const PointF& b, const Pen& p) = 0;
  virtual void draw_text(const std::wstring& text, const RectF& r,
                         const TextStyle& ts) = 0;
  // 位图：id 由 T12 icon_store 登记（register_bitmap）；未登记则不绘制
  virtual void draw_bitmap(uint64_t id, const RectF& r, float opacity) = 0;
  virtual void push_clip(const RectF& r) = 0;
  virtual void pop_clip() = 0;
  // 绘制原点（全局逻辑坐标）：on_paint 约定用局部坐标 {0,0,w,h}，
  // Widget::paint 在调用前把 origin 设为该 widget 的全局原点（实现经
  // transform 平移），使局部绘制自动落到全局正确位置。
  virtual void set_origin(const PointF& global_origin) = 0;
  virtual PointF origin() const = 0;
  virtual float scale() const = 0;      // dpi / 96
  virtual SizeF size() const = 0;       // 逻辑尺寸（物理 / scale）
  virtual bool is_lost() const = 0;     // EndDraw 返回 RECREATE_TARGET 后 true
};

// ---------- 文本测量（布局两阶段用；DWrite 独立于 target） ----------
class ITextEngine {
 public:
  virtual ~ITextEngine() = default;
  // max_w>0 时 wrap 生效按宽换行测量；返回内容期望尺寸
  virtual SizeF measure_text(const std::wstring& text, const TextStyle& ts,
                             float max_w) = 0;
};
ITextEngine* text_engine();
void set_text_engine(ITextEngine* engine);

// ---------- 帧调度：脏区合并 + 30fps 合帧（§5.4 主渲染策略） ----------
// 用法（T11）：bind(重绘回调) → WM_TIMER 33ms 调 flush()（仅 has_dirty 才绘制）；
// Widget::invalidate() 汇入本类。测试直接注入后手动 flush 断言。
class FrameClock {
 public:
  using PaintFn = std::function<void(const RectF& dirty)>;

  void bind(PaintFn fn) { paint_fn_ = std::move(fn); }
  void unbind() { paint_fn_ = nullptr; }

  // 合并脏区（全局逻辑坐标）。合并结果锁定为外接矩形（与 Win32
  // InvalidateRect+GetUpdateRect 语义一致）。
  void invalidate(const RectF& dirty) { dirty_ = RectF::union_of(dirty_, dirty); }

  bool has_dirty() const { return !dirty_.is_empty(); }
  RectF dirty() const { return dirty_; }

  // 取出当前合并脏区（不重绘；供统计/测试）
  RectF take_dirty() {
    RectF d = dirty_;
    dirty_ = {};
    return d;
  }

  // 有脏区则回调一次并清空；返回是否绘制
  bool flush() {
    if (!has_dirty()) return false;
    RectF d = take_dirty();
    if (paint_fn_) paint_fn_(d);
    return true;
  }

  // 30fps 帧间隔（ms）常量——T11 SetTimer 用
  static constexpr int kFrameIntervalMs = 33;

 private:
  RectF dirty_{};
  PaintFn paint_fn_;
};

// 进程级帧时钟（Widget::invalidate 汇入点；窗口层 T11 bind）。
FrameClock* frame_clock();
void set_frame_clock(FrameClock* clock);

}  // namespace clashwin::ui
