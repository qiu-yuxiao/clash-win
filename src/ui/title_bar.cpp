// 标题栏实现（T11）。
#include "title_bar.h"

#include <algorithm>

#include <filesystem>

#include "d2d_context.h"
#include "main_window.h"
#include "theme/theme.h"
#include "util/dirs.h"

#include <wincodec.h>
#include <wrl/client.h>
#ifdef interface
#undef interface  // COM 宏与 models.h 的 std::string interface 冲突
#endif

namespace clashwin::ui::win {

namespace {
// AppIcon 位图 id（src: _layout.tsx:30 import AppIcon from '@/assets/image/app-icon.png'）
constexpr uint64_t kAppIconBitmapId = 0x41707049636F6E31ULL;  // "AppIcon1"

// WIC 解码 resources/icons/32x32.png 并登记到 D2D（设备重建后按帧惰性重登）
bool register_app_icon(D2dRenderTarget& rt) {
  namespace fs = std::filesystem;
  using Microsoft::WRL::ComPtr;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ComPtr<IWICImagingFactory> wic;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
  if (FAILED(hr) || !wic) {
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                          CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
  }
  if (FAILED(hr) || !wic) return false;
  const fs::path png =
      fs::path(util::dirs::app_resources_dir()) / L"icons" / L"32x32.png";
  std::error_code ec;
  if (!fs::exists(png, ec)) return false;
  ComPtr<IWICBitmapDecoder> decoder;
  hr = wic->CreateDecoderFromFilename(png.c_str(), nullptr, GENERIC_READ,
                                      WICDecodeMetadataCacheOnLoad, &decoder);
  if (FAILED(hr)) return false;
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(decoder->GetFrame(0, &frame))) return false;
  ComPtr<IWICBitmapSource> converted;
  hr = WICConvertBitmapSource(GUID_WICPixelFormat32bppPBGRA, frame.Get(),
                              &converted);
  if (FAILED(hr)) return false;
  rt.register_bitmap(kAppIconBitmapId, converted.Get());
  return rt.has_bitmap(kAppIconBitmapId);
}

}  // namespace（匿名）

// 标题栏底色：layout.scss:166 .the_titlebar !important 覆写 inline
// var(--background-color)；皮肤分支 :323-460（retro-3d 无专属覆写，走 :166 基线）
// 暴露为命名空间级函数：便于 t35 回归锁死「alpha 跟随 depth 滑块」。
Color4f title_bar_bg(const theme::ThemeService& svc) {
  using theme::SkinId;
  const bool light = svc.is_light();
  switch (svc.skin()) {
    case SkinId::Original:  // src: layout.scss:452 var(--theme-bg)=:421-422
      return light ? Color4f::hex(0xf5f5f5) : Color4f::hex(0x121212);
    case SkinId::ModernFlat:  // src: layout.scss:338 var(--theme-bg)=:458-459
      return light ? Color4f::hex(0xf0f5ff) : Color4f::hex(0x0f1423);
    case SkinId::FrostedGlass: {
      // src: layout.scss:356 calc(0.02 + 0.12 * var(--depth-factor, 1.0))
      //   → 必须跟随 depth 滑块；写死 0.14 会让 Opacity/Blur 页首屏标题栏失效
      const float a = 0.02f + 0.12f * svc.depth();
      return light ? Color4f::hex(0xF0F5FF, a)
                   : Color4f::hex(0x0F1423, a);  // src: use-custom-theme.ts:466-468
    }
    case SkinId::Cyberpunk:  // src: layout.scss:389/:409
      return light ? Color4f::hex(0xe2e8f0) : Color4f::hex(0x0d1117);
    case SkinId::Monochrome:  // src: layout.scss:427 var(--background-color)
      return light ? Color4f::hex(0xECECEC) : Color4f::hex(0x2E303D);
    case SkinId::Retro3d:
    default: {
      // src: layout.scss:166 calc(1 - 0.2 * var(--depth-factor, 1.0))
      //   retro-3d 缺省 depth=0.3 → 应为 0.94；写死 0.8 既错又让 Depth 滑块失效
      const float a = 1.0f - 0.2f * svc.depth();
      // base-rgb light 240,245,255 / dark 15,20,35 ← use-custom-theme.ts:466-468
      return light ? Color4f::hex(0xF0F5FF, a) : Color4f::hex(0x0F1423, a);
    }
  }
}

void TitleBarIcon::on_paint(IRenderTarget& rt) {
  if (auto* d2d = dynamic_cast<D2dRenderTarget*>(&rt)) {
    if (!d2d->has_bitmap(kAppIconBitmapId)) register_app_icon(*d2d);
  }
  // 全不透明绘制（src: _layout.tsx:1143-1154 img 无 opacity 样式）
  rt.draw_bitmap(kAppIconBitmapId, RectF{0, 0, bounds().w, bounds().h}, 1.0f);
}

