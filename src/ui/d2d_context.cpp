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

#include <cmath>
#include <format>
#include <unordered_map>

#include "util/log.h"
#include "ui/theme/theme.h"

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

D2dRenderTarget::~D2dRenderTarget() {
  if (g_text_engine == this) g_text_engine = nullptr;
  destroy();
}

// ---- HWND 后端 ----
bool D2dRenderTarget::create_for_hwnd(HWND hwnd) {
  set_text_engine(this);
  destroy();
  if (!ctx_.init()) return false;
  hwnd_ = hwnd;
  // 【DPIARCH】target 取窗口实际 DPI（150% 屏=144）：scale()=1.5，
  // apply_transform 统一 物理=逻辑×scale（形状+文字单缩放，字号仍用 ts.size 逻辑值）。
  dpi_ = static_cast<float>(GetDpiForWindow(hwnd));
  RECT rc;
  GetClientRect(hwnd, &rc);
  D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left),
                                 static_cast<UINT32>(rc.bottom - rc.top));
  // 【DPIARCH】单缩放架构：target DPI=窗口 DPI（144），DIP→物理由 target
  // 自动完成；transform 恒等（见 apply_transform）。此前默认属性 DPI=0→
  // 跟随系统 144 + transform×1.5 = 双缩放（实测 effDpi=144/m11=1.5），已修。
  HRESULT hr = ctx_.d2d()->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(
          D2D1_RENDER_TARGET_TYPE_DEFAULT,
          D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN), dpi_,
          dpi_),
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
  set_text_engine(this);
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
                                       D2D1_ALPHA_MODE_PREMULTIPLIED),
                                   dpi_, dpi_),
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
  // 【DPIARCH-EXP】最小实验：target 有效 DPI=144（实测 effDpi=144）已自动
  // DIP→物理 ×1.5；transform 恒等（origin 为逻辑 DIP，平移在 DIP 空间，
  // 由 target DPI 统一换算），消除双缩放。
  active_rt_->SetTransform(
      D2D1::Matrix3x2F::Translation(origin_.x, origin_.y));
}

void D2dRenderTarget::destroy() {
  brushes_.clear();
  formats_.clear();
  measure_cache_.clear();  // 【FONTSIZE-FIX】测量含 dpi 因子，重建时一并失效
  bitmaps_.clear();
  if (opacity_layer_) { opacity_layer_->Release(); opacity_layer_ = nullptr; }
  opacity_pushed_.clear();
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
    HWND saved_hwnd = hwnd_;
    int w = pixel_width(), h = pixel_height();
    destroy();
    if (!ctx_.init()) return false;
    hwnd_ = saved_hwnd;
    if (w <= 0 || h <= 0) {
      RECT rc{};
      GetClientRect(hwnd_, &rc);
      w = rc.right - rc.left;
      h = rc.bottom - rc.top;
    }
    const UINT raw_dpi = GetDpiForWindow(hwnd_);
    dpi_ = raw_dpi > 0 ? static_cast<float>(raw_dpi) : 96.0f;  // ← GetDpiForWindow 换挡
    HRESULT hr = ctx_.d2d()->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN),
            dpi_, dpi_),
        D2D1::HwndRenderTargetProperties(
            hwnd_, D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h))),
        &hwnd_rt_);
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

namespace {
// ★ §2.7 皮肤字体解析：本机是否安装了某个字体
//   DirectWrite 的 GetFontCollection + FindFamilyName 是权威判据
//   （与 CSS font-family 的可用性判定一致）。
bool font_installed(IDWriteFactory* f, const std::wstring& name) {
  if (!f || name.empty()) return false;
  IDWriteFontCollection* coll = nullptr;
  if (FAILED(f->GetSystemFontCollection(&coll, FALSE)) || !coll) return false;
  UINT32 index = 0;
  BOOL exists = FALSE;
  const HRESULT hr = coll->FindFamilyName(name.c_str(), &index, &exists);
  coll->Release();
  return SUCCEEDED(hr) && exists == TRUE;
}

// 按候选链择一：首个本机已安装者胜出；全缺则回退 Segoe UI。
std::wstring resolve_skin_font(IDWriteFactory* f,
                                const std::vector<std::wstring>& cands) {
  for (const auto& c : cands) {
    if (font_installed(f, c)) return c;
  }
  return L"Segoe UI";
}
}  // namespace

