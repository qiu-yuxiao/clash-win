#pragma once
// 标题栏（T11）：30px —— AppIcon 16 + "Clash Mini Ver.{v}" + 右三键 28×28。
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
#include "widget.h"

namespace clashwin::ui::win {

class MainWindow;

class TitleBarButton : public Widget {
 public:
  TitleBarButton(wchar_t glyph, Widget* parent = nullptr)
      : Widget(parent), glyph_(glyph) {}

  std::function<void()> on_click;
  bool close_red_ = false;   // 关闭键 hover 红
  bool pressed_ = false;
  bool hover_ = false;
  wchar_t glyph_;

  SizeF on_measure(float max_w, float max_h) override {
    (void)max_w; (void)max_h;
    return {28, 28};  // ← window-controller btnSx 28×28
  }

  bool on_mouse(const MouseEvent& e) override {
    switch (e.type) {
      case MouseEventType::Enter: hover_ = true; invalidate(); return true;
      case MouseEventType::Leave: hover_ = false; pressed_ = false; invalidate(); return true;
      case MouseEventType::Down: pressed_ = true; invalidate(); return true;
      case MouseEventType::Up:
        if (pressed_ && bounds().contains(e.pos.x, e.pos.y) && on_click) on_click();
        pressed_ = false;
        invalidate();
        return true;
      default: return false;
    }
  }

  void on_paint(IRenderTarget& rt) override {
    // 28×28 圆角 4（← window-controller btnSx）；hover：普通键灰 4%、关闭键红
    RectF r = bounds();
    if (hover_) {
      Brush bg = close_red_ ? Brush{Color4f::hex(0xFF0000)}
                            : Brush{Color4f::hex(0x000000, 0.06f)};
      rt.round_rect(r, 4, {}, &bg);
    }
    // 图标 16px 居中（← sx width 16）；hover 且关闭键 → 白色
    TextStyle ts;
    ts.font_family = L"Segoe Fluent Icons";
    ts.size = 10;  // MDL2 线性图标在此字号下视觉 ≈ 16px 框
    ts.color = close_red_ && hover_ ? Color4f::hex(0xFFFFFF)
                                    : Color4f::hex(0x000000, 0.75f);
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt.draw_text(std::wstring(1, glyph_), r, ts);
  }
};

class TitleBar : public Row {
 public:
  explicit TitleBar(MainWindow* owner);
  ~TitleBar() override;

  void set_large_mode(bool large);  // 切换 □/restore 字形
  void set_version(const std::wstring& v);

  // 齿轮键（T17：设置抽屉入口；宿主 set on_settings）
  std::function<void()> on_settings;
  TitleBarButton* btn_settings() const { return btn_settings_; }

  void on_paint(IRenderTarget& rt) override;  // 背景 + divider 底边

  // 按钮（测试/宿主触发交互用）
  TitleBarButton* btn_minimize() const { return btn_min_; }
  TitleBarButton* btn_large() const { return btn_large_; }
  TitleBarButton* btn_close() const { return btn_close_; }

 private:
  Label* title_label_ = nullptr;
  TitleBarButton* btn_settings_ = nullptr;
  TitleBarButton* btn_min_ = nullptr;
  TitleBarButton* btn_large_ = nullptr;
  TitleBarButton* btn_close_ = nullptr;
};

}  // namespace clashwin::ui::win
