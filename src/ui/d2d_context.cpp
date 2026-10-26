// D2D 渲染上下文（T10）：工厂三件套生命周期 + HWND/WIC 双后端渲染目标。
// - HWND 后端：T11 主窗口用（HwndRenderTarget， Present 由 EndDraw 完成）
// - WIC 后端：离屏渲染，测试做像素级断言（CPU 可读 BGRA）
// - DPI：PerMonitorV2；创建时取窗口/指定 DPI，所有入参逻辑坐标 ×(dpi/96)
// - 设备丢失：EndDraw 返回 D2DERR_RECREATE_TARGET → is_lost，外部调 recreate
#include "d2d_context.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <windows.h>

#include <format>
#include <unordered_map>

#include "util/log.h"

namespace clashwin::ui {

// ---------- Color4f ----------
Color4f Color4f::lerp(const Color4f& a, const Color4f& b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
          a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// ---------- 全局单点 ----------
namespace {
FrameClock* g_frame_clock = nullptr;
ITextEngine* g_text_engine = nullptr;
}  // namespace

FrameClock* frame_clock() { return g_frame_clock; }
void set_frame_clock(FrameClock* clock) { g_frame_clock = clock; }
ITextEngine* text_engine() { return g_text_engine; }
void set_text_engine(ITextEngine* engine) { g_text_engine = engine; }

// ---------- ComPtr（最小 RAII） ----------
template <typename T>
class ComPtr {
 public:
  ComPtr() = default;
  ~ComPtr() { reset(); }
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  T** operator&() { return &ptr_; }
  T* operator->() const { return ptr_; }
  T* get() const { return ptr_; }
  void reset() {
    if (ptr_) { ptr_->Release(); ptr_ = nullptr; }
  }
 private:
  T* ptr_ = nullptr;
};

// ---------- D2dContext：工厂三件套 ----------
D2dContext::D2dContext() = default;
D2dContext::~D2dContext() { shutdown(); }

bool D2dContext::init() {
  if (inited_) return true;
  if (!wic_inited_) {
    HRESULT hr = CoInitializeEx(nullptr,
                                COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    // S_FALSE = 已初始化，同样算成功
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
      CWLOG_ERROR("D2dContext: CoInitializeEx 失败");
      return false;
    }
    wic_inited_ = true;
  }
  HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_);
  if (FAILED(hr)) {
    CWLOG_ERROR("D2dContext: D2D1CreateFactory 失败");
    return false;
  }
  hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                           reinterpret_cast<IUnknown**>(&dwrite_factory_));
  if (FAILED(hr)) {
    CWLOG_ERROR("D2dContext: DWriteCreateFactory 失败");
    return false;
  }
  hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                        __uuidof(IWICImagingFactory),
                        reinterpret_cast<void**>(&wic_factory_));
  if (FAILED(hr)) {
    CWLOG_ERROR("D2dContext: WICImagingFactory 失败");
    return false;
  }
  inited_ = true;
  CWLOG_INFO("D2dContext: D2D/DWrite/WIC 工厂就绪");
  return true;
}

void D2dContext::shutdown() {
  if (d2d_factory_) { d2d_factory_->Release(); d2d_factory_ = nullptr; }
  if (dwrite_factory_) { dwrite_factory_->Release(); dwrite_factory_ = nullptr; }
  if (wic_factory_) { wic_factory_->Release(); wic_factory_ = nullptr; }
  inited_ = false;
}

ID2D1Factory* D2dContext::d2d() const { return d2d_factory_; }
IDWriteFactory* D2dContext::dwrite() const { return dwrite_factory_; }
IWICImagingFactory* D2dContext::wic() const { return wic_factory_; }

// ---------- D2dRenderTarget（IRenderTarget 实现） ----------
D2dRenderTarget::D2dRenderTarget(D2dContext& ctx) : ctx_(ctx) {
  set_text_engine(this);
}

D2dRenderTarget::~D2dRenderTarget() { destroy(); }

// ---- HWND 后端 ----
bool D2dRenderTarget::create_for_hwnd(HWND hwnd) {
  destroy();
  if (!ctx_.init()) return false;
  hwnd_ = hwnd;
  dpi_ = static_cast<float>(GetDpiForWindow(hwnd));
  if (dpi_ <= 0) dpi_ = 96.0f;
  RECT rc;
  GetClientRect(hwnd, &rc);
  D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left),
                                 static_cast<UINT32>(rc.bottom - rc.top));
  HRESULT hr = ctx_.d2d()->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(),
      D2D1::HwndRenderTargetProperties(hwnd, size), &hwnd_rt_);
  if (FAILED(hr)) {
    CWLOG_ERROR(std::format("D2dRenderTarget: CreateHwndRenderTarget 失败 0x{:08X}",
                            static_cast<unsigned>(hr)));
    return false;
  }
  active_rt_ = hwnd_rt_;
  on_target_ready();
  CWLOG_INFO(std::format("D2dRenderTarget: HWND target 就绪 dpi={}", dpi_));
  return true;
}