IDWriteTextFormat* D2dRenderTarget::format(const TextStyle& ts) {
  // 【SCALEFIX】1:1 物理像素：scale()=1.0，字号直接用 ts.size（12=12 物理），
  // 不 ×1.5 也不 /1.5；key 含 dpi_ 位仅为防御（现为常量）。
  uint64_t key = static_cast<uint64_t>(static_cast<uint32_t>(ts.size * 4)) ^
                 (static_cast<uint64_t>(ts.weight) << 32) ^
                 (static_cast<uint64_t>(ts.italic) << 40) ^
                 (static_cast<uint64_t>(static_cast<uint32_t>(dpi_)) << 48);
  // ★ §2.7 字体解析：font_family 为空 → 跟随当前皮肤
  //   （← Mini layout.scss:319 --control-font-family 全局生效）
  std::wstring family = ts.font_family;
  if (family.empty()) {
    auto& theme = theme::ThemeService::instance();
    family = theme.ui_font();
    if (family.empty()) {
      // 首次解析：按候选链择一（本机字体存在性检测需 DirectWrite）
      family = resolve_skin_font(ctx_.dwrite(),
                                 theme::skin_font_candidates(theme.skin()));
      theme.set_ui_font(family);
    }
  }
  // family 哈希（FNV-1a 32）
  uint64_t h = 2166136261u;
  for (wchar_t ch : family) {
    h ^= static_cast<uint64_t>(ch);
    h *= 16777619u;
  }
  key ^= h << 8;
  auto it = formats_.find(key);
  if (it != formats_.end()) return it->second;

  IDWriteTextFormat* f = nullptr;
  HRESULT hr = ctx_.dwrite()->CreateTextFormat(
      family.c_str(), nullptr,
      static_cast<DWRITE_FONT_WEIGHT>(ts.weight), DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, ts.size, L"", &f);
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
  // 【FONTSIZE-FIX】测量结果随 dpi 变化（format 字号含 /scale），key 必须含 dpi
  h ^= static_cast<uint64_t>(static_cast<uint32_t>(dpi_)) << 56;
  auto it = measure_cache_.find(h);
  if (it != measure_cache_.end()) return it->second;

  IDWriteTextLayout* layout = nullptr;
  HRESULT hr = ctx_.dwrite()->CreateTextLayout(
      text.c_str(), static_cast<UINT32>(text.size()),
      layout_for_measure(ts), max_w > 0 ? max_w : 1e6f,
      1e6f, &layout);
  if (FAILED(hr) || !layout) return {};
  DWRITE_TEXT_METRICS m;
  layout->GetMetrics(&m);
  layout->Release();
  SizeF s{m.widthIncludingTrailingWhitespace, m.height};
  measure_cache_.emplace(h, s);
  if (measure_cache_.size() > 4096) measure_cache_.clear();  // 防膨胀
  return s;
}

