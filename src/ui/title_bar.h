#pragma once
// 标题栏（T11）：30px —— AppIcon 16 + "Clash Win Ver.{v}" + 右三键 28×28。
// ← _layout.tsx:1119-1183（customTitlebar）+ window-controller.tsx（三键）。
// - 三键：最小化 / 大尺寸□（large 态换 restore 字形）/ 关闭（hover 红底白字）
// - 关闭 = 隐藏窗口 + entry_lightweight_mode（← 02§7，非退出）
// - 大尺寸键 = toggle 640×860（禁 OS maximize），恢复 last-non-large-size
// - 拖动区：MainWindow WM_NCHITTEST 返回 HTCAPTION（本 widget 不收鼠标）
// - stealth 隐藏时整栏不挂载（layout-content 高度 = 全客户区）
#include <functional>
#include <string>

#include "container.h"
#include "icon_w.h"
#include "label.h"
#include "theme/theme.h"
#include "widget.h"

namespace clashwin::ui::win {

class MainWindow;

// 标题栏底色（src: layout.scss:166 / :356）。暴露供 t35 回归断言
// 「alpha 跟随 --depth-factor」，避免滑块被写死常量架空。
Color4f title_bar_bg(const theme::ThemeService& svc);

class TitleBarButton : public Widget {
 public:
  TitleBarButton(wchar_t glyph, Widget* parent = nullptr)
      : Widget(parent), glyph_(glyph) {}

  std::function<void()> on_click;
  bool close_red_ = false;   // 关闭键 hover 红
  bool pressed_ = false;
  bool hover_ = false;
  wchar_t glyph_;             // 兼容保留（F-D11 后绘制走 icon_name_）
  std::string icon_name_;     // F-D11：MUI path 名（minimize/maximize/restore/winclose）

  SizeF on_measure(float max_w, float max_h) override {
    (void)max_w; (void)max_h;
    return {28, 28};  // ← window-controller btnSx 28×28
  }

  A11yRole a11y_default_role() const override { return A11yRole::Button; }
  bool a11y_is_focusable() const override { return enabled(); }
  bool a11y_can_invoke() const override {
    return enabled() && static_cast<bool>(on_click);
  }
  bool a11y_invoke() override {
    if (!enabled() || !on_click) return false;
    on_click();
    return true;
  }

  bool on_mouse(const MouseEvent& e) override {
    switch (e.type) {
      case MouseEventType::Enter: hover_ = true; invalidate(); return true;
      case MouseEventType::Leave: hover_ = false; pressed_ = false; invalidate(); return true;
      case MouseEventType::Down: pressed_ = true; invalidate(); return true;
      case MouseEventType::Up:
        // 【CLICKFIX】e.pos 为局部坐标，须对 {0,0,w,h} 判定（原 bounds() 是父坐标，
        // 非零 x/y 的按钮 Up 恒不命中 → on_click 从不触发）
        if (pressed_ && RectF{0, 0, bounds().w, bounds().h}.contains(e.pos.x, e.pos.y) &&
            on_click)
          on_click();
        pressed_ = false;
        invalidate();
        return true;
      default: return false;
    }
  }

