#pragma once
// SnackbarW：通知条 —— ← src/components/layout/notice-manager.tsx:157-216
//   + MUI Alert variant="filled"。
// 容器（notice-manager.tsx:157-176）：position fixed；vertical=top → top 20、
//   bottom → bottom 20；horizontal=left → left 20、right → right 20；
//   display flex / column / gap 10px / maxWidth 360px / zIndex 1500。
// 条目（:178-212）：<Snackbar open anchorOrigin><Alert severity variant="filled"
//   width:100% action={<IconButton size="small" color="inherit">
//   <CloseRounded fontSize="inherit"/></IconButton>}>
//   MUI Alert filled 度量：padding 6px 16px / radius 4px / icon 22px + marginRight 12
//   / message padding 8px 0 / fontSize 14 lineHeight 1.43 / action padding 4px 0 0 16px
//   + marginRight -8px。
// 通知位置取 verge.notice_position（缺省 "top-right"，config/verge.rs:353）。
#include <string>
#include <vector>

#include "events/notice.h"
#include "widget.h"

namespace clashwin::ui {

class SnackbarW : public Widget {
 public:
  explicit SnackbarW(Widget* parent = nullptr) {
    set_z_order(1500);  // ← notice-manager.tsx:165 zIndex 1500
  }
  ~SnackbarW() override;

  // 绑定 NoticeService（订阅变化 → 刷新）
  void attach(events::NoticeService& svc);
  // 独立注入（测试用）
  void set_items(std::vector<events::NoticeItem> items);

  size_t visible_count() const { return items_.size(); }
  const std::vector<events::NoticeItem>& items() const { return items_; }

  // "top-left" / "top-right" / "bottom-left" / "bottom-right"（非法值回落 top-right）
  void set_position(const std::string& pos);
  const std::string& position() const { return position_; }

  // 布局几何（逻辑像素）
  static constexpr float kInset = 20;      // :160-163 20px
  static constexpr float kGapPx = 10;      // :172 gap 10px
  static constexpr float kMaxWidth = 360;  // :173 maxWidth 360px
  static constexpr float kItemH = 48;      // 6 + 36 + 6（MUI Alert filled 单行）
  static constexpr float kRadius = 4;      // MUI Alert borderRadius
  static constexpr float kIconSize = 22;   // .MuiAlert-icon fontSize 22

  SizeF on_measure(float max_w, float max_h) override;
  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;

  // 测试断言：第 i 条在自身坐标系的矩形
  const std::vector<RectF>& item_rects() const { return item_rects_; }
  void layout_items();  // 依 bounds + position_ 计算 item_rects_

 private:
  void refresh();
  // 关闭按钮命中盒（绝对条目矩形，自身坐标系）
  static RectF close_rect(const RectF& item);

  std::vector<events::NoticeItem> items_;
  std::vector<RectF> item_rects_;
  std::string position_ = "top-right";
  bool attached_ = false;
};

}  // namespace clashwin::ui
