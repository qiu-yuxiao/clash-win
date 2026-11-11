#pragma once
// D2D 渲染上下文（T10）：工厂三件套 + HWND/WIC 双后端渲染目标。
// 前端普查对照：MUI/Web 的绘制面 → 原生侧收敛到本文件唯一封装。
// - D2dContext：ID2D1Factory / IDWriteFactory / IWICImagingFactory 生命周期
// - D2dRenderTarget：IRenderTarget 实现；HWND（T11 窗口）与 WIC 离屏（测试）
//   双后端；DPI 感知（PerMonitorV2，逻辑坐标 = 96dpi 基准）；设备丢失重建
#pragma once

#include <windows.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

struct ID2D1Factory;
struct ID2D1RenderTarget;
struct ID2D1HwndRenderTarget;
struct ID2D1SolidColorBrush;
struct ID2D1Bitmap;
struct ID2D1Layer;
struct IDWriteFactory;
struct IDWriteTextFormat;
struct IWICImagingFactory;
struct IWICBitmap;
struct IWICBitmapSource;

#include "render_target.h"

namespace clashwin::ui {

class D2dContext {
 public:
  D2dContext();
  ~D2dContext();
  D2dContext(const D2dContext&) = delete;
  D2dContext& operator=(const D2dContext&) = delete;

  bool init();
  void shutdown();
  bool inited() const { return inited_; }

  ID2D1Factory* d2d() const;
  IDWriteFactory* dwrite() const;
  IWICImagingFactory* wic() const;

 private:
  bool wic_inited_ = false;
  bool inited_ = false;
  ID2D1Factory* d2d_factory_ = nullptr;
  IDWriteFactory* dwrite_factory_ = nullptr;
  IWICImagingFactory* wic_factory_ = nullptr;
};

class D2dRenderTarget : public IRenderTarget, public ITextEngine {
 public:
  explicit D2dRenderTarget(D2dContext& ctx);
  ~D2dRenderTarget() override;
  D2dRenderTarget(const D2dRenderTarget&) = delete;
  D2dRenderTarget& operator=(const D2dRenderTarget&) = delete;

  // HWND 后端（T11 主窗口；dpi 取自窗口）
  bool create_for_hwnd(HWND hwnd);
  // WIC 离屏后端（测试像素断言；dpi 模拟显示器 DPI，144 = 150%）
  bool create_offscreen(int width_px, int height_px, float dpi = 96.0f);
  void destroy();

  // 帧：paint 回调内 begin → Widget 树绘制 → end
  void begin_frame();
  bool end_frame();  // false = 设备丢失（is_lost）

  bool resize(int width_px, int height_px);  // 仅 HWND 后端
  bool recreate();                           // 设备丢失全量重建
  float dpi() const { return dpi_; }
  int pixel_width() const;
  int pixel_height() const;

  // 位图登记（T12 icon_store 调用；draw_bitmap 按 id 引用）
  void register_bitmap(uint64_t id, IWICBitmapSource* src);
  bool has_bitmap(uint64_t id) const;  // 设备重建后位图清空，需重新登记

  // 测试专用：读取 WIC 位图像素（BGRA），仅离屏后端
  bool read_pixels(std::vector<uint8_t>& bgra, int& w, int& h);

  // ---- IRenderTarget ----
  void clear(const Color4f& c) override;
  void fill_rect(const RectF& r, const Brush& b) override;
  void round_rect(const RectF& r, float radius, const Pen& p,
                  const Brush* fill) override;
  void fill_round_rect_gradient(const RectF& r, float radius,
                                const GradientStop* stops, int count) override;
  void fill_round_rect_gradient_angle(const RectF& r, float radius,
                                      const GradientStop* stops, int count,
                                      float css_angle_deg,
                                      int corner_mask = 0xF) override;
  void fill_round_rect_radial(const RectF& r, float radius,
                              const GradientStop* stops, int count,
                              float cx_ratio, float cy_ratio,
                              float rx, float ry) override;
  void draw_line(const PointF& a, const PointF& b, const Pen& p) override;
  void fill_path(const std::vector<std::vector<PointF>>& loops,
                 const Brush& b) override;
  void draw_text(const std::wstring& text, const RectF& r,
                 const TextStyle& ts) override;
  void draw_bitmap(uint64_t id, const RectF& r, float opacity) override;
  void push_clip(const RectF& r) override;
  void pop_clip() override;
  void push_opacity(float alpha) override;
  void pop_opacity() override;
  void set_origin(const PointF& global_origin) override;
  PointF origin() const override { return origin_; }
  float scale() const override;
  SizeF size() const override;
  bool is_lost() const override;

  // ---- ITextEngine ----
  SizeF measure_text(const std::wstring& text, const TextStyle& ts,
                     float max_w) override;
  float measure_ink_bottom(const std::wstring& text, const TextStyle& ts,
                           float box_w, float box_h) override;

 private:
  void on_target_ready();
  void apply_transform();  // SetTransform = scale × translate(origin_)
  ID2D1SolidColorBrush* brush(const Color4f& c);
  IDWriteTextFormat* format(const TextStyle& ts);
  IDWriteTextFormat* layout_for_measure(const TextStyle& ts);
  static uint32_t key_of(const Color4f& c);

  D2dContext& ctx_;
  HWND hwnd_ = nullptr;
  ID2D1HwndRenderTarget* hwnd_rt_ = nullptr;
  ID2D1RenderTarget* wic_rt_ = nullptr;      // 不 Release（非引用计数双记）
  IWICBitmap* wic_bitmap_ = nullptr;
  ID2D1RenderTarget* active_rt_ = nullptr;
  float dpi_ = 96.0f;
  SizeF logical_size_{};
  PointF origin_{};  // 当前绘制原点（全局逻辑坐标）
  bool lost_ = false;

  std::unordered_map<uint32_t, ID2D1SolidColorBrush*> brushes_;
  std::unordered_map<uint64_t, IDWriteTextFormat*> formats_;
  std::unordered_map<uint64_t, ID2D1Bitmap*> bitmaps_;
  std::unordered_map<uint64_t, SizeF> measure_cache_;
  // 不透明度层：D2D 的 layer 资源可重复使用（同一次 BeginDraw 内可多次
  // Push/Pop），只保留一个；opacity_pushed_ 逐层记录「是否真推了 layer」
  // （满不透明/创建失败时为空推），保证 Push/Pop 严格配对。
  ID2D1Layer* opacity_layer_ = nullptr;
  std::vector<bool> opacity_pushed_;
};

}  // namespace clashwin::ui
