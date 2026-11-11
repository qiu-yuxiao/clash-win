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
// 消息块（.MuiAlert-message）minWidth:0 / overflow:auto → 长文案**自动折行**，
//   条目高度随行数增长（不截断、不省略）。
// 右键（onContextMenu :196-200）：复制该条全文 + copySuccess 提示 1000ms
//   （← resolveNoticeCopyText :100-129 + handleNoticeCopy :178-193）。
// 通知位置取 verge.notice_position（缺省 "top-right"，config/verge.rs:353）。
#include <string>
#include <vector>

#include "events/notice.h"
#include "widget.h"

namespace clashwin::ui {

class SnackbarW : public Widget {
 public:
  explicit SnackbarW(Widget* parent = nullptr) : Widget(parent) {
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
  static constexpr float kLineH = 20.02f;  // fontSize 14 × lineHeight 1.43
  static constexpr float kMsgPadY = 8;     // .MuiAlert-message padding 8px 0
  static constexpr float kPadY = 6;        // Alert root padding 6px 16px

  SizeF on_measure(float max_w, float max_h) override;
  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;

  // 【命中】通知容器不拦截点击。
  //   ← notice-manager.tsx:157-176：容器 position:fixed；MUI Snackbar 容器默认
  //     pointer-events:none（只有 Snackbar 本体可交互）→ 容器本体不吃鼠标。
  //   本实现的通知条目是自绘的（无子 widget），所以只有「点落在某条通知条目上」
  //   才算命中（条目支持右键复制全文）；其余区域一律放行给下层控件。
  //   ★ 不加这条守卫时，本容器铺满全窗且 visible=true，会在 hit_test 里截断
  //     整棵命中链 → 全应用鼠标点击失效（齿轮/开关/滑块全部点不动）。
  bool hit_self(PointF local) const override {
    for (size_t i = 0; i < item_rects_.size() && i < items_.size(); ++i)
      if (item_rects_[i].contains(local.x, local.y)) return true;
    return false;
  }

  // 测试断言：第 i 条在自身坐标系的矩形
  const std::vector<RectF>& item_rects() const { return item_rects_; }
  void layout_items();  // 依 bounds + position_ 计算 item_rects_

  // 条目高度（按消息在 item_w 内折行的行数算；单行 = kItemH）
  static float item_height(const std::string& message, float item_w);

 private:
  void refresh();
  // 消息可用宽（Alert root padding 16 + 图标 22+12 + action 30+16-8）
  static float message_width(float item_w);
  // 右键复制该条全文 + copySuccess 提示（← :178-193）
  static void copy_notice(const std::string& text);
  // 关闭按钮命中盒（绝对条目矩形，自身坐标系）
  static RectF close_rect(const RectF& item);

  std::vector<events::NoticeItem> items_;
  std::vector<RectF> item_rects_;
  std::string position_ = "top-right";
  events::NoticeService* svc_ = nullptr;
  uint64_t sub_id_ = 0;
};

}  // namespace clashwin::ui