float D2dRenderTarget::measure_ink_bottom(const std::wstring& text,
                                          const TextStyle& ts, float box_w,
                                          float box_h) {
  // ← basic-settings-card.tsx:25,148 按当前系统真实解析字体测量字形底边，消除跨 OS 字体回退误差
  const float fallback = box_h * 0.735f;
  if (!ctx_.init() || text.empty() || box_w <= 0.0f || box_h <= 0.0f) {
    return fallback;
  }
  IDWriteTextFormat* fmt = format(ts);
  if (!fmt) return fallback;
  IDWriteTextLayout* layout = nullptr;
  HRESULT hr = ctx_.dwrite()->CreateTextLayout(
      text.c_str(), static_cast<UINT32>(text.size()), fmt, box_w, box_h,
      &layout);
  if (FAILED(hr) || !layout) return fallback;
  layout->SetTextAlignment(
      ts.align_h == AlignH::Left     ? DWRITE_TEXT_ALIGNMENT_LEADING
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
  DWRITE_OVERHANG_METRICS overhangs{};
  hr = layout->GetOverhangMetrics(&overhangs);
  layout->Release();
  if (FAILED(hr)) return fallback;
  const float ink_bot = box_h + overhangs.bottom;
  // 豁免: Mini 无对应物 — DirectWrite GetOverhangMetrics 极端字形越界保护（出处: 无）
  if (ink_bot <= 2.0f || ink_bot > box_h + 4.0f) return fallback;
  return ink_bot;
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

void D2dRenderTarget::fill_round_rect_gradient(const RectF& r, float radius,
                                               const GradientStop* stops,
                                               int count) {
  if (!active_rt_ || count < 2) return;
  std::vector<D2D1_GRADIENT_STOP> gs(count);
  for (int i = 0; i < count; ++i) {
    gs[i].position = stops[i].offset;
    gs[i].color = D2D1::ColorF(stops[i].color.r, stops[i].color.g,
                               stops[i].color.b, stops[i].color.a);
  }
  ID2D1GradientStopCollection* coll = nullptr;
  if (FAILED(active_rt_->CreateGradientStopCollection(gs.data(),
                                                      static_cast<UINT32>(count),
                                                      &coll)) ||
      !coll)
    return;
  ID2D1LinearGradientBrush* br = nullptr;
  HRESULT hr = active_rt_->CreateLinearGradientBrush(
      D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y),
                                          D2D1::Point2F(r.right(), r.y)),
      coll, &br);
  coll->Release();
  if (FAILED(hr) || !br) return;
  D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
      D2D1::RectF(r.x, r.y, r.right(), r.bottom()), radius, radius);
  active_rt_->FillRoundedRectangle(rr, br);
  br->Release();
}