TitleBar::TitleBar(MainWindow* owner) {
  spacing = 8;  // ← gap 8px
  set_padding(10, 0, 8, 0);  // ← padding 0 8px 0 10px
  flex_weight = 0;
  cross_align = CrossAlign::Center;  // 28×28 键在 30px 栏内垂直居中（y=1）

  // 左：AppIcon 16×16（src: _layout.tsx:1143-1154）
  auto icon = std::make_unique<TitleBarIcon>();
  icon_ = icon.get();
  add_child(std::move(icon));

  // 标题文本 12px / Medium / 单行省略（← CSS flex:1 + overflow:hidden +
  //   text-overflow:ellipsis：最大宽=剩余空间，绝不覆盖右侧按钮）
  auto label = std::make_unique<Label>();
  title_label_ = label.get();
  title_label_->style.size = 12;
  title_label_->style.weight = static_cast<int>(Weight::Medium);
  title_label_->style.align_v = AlignV::Center;  // src: _layout.tsx:1135 alignItems 'center'
  title_label_->style.ellipsis = true;
  // src: _layout.tsx:1155-1169 标题 span flex 不伸缩、宽度随内容
  title_label_->flex_weight = 0;
  title_label_->shrink_to_free = false;
  title_label_->set_text(L"Clash Win Ver.");
  add_child(std::move(label));

  // 三键 28×28 gap 2（← WindowControls；flex-shrink:0 固定尺寸）。
  // 内层 Row 承担 gap 2，外层 spacing 8 只作用于 icon/label/按钮组之间。
  // 【右对齐】Mini 的按钮组容器带 marginLeft:'auto'（_layout.tsx:1178），
  //   把剩余横向空间全部吃掉 → 三键永远贴右边缘，而不是紧跟在标题文字后面。
  //   对应物 = 可压缩的空占位（flex 1 + 最小宽 0），语义与 marginLeft:auto 一致。
  auto spacer = std::make_unique<Widget>();
  spacer->flex_weight = 1;
  spacer->shrink_to_free = true;
  add_child(std::move(spacer));

  auto ctrls = std::make_unique<Row>();
  ctrls->spacing = 2;
  ctrls->flex_weight = 0;
  // F-D11：绘制按 MUI 真实 SVG path（window-controller.tsx 的 Minimize/
  //   CropSquare/FilterNone/Close）；glyph 仅为兜底兼容保留。
  auto mkbtn = [&](wchar_t g, const char* name, bool close_red) {
    auto b = std::make_unique<TitleBarButton>(g);
    b->icon_name_ = name;
    b->close_red_ = close_red;
    return b;
  };
  // Mini 标题栏只有三键：齿轮在 quickbar 内（_layout.tsx:1339-1349），
  // 这里不再重复挂 Setting，避免与 quickbar 齿轮双入口（P1）。
  auto bmin = mkbtn(L'\uE921', "minimize", false);  // Minimize
  bmin->on_click = [owner] { if (owner) owner->minimize(); };
  btn_min_ = bmin.get();
  ctrls->add_child(std::move(bmin));
  auto blg = mkbtn(L'\uE922', "cropsquare", false);  // CropSquare（large 态换 FilterNone）
  blg->on_click = [owner] { if (owner) owner->toggle_large(); };
  btn_large_ = blg.get();
  ctrls->add_child(std::move(blg));
  auto bcl = mkbtn(L'\uE711', "winclose", true);  // Close；hover 红
  bcl->on_click = [owner] { if (owner) owner->close_to_lightweight(); };
  btn_close_ = bcl.get();
  ctrls->add_child(std::move(bcl));
  add_child(std::move(ctrls));
}

TitleBar::~TitleBar() = default;

void TitleBar::set_large_mode(bool large) {
  // src: window-controller.tsx:34-39 isLargeMode ? FilterNone : CropSquare
  btn_large_->icon_name_ = large ? "filternone" : "cropsquare";
  btn_large_->glyph_ = large ? L'\uE923' : L'\uE922';  // 兜底字形同步
  btn_large_->invalidate();
}

void TitleBar::set_version(const std::wstring& v) {
  title_label_->set_text(L"Clash Win Ver." + v);
}

void TitleBar::on_paint(IRenderTarget& rt) {
  // src: layout.scss:166 .the_titlebar !important 覆写 inline var(--background-color)
  // （:1128-1140 inline 只是 fallback）；按皮肤六分支，见 title_bar_bg()
  const auto& svc = theme::ThemeService::instance();
  // src: _layout.tsx:1155-1169 标题 span 用 var(--text-primary-color)
  //   （= _theme.tsx:19-20 primary_text '#000000'/'#FFFFFF'）；每帧跟随主题刷新。
  if (title_label_) title_label_->style.color = svc.text_primary();
  rt.fill_rect(bounds(), Brush{title_bar_bg(svc)});
  // src: layout.scss:165 border-bottom 1px var(--theme-border)
  rt.fill_rect({bounds().x, bounds().bottom() - 1, bounds().w, 1},
               Brush{svc.vars().theme_border});
  // src: layout.scss:168 box-shadow 0 1px 4px var(--theme-border-outer)
  //   TitleBar 在 flow_ 内 z_order=10 高于 content_(0)，而 flow_(0) 低于
  //   root_ 顶层浮层（弹窗/TooltipHost/SnackbarW），故在此绘制阴影既能压在
  //   内容区之上，又绝不横穿顶部通知条或弹窗，且随 stealth 隐藏自动消隐。
  const Color4f outer = svc.vars().theme_border_outer;
  for (int i = 1; i <= 4; ++i) {
    Color4f c = outer;
    c.a = outer.a * (1.0f - static_cast<float>(i - 1) / 4.0f) / 2.0f;
    rt.fill_rect({0.0f, bounds().h + static_cast<float>(i - 1), bounds().w, 1.0f},
                 Brush{c});
  }
}

}  // namespace clashwin::ui::win
