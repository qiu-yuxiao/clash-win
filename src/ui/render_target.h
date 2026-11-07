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
  static constexpr Color4f hex(uint32_t rgb, float alpha = 1.0f) {  // 豁免: Mini 无对应物 — 颜色工具缺省 alpha 不透明
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
  // ★ 空 = 跟随当前皮肤的全局字体（← Mini layout.scss:319 --control-font-family）
  //   非空 = 显式指定（图标字体 Segoe MDL2 Assets / Segoe Fluent Icons、
  //   代码框 Consolas 等）。皮肤字体候选链见 theme::skin_font_candidates。
  std::wstring font_family;
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

struct GradientStop {
  float offset;  // 0..1
  Color4f color;
};

// ---------- Widget 树唯一绘制接口（逻辑像素；实现负责乘 DPI scale） ----------
class IRenderTarget {
 public:
  virtual ~IRenderTarget() = default;
  virtual void clear(const Color4f& c) = 0;
  virtual void fill_rect(const RectF& r, const Brush& b) = 0;
  virtual void round_rect(const RectF& r, float radius, const Pen& p,
                          const Brush* fill) = 0;
  // 水平线性渐变圆角填充（← Mini 流量卡；stops 按 offset 升序）
  virtual void fill_round_rect_gradient(const RectF& r, float radius,
                                        const GradientStop* stops,
                                        int count) {
    (void)r; (void)radius; (void)stops; (void)count;
  }
  // 圆角矩形 + CSS 角度线性渐变（css_angle_deg：0=to top，顺时针；135=to 右下）。
  // 对齐 CSS linear-gradient 渐变线投影：d=(sinθ,-cosθ)，L=|W·sinθ|+|H·cosθ|。
  // corner_mask：bit0=TL bit1=TR bit2=BR bit3=BL（默认全角）。
  // src: mini-traffic-panel.tsx:102-104/171-173 指标卡成对接缝处内角半径 0。
  virtual void fill_round_rect_gradient_angle(const RectF& r, float radius,
                                              const GradientStop* stops,
                                              int count, float css_angle_deg,
                                              int corner_mask = 0xF) {
    // 默认退化：无角度支持时用水平渐变近似。
    (void)css_angle_deg; (void)corner_mask;
    fill_round_rect_gradient(r, radius, stops, count);
  }
  // 圆角矩形 + 径向渐变（CSS radial-gradient(circle at cx% cy%, ...)）：
  //   c 为渐变圆心（相对 r 的比例 0..1），rx/ry 为椭圆半径（DIP）。
  //   src: base-switch.tsx:76-80 / layout.scss:19-22 极光
  virtual void fill_round_rect_radial(const RectF& r, float radius,
                                      const GradientStop* stops, int count,
                                      float cx_ratio, float cy_ratio,
                                      float rx, float ry) {
    // 默认退化：无径向支持时用水平渐变近似。
    (void)cx_ratio; (void)cy_ratio; (void)rx; (void)ry;
    fill_round_rect_gradient(r, radius, stops, count);
  }
  virtual void draw_line(const PointF& a, const PointF& b, const Pen& p) = 0;
  // 折线（SVG path 扁平化后用；默认逐段 draw_line，实现可覆盖为几何 stroke）
  virtual void draw_polyline(const PointF* pts, int count, const Pen& p) {
    if (!pts || count < 2) return;
    for (int i = 1; i < count; ++i) draw_line(pts[i - 1], pts[i], p);
  }
  // 多环填充（F-D11：MUI SVG path 扁平化后的闭合环集合；偶奇填充，
  //   语义 = SVG fill-rule 摘要下的镂空 donut——外环减内环）。
  // 默认退化为逐环描边（无填充能力的测试/兼容渲染器仍可见轮廓）。
  virtual void fill_path(const std::vector<std::vector<PointF>>& loops,
                         const Brush& b) {
    Pen p{b.color, 1.0f};
    for (const auto& loop : loops)
      draw_polyline(loop.data(), static_cast<int>(loop.size()), p);
  }
  virtual void draw_text(const std::wstring& text, const RectF& r,
                         const TextStyle& ts) = 0;
  // 位图：id 由 T12 icon_store 登记（register_bitmap）；未登记则不绘制
  virtual void draw_bitmap(uint64_t id, const RectF& r, float opacity) = 0;
  virtual void push_clip(const RectF& r) = 0;
  virtual void pop_clip() = 0;
  // 子树整体不透明度：范围内的绘制按 alpha 与背景合成一次（← MUI Fade 过渡，
  // Dialog / Menu 淡入淡出）。D2D 侧用 PushLayer(opacity) 实现；
  // 缺省 no-op —— 测试替身与不需合成的实现可直接继承。
  // 调用必须成对且各自负责，异常路径不存在（纯绘制）。
  virtual void push_opacity(float alpha) { (void)alpha; }
  virtual void pop_opacity() {}
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
  static constexpr int kFrameIntervalMs = 33;  // ← 30fps 渲染节拍（1000/30）

 private:
  RectF dirty_{};
  PaintFn paint_fn_;
};

// 进程级帧时钟（Widget::invalidate 汇入点；窗口层 T11 bind）。
FrameClock* frame_clock();
void set_frame_clock(FrameClock* clock);

}  // namespace clashwin::ui