void D2dRenderTarget::fill_round_rect_gradient_angle(
    const RectF& r, float radius, const GradientStop* stops, int count,
    float css_angle_deg, int corner_mask) {
  if (!active_rt_ || count < 2) return;
  std::vector<D2D1_GRADIENT_STOP> gs(count);
  for (int i = 0; i < count; ++i) {
    gs[i].position = stops[i].offset;
    gs[i].color = D2D1::ColorF(stops[i].color.r, stops[i].color.g,
                               stops[i].color.b, stops[i].color.a);
  }
  ID2D1GradientStopCollection* coll = nullptr;
  if (FAILED(active_rt_->CreateGradientStopCollection(gs.data(),
                                                      static_cast<UINT32>(count),
                                                      &coll)) ||
      !coll)
    return;
  // CSS 渐变线：0=to top，顺时针。方向 d=(sinθ,-cosθ)（屏坐标 y 向下）；
  // 长度 L=|W·sinθ|+|H·cosθ|；起止点=center±d·L/2。
  const float th = css_angle_deg * 3.14159265358979f / 180.0f;
  float dx = std::sin(th), dy = -std::cos(th);
  float L = std::fabs(r.w * dx) + std::fabs(r.h * dy);
  float cx = r.x + r.w / 2.0f, cy = r.y + r.h / 2.0f;
  D2D1_POINT_2F p0 = {cx - dx * L / 2.0f, cy - dy * L / 2.0f};
  D2D1_POINT_2F p1 = {cx + dx * L / 2.0f, cy + dy * L / 2.0f};
  ID2D1LinearGradientBrush* br = nullptr;
  HRESULT hr = active_rt_->CreateLinearGradientBrush(
      D2D1::LinearGradientBrushProperties(p0, p1), coll, &br);
  coll->Release();
  if (FAILED(hr) || !br) return;
  if (corner_mask == 0xF) {
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(r.x, r.y, r.right(), r.bottom()), radius, radius);
    active_rt_->FillRoundedRectangle(rr, br);
  } else {
    // 分角圆角路径（src: mini-traffic-panel.tsx 成对指标卡内角半径 0）
    const float rtl = (corner_mask & 1) ? radius : 0.f;
    const float rtr = (corner_mask & 2) ? radius : 0.f;
    const float rbr = (corner_mask & 4) ? radius : 0.f;
    const float rbl = (corner_mask & 8) ? radius : 0.f;
    ID2D1PathGeometry* path = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(ctx_.d2d()->CreatePathGeometry(&path)) && path &&
        SUCCEEDED(path->Open(&sink)) && sink) {
      const float x0 = r.x, y0 = r.y, x1 = r.right(), y1 = r.bottom();
      auto arc = [&](D2D1_POINT_2F pt, float rad) {
        D2D1_ARC_SEGMENT seg{};
        seg.point = pt;
        seg.size = {rad, rad};
        seg.rotationAngle = 0.f;
        seg.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
        seg.arcSize = D2D1_ARC_SIZE_SMALL;
        sink->AddArc(seg);
      };
      sink->BeginFigure(D2D1::Point2F(x0 + rtl, y0), D2D1_FIGURE_BEGIN_FILLED);
      sink->AddLine(D2D1::Point2F(x1 - rtr, y0));
      if (rtr > 0) arc(D2D1::Point2F(x1, y0 + rtr), rtr);
      sink->AddLine(D2D1::Point2F(x1, y1 - rbr));
      if (rbr > 0) arc(D2D1::Point2F(x1 - rbr, y1), rbr);
      sink->AddLine(D2D1::Point2F(x0 + rbl, y1));
      if (rbl > 0) arc(D2D1::Point2F(x0, y1 - rbl), rbl);
      sink->AddLine(D2D1::Point2F(x0, y0 + rtl));
      if (rtl > 0) arc(D2D1::Point2F(x0 + rtl, y0), rtl);
      sink->EndFigure(D2D1_FIGURE_END_CLOSED);
      sink->Close();
      sink->Release();
      active_rt_->FillGeometry(path, br);
      path->Release();
    }
  }
  br->Release();
}

void D2dRenderTarget::fill_round_rect_radial(
    const RectF& r, float radius, const GradientStop* stops, int count,
    float cx_ratio, float cy_ratio, float rx, float ry) {
  if (!active_rt_ || count < 2) return;
  std::vector<D2D1_GRADIENT_STOP> gs(count);
  for (int i = 0; i < count; ++i) {
    gs[i].position = stops[i].offset;
    gs[i].color = D2D1::ColorF(stops[i].color.r, stops[i].color.g,
                               stops[i].color.b, stops[i].color.a);
  }
  ID2D1GradientStopCollection* coll = nullptr;
  if (FAILED(active_rt_->CreateGradientStopCollection(gs.data(),
                                                      static_cast<UINT32>(count),
                                                      &coll)) ||
      !coll)
    return;
  // CSS radial-gradient(circle at cx cy, ...)：圆心按比例落在 r 内
  D2D1_POINT_2F center = {r.x + r.w * cx_ratio, r.y + r.h * cy_ratio};
  ID2D1RadialGradientBrush* br = nullptr;
  HRESULT hr = active_rt_->CreateRadialGradientBrush(
      D2D1::RadialGradientBrushProperties(center, D2D1::Point2F(0, 0), rx, ry),
      coll, &br);
  coll->Release();
  if (FAILED(hr) || !br) return;
  D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
      D2D1::RectF(r.x, r.y, r.right(), r.bottom()), radius, radius);
  active_rt_->FillRoundedRectangle(rr, br);
  br->Release();
}

