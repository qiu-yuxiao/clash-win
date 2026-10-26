// 标题栏实现（T11）。
#include "title_bar.h"

#include <algorithm>

#include "main_window.h"
#include "theme/theme.h"

namespace clashwin::ui::win {

TitleBar::TitleBar(MainWindow* owner) {
  spacing = 8;  // ← gap 8px
  set_padding(10, 0, 8, 0);  // ← padding 0 8px 0 10px
  flex_weight = 0;

  // 左：AppIcon 16×16（位图由 T12 icon_store 登记 draw_bitmap；骨架期占位框）
  auto icon = std::make_unique<Widget>();
  icon->set_z_order(0);
  add_child(std::move(icon));

  // 标题文本 12px / Medium / 单行省略
  auto label = std::make_unique<Label>();
  title_label_ = label.get();
  title_label_->style.size = 12;
  title_label_->style.weight = static_cast<int>(Weight::Medium);
  title_label_->style.ellipsis = true;
  title_label_->set_text(L"Clash Mini Ver.");
  add_child(std::move(label));

  // 弹性撑开 → 右侧三键贴右（marginLeft auto）
  auto spring = std::make_unique<Widget>();
  spring->flex_weight = 1;
  add_child(std::move(spring));

  // 四键 28×28 gap 2（← WindowControls；glyph ← icon_w 映射）
  auto mkbtn = [&](wchar_t g, bool close_red) {
    auto b = std::make_unique<TitleBarButton>(g);
    b->close_red_ = close_red;
    return b;
  };
  auto bset = mkbtn(L'\uE713', false);   // Setting（T17 抽屉入口）
  bset->on_click = [this] { if (on_settings) on_settings(); };
  btn_settings_ = bset.get();
  add_child(std::move(bset));
  auto bmin = mkbtn(L'\uE921', false);   // ChromeMinimize
  bmin->on_click = [owner] { if (owner) owner->minimize(); };
  btn_min_ = bmin.get();
  add_child(std::move(bmin));
  auto blg = mkbtn(L'\uE922', false);    // ChromeMaximize（large 态换 E923）
  blg->on_click = [owner] { if (owner) owner->toggle_large(); };
  btn_large_ = blg.get();
  add_child(std::move(blg));
  auto bcl = mkbtn(L'\uE711', true);     // Cancel；hover 红
  bcl->on_click = [owner] { if (owner) owner->close_to_lightweight(); };
  btn_close_ = bcl.get();
  add_child(std::move(bcl));
}

TitleBar::~TitleBar() = default;

void TitleBar::set_large_mode(bool large) {
  btn_large_->glyph_ = large ? L'\uE923' : L'\uE922';  // FilterNone / CropSquare
  btn_large_->invalidate();
}

void TitleBar::set_version(const std::wstring& v) {
  title_label_->set_text(L"Clash Mini Ver." + v);
}

void TitleBar::on_paint(IRenderTarget& rt) {
  // ← customTitlebar 样式：background var(--background-color)（light #ECECEC）
  //   + borderBottom var(--divider-color)；close hover 红为原版硬编码保留
  const auto& svc = theme::ThemeService::instance();
  rt.fill_rect(bounds(), Brush{svc.vars().background_color});
  rt.fill_rect({bounds().x, bounds().bottom() - 1, bounds().w, 1},
               Brush{svc.vars().divider_color});
}

}  // namespace clashwin::ui::win