// ---- WIC 离屏后端（测试/像素断言用） ----
bool D2dRenderTarget::create_offscreen(int width_px, int height_px, float dpi) {
  destroy();
  if (!ctx_.init()) return false;
  dpi_ = dpi <= 0 ? 96.0f : dpi;
  HRESULT hr = ctx_.wic()->CreateBitmap(static_cast<UINT>(width_px),
                                        static_cast<UINT>(height_px),
                                        GUID_WICPixelFormat32bppPBGRA,
                                        WICBitmapCacheOnDemand, &wic_bitmap_);
  if (FAILED(hr)) return false;
  hr = ctx_.d2d()->CreateWicBitmapRenderTarget(
      wic_bitmap_,
      D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                   D2D1::PixelFormat(
                                       DXGI_FORMAT_B8G8R8A8_UNORM,
                                       D2D1_ALPHA_MODE_PREMULTIPLIED)),
      &wic_rt_);
  if (FAILED(hr)) return false;
  active_rt_ = wic_rt_;
  on_target_ready();
  return true;
}

void D2dRenderTarget::on_target_ready() {
  origin_ = {};
  apply_transform();
  // WIC 软件目标不支持 ClearType（静默不渲染文本），统一灰度抗锯齿
  active_rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  logical_size_ = {static_cast<float>(pixel_width()) / scale(),
                   static_cast<float>(pixel_height()) / scale()};
  lost_ = false;
}

void D2dRenderTarget::apply_transform() {
  // 物理 = (逻辑 + origin) × scale → 先平移后缩放
  float s = scale();
  active_rt_->SetTransform(D2D1::Matrix3x2F::Scale(D2D1::SizeF(s, s)) *
                           D2D1::Matrix3x2F::Translation(origin_.x, origin_.y));
}

void D2dRenderTarget::destroy() {
  brushes_.clear();
  formats_.clear();
  bitmaps_.clear();
  if (hwnd_rt_) { hwnd_rt_->Release(); hwnd_rt_ = nullptr; }
  if (wic_rt_) { wic_rt_->Release(); wic_rt_ = nullptr; }
  if (wic_bitmap_) { wic_bitmap_->Release(); wic_bitmap_ = nullptr; }
  active_rt_ = nullptr;
  hwnd_ = nullptr;
}

int D2dRenderTarget::pixel_width() const {
  if (!active_rt_) return 0;
  D2D1_SIZE_U s = active_rt_->GetPixelSize();
  return static_cast<int>(s.width);
}

int D2dRenderTarget::pixel_height() const {
  if (!active_rt_) return 0;
  D2D1_SIZE_U s = active_rt_->GetPixelSize();
  return static_cast<int>(s.height);
}

bool D2dRenderTarget::resize(int width_px, int height_px) {
  if (!active_rt_ || wic_rt_) return false;  // HWND 专用（WIC 由重建换尺寸）
  HRESULT hr = hwnd_rt_->Resize(
      D2D1::SizeU(static_cast<UINT32>(width_px), static_cast<UINT32>(height_px)));
  if (FAILED(hr)) return false;
  on_target_ready();
  return true;
}

// ---- 设备丢失恢复 ----
bool D2dRenderTarget::recreate() {
  if (hwnd_) {
    int w = pixel_width(), h = pixel_height();
    destroy();
    if (!ctx_.init()) return false;
    dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
    if (dpi_ <= 0) dpi_ = 96.0f;
    HRESULT hr = ctx_.d2d()->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(w, h)), &hwnd_rt_);
    if (FAILED(hr)) return false;
    active_rt_ = hwnd_rt_;
    on_target_ready();
    CWLOG_INFO("D2dRenderTarget: 设备丢失后已重建 HWND target");
    return true;
  }
  if (wic_bitmap_) {
    int w = pixel_width(), h = pixel_height();
    float dpi = dpi_;
    return create_offscreen(w, h, dpi);
  }
  return false;
}

// ---- 画刷缓存 ----
ID2D1SolidColorBrush* D2dRenderTarget::brush(const Color4f& c) {
  auto it = brushes_.find(key_of(c));
  if (it != brushes_.end()) return it->second;
  ID2D1SolidColorBrush* b = nullptr;
  active_rt_->CreateSolidColorBrush(D2D1::ColorF(c.r, c.g, c.b, c.a), &b);
  if (!b) return nullptr;
  brushes_.emplace(key_of(c), b);
  return b;
}