void D2dRenderTarget::draw_line(const PointF& a, const PointF& b, const Pen& p) {
  if (!active_rt_) return;
  ID2D1SolidColorBrush* br = brush(p.color);
  if (br)
    active_rt_->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), br,
                         p.width);
}

// F-D11：MUI SVG path 扁平化后的多环填充（偶奇）。每环一个 FILLED+CLOSED
// figure；SetFillMode(ALTERNATE) 对整条几何生效（MUI donut 型图标的镂空语义）。
void D2dRenderTarget::fill_path(const std::vector<std::vector<PointF>>& loops,
                                const Brush& b) {
  if (!active_rt_ || loops.empty()) return;
  ID2D1PathGeometry* path = nullptr;
  if (FAILED(ctx_.d2d()->CreatePathGeometry(&path)) || !path) return;
  ID2D1GeometrySink* sink = nullptr;
  if (FAILED(path->Open(&sink)) || !sink) {
    path->Release();
    return;
  }
  sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
  for (const auto& loop : loops) {
    if (loop.size() < 2) continue;
    sink->BeginFigure(D2D1::Point2F(loop[0].x, loop[0].y),
                      D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < loop.size(); ++i)
      sink->AddLine(D2D1::Point2F(loop[i].x, loop[i].y));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
  }
  sink->Close();
  sink->Release();
  ID2D1SolidColorBrush* br = brush(b.color);
  if (br) active_rt_->FillGeometry(path, br);
  path->Release();
}

void D2dRenderTarget::draw_text(const std::wstring& text, const RectF& r,
                                const TextStyle& ts) {
  if (!active_rt_ || text.empty()) return;
  IDWriteTextFormat* f = format(ts);
  if (!f) return;
  IDWriteTextLayout* layout = nullptr;
  HRESULT hr = ctx_.dwrite()->CreateTextLayout(
      text.c_str(), static_cast<UINT32>(text.size()), f,
      r.w, r.h, &layout);
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
      if (br)
        active_rt_->DrawTextLayout(D2D1::Point2F(rr.x, rr.y), layout, br,
                                   D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
      layout->Release();
      return;
    }
  }
  ID2D1SolidColorBrush* br = brush(ts.color);
  if (br)
    active_rt_->DrawTextLayout(D2D1::Point2F(r.x, r.y), layout, br,
                               D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
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

bool D2dRenderTarget::has_bitmap(uint64_t id) const {
  return bitmaps_.find(id) != bitmaps_.end();
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

// ---- 不透明度层（← MUI Fade：Dialog entering 225ms / leaving 195ms） ----
void D2dRenderTarget::push_opacity(float alpha) {
  if (!active_rt_) return;
  if (alpha >= 0.999f) {  // 满不透明 → 免合成，但仍占栈位保证配对
    opacity_pushed_.push_back(false);
    return;
  }
  if (alpha < 0.0f) alpha = 0.0f;
  if (!opacity_layer_ &&
      FAILED(active_rt_->CreateLayer(nullptr, &opacity_layer_))) {
    opacity_layer_ = nullptr;
    opacity_pushed_.push_back(false);  // 退化：不合成，也不破坏配对
    return;
  }
  D2D1_LAYER_PARAMETERS p = D2D1::LayerParameters();
  p.opacity = alpha;
  active_rt_->PushLayer(&p, opacity_layer_);
  opacity_pushed_.push_back(true);
}

void D2dRenderTarget::pop_opacity() {
  if (!active_rt_ || opacity_pushed_.empty()) return;
  const bool pushed = opacity_pushed_.back();
  opacity_pushed_.pop_back();
  if (pushed) active_rt_->PopLayer();
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
    // layer 与 target 同生命周期：设备丢失后必须一起丢弃，否则重建目标后
    // 继续用旧 layer 会返回 D2DERR_WRONG_STATE。
    if (opacity_layer_) { opacity_layer_->Release(); opacity_layer_ = nullptr; }
    opacity_pushed_.clear();
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
