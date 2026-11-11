#pragma once
// TooltipW（T10）：悬停提示——悬停 100ms 后显示。
// src: node_modules/@mui/material/Tooltip/Tooltip.js:90-96,331（enterDelay 缺省 100，maxWidth 300）
// 实机（绑定 owner_hwnd_ 时）通过独立顶层分层窗口（WS_POPUP | WS_EX_LAYERED）
// 渲染到桌面坐标系并按工作区（MONITORINFO::rcWork）钳制，彻底解除 271px 窄窗对提示条的裁切；
// 无 HWND 的离屏单元测试场景退化为画布内自钳制渲染。
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

#include "widget.h"

namespace clashwin::ui {

class D2dContext;
class D2dRenderTarget;

class TooltipHost : public Widget {
 public:
  explicit TooltipHost(Widget* parent = nullptr);
  ~TooltipHost() override;

  static constexpr int kShowDelayMs = 100;  // src: node_modules/@mui/material/Tooltip/Tooltip.js:331 enterDelay = 100
  static constexpr float kMaxWidth = 300.0f;  // src: node_modules/@mui/material/Tooltip/Tooltip.js:94 maxWidth: 300

  // 绑定宿主主窗口 HWND：非空且可见时启用跨窗口边界的顶层分层提示浮窗
  void bind_owner_hwnd(HWND owner_hwnd);
  HWND popup_hwnd() const { return popup_hwnd_; }
  RECT popup_screen_rect() const { return popup_screen_rect_; }

  // 鼠标悬停在带 tooltip_text() 的 widget 上时由宿主调用（全局坐标）
  void hover(Widget* target, PointF global);
  void leave();                      // 离开/移出目标
  void tick_now(int64_t now_ms);     // 时间推进：到 100ms 显示
  bool showing() const { return visible(); }
  const std::wstring& text() const { return text_; }
  RectF tip_bounds() const { return bounds(); }

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  // 提示层不拦截命中（否则显示期间鼠标事件全被它吃掉）
  bool hit_self(PointF) const override { return false; }

  // 绘制参数（MUI tooltip：深灰底白字）
  Color4f bg_ = Color4f::hex(0x616161, 0.95f);  // src: MUI grey[700] rgba(97,97,97,.95)
  Color4f text_color_ = Color4f::hex(0xFFFFFF);  // src: MUI tooltip 文字白
  float font_size = 11;  // ← MUI tooltip caption 字号
  float pad_h = 8, pad_v = 4;  // ← MUI tooltip 内距 8/4
  float radius_ = 4;

 private:
  struct MeasuredTip {
    float box_w = 0.0f;
    float box_h = 0.0f;
    bool multiline = false;
  };
  MeasuredTip measure_tip(ITextEngine* eng, float max_box_w) const;
  void paint_tip_body(IRenderTarget& rt, float box_w, float box_h, bool multiline,
                      bool with_arrow, bool arrow_at_top, float arrow_x) const;
  void place(PointF anchor);
  bool can_use_popup() const;
  bool ensure_popup_hwnd();
  void present_popup();
  void hide_popup();
  void destroy_popup();

  HWND owner_hwnd_ = nullptr;
  HWND popup_hwnd_ = nullptr;
  bool popup_active_ = false;
  RECT popup_screen_rect_{};
  std::unique_ptr<D2dContext> popup_ctx_;
  std::unique_ptr<D2dRenderTarget> popup_rt_;

  RectF host_area_{};
  Widget* target_widget_ = nullptr;
  RectF target_rect_{};
  bool prefer_top_arrow_ = false;
  bool multiline_ = false;
  std::wstring text_;
  int64_t hover_start_ = -1;
  int64_t now_ = 0;
  PointF anchor_{};
};

}  // namespace clashwin::ui