uint32_t D2dRenderTarget::key_of(const Color4f& c) {
  auto q = [](float v) { return static_cast<uint8_t>(v * 255.0f + 0.5f); };
  return (q(c.r) << 24) | (q(c.g) << 16) | (q(c.b) << 8) | q(c.a);
}

// ---- 文本格式缓存 ----
IDWriteTextFormat* D2dRenderTarget::format(const TextStyle& ts) {
  uint64_t key = static_cast<uint64_t>(static_cast<uint32_t>(ts.size * 4)) ^
                 (static_cast<uint64_t>(ts.weight) << 32) ^
                 (static_cast<uint64_t>(ts.italic) << 40);
  // family 哈希（FNV-1a 32）
  uint64_t h = 2166136261u;
  for (wchar_t ch : ts.font_family) {
    h ^= static_cast<uint64_t>(ch);
    h *= 16777619u;
  }
  key ^= h << 8;
  auto it = formats_.find(key);
  if (it != formats_.end()) return it->second;

  IDWriteTextFormat* f = nullptr;
  HRESULT hr = ctx_.dwrite()->CreateTextFormat(
      ts.font_family.c_str(), nullptr,
      static_cast<DWRITE_FONT_WEIGHT>(ts.weight), DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, ts.size * scale(), L"", &f);
  if (FAILED(hr)) return nullptr;
  formats_.emplace(key, f);
  return f;
}

// ---- ITextEngine ----
SizeF D2dRenderTarget::measure_text(const std::wstring& text,
                                    const TextStyle& ts, float max_w) {
  if (!ctx_.init() || text.empty()) return {};
  // 测量缓存（文本×样式×宽度），热点路径（label 布局/光标定位）O(1)
  uint64_t h = 1469598103934665603ull;
  for (wchar_t ch : text) { h ^= ch; h *= 1099511628211ull; }
  h ^= static_cast<uint64_t>(static_cast<uint32_t>(ts.size * 4));
  h ^= static_cast<uint64_t>(ts.weight) << 24;
  h ^= static_cast<uint64_t>(ts.wrap) << 40;
  h ^= static_cast<uint64_t>(static_cast<int32_t>(max_w * 4)) << 41;
  auto it = measure_cache_.find(h);
  if (it != measure_cache_.end()) return it->second;

  IDWriteTextLayout* layout = nullptr;
  HRESULT hr = ctx_.dwrite()->CreateTextLayout(
      text.c_str(), static_cast<UINT32>(text.size()),
      layout_for_measure(ts), max_w > 0 ? max_w * scale() : 1e6f,
      1e6f, &layout);
  if (FAILED(hr) || !layout) return {};
  DWRITE_TEXT_METRICS m;
  layout->GetMetrics(&m);
  layout->Release();
  SizeF s{m.widthIncludingTrailingWhitespace / scale(), m.height / scale()};
  measure_cache_.emplace(h, s);
  if (measure_cache_.size() > 4096) measure_cache_.clear();  // 防膨胀
  return s;
}

// 复用 draw_text 的 format 缓存
IDWriteTextFormat* D2dRenderTarget::layout_for_measure(const TextStyle& ts) {
  return format(ts);
}

// ---- IRenderTarget ----
void D2dRenderTarget::clear(const Color4f& c) {
  if (active_rt_) active_rt_->Clear(D2D1::ColorF(c.r, c.g, c.b, c.a));
}

void D2dRenderTarget::fill_rect(const RectF& r, const Brush& b) {
  if (!active_rt_) return;
  ID2D1SolidColorBrush* br = brush(b.color);
  if (br) active_rt_->FillRectangle(D2D1::RectF(r.x, r.y, r.right(), r.bottom()), br);
}

void D2dRenderTarget::round_rect(const RectF& r, float radius, const Pen& p,
                                 const Brush* fill) {
  if (!active_rt_) return;
  D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
      D2D1::RectF(r.x, r.y, r.right(), r.bottom()), radius, radius);
  if (fill) {
    ID2D1SolidColorBrush* br = brush(fill->color);
    if (br) active_rt_->FillRoundedRectangle(rr, br);
  }
  if (p.width > 0) {
    ID2D1SolidColorBrush* br = brush(p.color);
    if (br) active_rt_->DrawRoundedRectangle(rr, br, p.width);
  }
}

void D2dRenderTarget::draw_line(const PointF& a, const PointF& b, const Pen& p) {
  if (!active_rt_) return;
  ID2D1SolidColorBrush* br = brush(p.color);
  if (br)
    active_rt_->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), br,
                         p.width);
}

