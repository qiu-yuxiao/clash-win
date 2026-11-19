#pragma once
// SnackbarW：通知条 —— src: components/layout/notice-manager.tsx:108-231
// 固定左上锚定：width=min(271, winW)、px=6、top=topOffset+4、gap=4、zIndex=2200
// Alert filled：cyberpunk/monochrome 0px，其余 6px；px 10 / py 4 / 12px / lineHeight 1.45
#include <string>
#include <vector>

#include "events/notice.h"
#include "widget.h"

namespace clashwin::ui {

class SnackbarW : public Widget {
 public:
  explicit SnackbarW(Widget* parent = nullptr) : Widget(parent) {
    // src: components/layout/notice-manager.tsx:158 zIndex 2200
    set_z_order(2200);
  }
  ~SnackbarW() override;

  void attach(events::NoticeService& svc);
  void set_items(std::vector<events::NoticeItem> items);

  size_t visible_count() const { return items_.size(); }
  const std::vector<events::NoticeItem>& items() const { return items_; }

  // 兼容持久化字段接口（notice-manager.tsx:109 已统一固定左上角 271px 窄列锚定）
  void set_position(const std::string& pos);
  const std::string& position() const { return position_; }

  // 自定义标题栏显示时让出 30px 标题栏，隐藏时贴顶 0px（src: utils/popup-styles.ts:35-41）
  void set_top_offset(float top_offset);
  float top_offset() const { return top_offset_; }

  // src: components/layout/notice-manager.tsx:116,152-208
  static constexpr float kOuterPadX = 6.0f;    // px: '6px'
  static constexpr float kOuterPadTop = 4.0f;  // pt: '4px'
  static constexpr float kGapPx = 4.0f;        // gap: '4px'
  static constexpr float kMaxWidth = 271.0f;   // MIN_WINDOW_WIDTH
  static constexpr float kItemH = 32.0f;       // py 4*2 + icon/msg 24
  static constexpr float kRadius = 6.0f;       // borderRadius 6px (0 for cyber/mono)
  static constexpr float kIconSize = 18.0f;    // fontSize: '18px'
  static constexpr float kFontSize = 12.0f;    // fontSize: '12px'
  static constexpr float kLineH = 17.4f;       // 12px * 1.45
  static constexpr float kMsgPadY = 2.0f;      // message py: '2px'
  static constexpr float kPadX = 10.0f;        // Alert px: 1.25
  static constexpr float kPadY = 4.0f;         // Alert py: 0.5
  static constexpr float kTitleBarOffset = 30.0f;  // CUSTOM_TITLEBAR_HEIGHT

  SizeF on_measure(float max_w, float max_h) override;
  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;

  bool hit_self(PointF local) const override {
    for (size_t i = 0; i < item_rects_.size() && i < items_.size(); ++i)
      if (item_rects_[i].contains(local.x, local.y)) return true;
    return false;
  }

  bool a11y_subtree_visible() const override {
    return visible() && !a11y_hidden() && !items_.empty();
  }
  A11yRole a11y_default_role() const override { return A11yRole::StatusBar; }
  std::wstring a11y_default_name() const override;
  size_t a11y_virtual_child_count() const override {
    return std::min(items_.size(), item_rects_.size());
  }
  A11ySubItem a11y_virtual_child(size_t index) const override;
  bool a11y_invoke_virtual_child(size_t index) override;

  const std::vector<RectF>& item_rects() const { return item_rects_; }
  void layout_items();

  static float item_height(const std::string& message, float item_w);

 private:
  void refresh();
  static float message_width(float item_w);
  static void copy_notice(const std::string& text);
  static RectF close_rect(const RectF& item);

  std::vector<events::NoticeItem> items_;
  std::vector<RectF> item_rects_;
  std::string position_ = "top-left";
  float top_offset_ = kTitleBarOffset;
  events::NoticeService* svc_ = nullptr;
  uint64_t sub_id_ = 0;
  uint64_t last_announced_id_ = 0;
};

}  // namespace clashwin::ui