  void on_paint(IRenderTarget& rt) override {
    // 28×28 圆角 4（← window-controller btnSx）；hover：普通键灰 4%、关闭键红
    // 【PAINTFIX】on_paint 约定局部坐标 {0,0,w,h}（paint 已平移原点）；此前直接
    // 用 bounds()（父局部 x/y）→ 绘制原点二次叠加，四键全部画到窗外。
    RectF r{0, 0, bounds().w, bounds().h};
    const auto& svc = theme::ThemeService::instance();
    if (hover_) {
      // ← window-controller.tsx:31-49 IconButton 未指定 color ⇒ MUI action.hover
      //   （light rgba(0,0,0,.04) / dark rgba(255,255,255,.08)）；关闭键 :hover 红
      Brush bg = close_red_ ? Brush{Color4f::hex(0xFF0000)}
                            : Brush{svc.is_light() ? Color4f(0, 0, 0, 0.04f)
                                                   : Color4f(1, 1, 1, 0.08f)};
      rt.round_rect(r, 4, {}, &bg);
    }
    // 图标 16px 居中（src: window-controller.tsx:32「width: 16, height: 16」）；
    //   hover 且关闭键 → 白色。F-D11：按 Mini 的 MUI SVG path 填充绘制
    //   （Minimize/CropSquare/FilterNone/Close 真实 path，24×24→16px 整盒映射）。
    // ← MUI IconButton 默认 color=default ⇒ palette.action.active
    //   （light rgba(0,0,0,.54) / dark rgba(255,255,255,.54)）
    Color4f ink = close_red_ && hover_
                      ? Color4f::hex(0xFFFFFF)
                      : (svc.is_light() ? Color4f(0, 0, 0, 0.54f)
                                        : Color4f(1, 1, 1, 0.54f));
    std::vector<MuiLoopSet> mui;
    if (!icon_name_.empty() && IconW::mui_paths_for(icon_name_, mui)) {
      const float s = 16.0f;  // src: window-controller.tsx:32 width 16
      const float scale = s / 24.0f;
      const float ox = (bounds().w - s) * 0.5f, oy = (bounds().h - s) * 0.5f;
      for (const auto& ls : mui) {
        Color4f c = ink;
        c.a *= ls.opacity;
        // 同一 <path> 元素的全部子路径必须一次性 fill_path（偶奇填充靠
        //   多环同场运算产生镂空，如 CropSquare 外框减内框）
        std::vector<std::vector<PointF>> loops;
        loops.reserve(ls.loops.size());
        for (const auto& poly : ls.loops) {
          std::vector<PointF> pts;
          pts.reserve(poly.size());
          for (const auto& p : poly)
            pts.push_back({ox + p.x * scale, oy + p.y * scale});
          loops.push_back(std::move(pts));
        }
        rt.fill_path(loops, Brush{c});
      }
      return;
    }
    // 兜底：字体字形（icon_name_ 未设时保持旧行为）
    TextStyle ts;
    ts.font_family = L"Segoe Fluent Icons";
    ts.size = 16;  // src: window-controller.tsx:32 width 16
    ts.color = ink;
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt.draw_text(std::wstring(1, glyph_), r, ts);
  }
};

// AppIcon 16×16（src: _layout.tsx:1143-1154；位图资源 resources/icons/32x32.png）
class TitleBarIcon : public Widget {
 public:
  explicit TitleBarIcon(Widget* parent = nullptr) : Widget(parent) {}

  SizeF on_measure(float max_w, float max_h) override {
    (void)max_w; (void)max_h;
    return {16, 16};  // src: _layout.tsx:1149-1150 width/height 16px
  }

  // pointerEvents: 'none'（src: _layout.tsx:1152）
  bool hit_self(PointF local) const override {
    (void)local;
    return false;
  }

  void on_paint(IRenderTarget& rt) override;
};

class TitleBar : public Row {
 public:
  explicit TitleBar(MainWindow* owner);
  ~TitleBar() override;

  // 标题栏固定 30px（← customTitlebar 高度；Row::on_measure 只算出 28 键高）
  SizeF on_measure(float max_w, float max_h) override {
    SizeF s = Row::on_measure(max_w, max_h);
    s.h = 30;
    return s;
  }

  void set_large_mode(bool large);  // 切换 □/restore 字形
  void set_version(const std::wstring& v);

  std::wstring a11y_default_name() const override {
    return title_label_ ? title_label_->text : L"Clash Win";
  }

  void on_paint(IRenderTarget& rt) override;  // 背景 + divider 底边

  // 按钮（测试/宿主触发交互用）
  TitleBarButton* btn_minimize() const { return btn_min_; }
  TitleBarButton* btn_large() const { return btn_large_; }
  TitleBarButton* btn_close() const { return btn_close_; }
  TitleBarIcon* icon() const { return icon_; }

 private:
  Label* title_label_ = nullptr;
  TitleBarIcon* icon_ = nullptr;
  TitleBarButton* btn_min_ = nullptr;
  TitleBarButton* btn_large_ = nullptr;
  TitleBarButton* btn_close_ = nullptr;
};

}  // namespace clashwin::ui::win