void D2dRenderTarget::draw_text(const std::wstring& text, const RectF& r,
                                const TextStyle& ts) {
  if (!active_rt_ || text.empty()) return;
  IDWriteTextFormat* f = format(ts);
  if (!f) return;
  IDWriteTextLayout* layout = nullptr;
  HRESULT hr = ctx_.dwrite()->CreateTextLayout(
      text.c_str(), static_cast<UINT32>(text.size()), f,
      r.w * scale(), r.h * scale(), &layout);
  if (FAILED(hr) || !layout) return;

  layout->SetTextAlignment(
      ts.align_h == AlignH::Left    ? DWRITE_TEXT_ALIGNMENT_LEADING
      : ts.align_h == AlignH::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                     : DWRITE_TEXT_ALIGNMENT_TRAILING);
  layout->SetParagraphAlignment(
      ts.align_v == AlignV::Top      ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
      : ts.align_v == AlignV::Center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                     : DWRITE_PARAGRAPH_ALIGNMENT_FAR);
  layout->SetWordWrapping(
      ts.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  if (ts.ellipsis) {
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    layout->SetTrimming(&trim, nullptr);
  }
  // max_lines：DWrite 无直接接口，超出按行高截断 + trimming 省略（工具箱近似）
  if (ts.max_lines > 0 && ts.wrap) {
    float line_h = ts.size * 1.33f;
    float max_h = ts.max_lines * line_h;
    if (r.h > max_h) {
      RectF rr = r;
      rr.h = max_h;
      DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
      layout->SetTrimming(&trim, nullptr);
      ID2D1SolidColorBrush* br = brush(ts.color);
      if (br) active_rt_->DrawTextLayout(D2D1::Point2F(rr.x, rr.y), layout, br);
      layout->Release();
      return;
    }
  }
  ID2D1SolidColorBrush* br = brush(ts.color);
  if (br) active_rt_->DrawTextLayout(D2D1::Point2F(r.x, r.y), layout, br);
  layout->Release();
}

void D2dRenderTarget::draw_bitmap(uint64_t id, const RectF& r, float opacity) {
  if (!active_rt_) return;
  auto it = bitmaps_.find(id);
  if (it == bitmaps_.end()) return;
  active_rt_->DrawBitmap(it->second,
                         D2D1::RectF(r.x, r.y, r.right(), r.bottom()),
                         opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void D2dRenderTarget::register_bitmap(uint64_t id, IWICBitmapSource* src) {
  if (!active_rt_) return;
  ID2D1Bitmap* bmp = nullptr;
  HRESULT hr = active_rt_->CreateBitmapFromWicBitmap(src, &bmp);
  if (FAILED(hr)) return;
  bitmaps_[id] = bmp;
}

void D2dRenderTarget::set_origin(const PointF& global_origin) {
  origin_ = global_origin;
  if (active_rt_) apply_transform();
}

void D2dRenderTarget::push_clip(const RectF& r) {
  if (active_rt_)
    active_rt_->PushAxisAlignedClip(
        D2D1::RectF(r.x, r.y, r.right(), r.bottom()),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void D2dRenderTarget::pop_clip() {
  if (active_rt_) active_rt_->PopAxisAlignedClip();
}

float D2dRenderTarget::scale() const { return dpi_ / 96.0f; }
SizeF D2dRenderTarget::size() const { return logical_size_; }
bool D2dRenderTarget::is_lost() const { return lost_; }

// ---- 帧：Begin/End + 丢失检测 ----
void D2dRenderTarget::begin_frame() {
  if (active_rt_) active_rt_->BeginDraw();
}

bool D2dRenderTarget::end_frame() {
  if (!active_rt_) return false;
  HRESULT hr = active_rt_->EndDraw();
  if (FAILED(hr))
  if (hr == D2DERR_RECREATE_TARGET) {
    lost_ = true;
    CWLOG_WARN("D2dRenderTarget: D2DERR_RECREATE_TARGET（设备丢失，待重建）");
    return false;
  }
  return SUCCEEDED(hr);
}

// ---- 测试读像素（仅 WIC 后端）：返回 BGRA ----
bool D2dRenderTarget::read_pixels(std::vector<uint8_t>& bgra, int& w, int& h) {
  if (!wic_bitmap_) return false;
  w = pixel_width();
  h = pixel_height();
  bgra.resize(static_cast<size_t>(w) * h * 4);
  return SUCCEEDED(
      wic_bitmap_->CopyPixels(nullptr, w * 4,
                              static_cast<UINT>(bgra.size()), bgra.data()));
}

}  // namespace clashwin::ui
