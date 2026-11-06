#pragma once
// SnackbarW（T10）：通知条——对接 NoticeService（push→显示→到期消失）。
// 对照 notice-manager：堆叠通知（最新在下），类型色条 success/info/error；
// 到期由 NoticeService sweep 驱动（回调 → snapshot 刷新）。
// 布局：宿主给 bounds（通常底部整宽），条目自底向上堆叠。
#include <vector>

#include "events/notice.h"
#include "widget.h"

namespace clashwin::ui {

class SnackbarW : public Widget {
 public:
  explicit SnackbarW(Widget* parent = nullptr) : Widget(parent) {
    set_z_order(1100);  // 高于 modal
  }
  ~SnackbarW() override;

  // 绑定 NoticeService（订阅变化 → 刷新）
  void attach(events::NoticeService& svc);
  // 独立注入（测试用）
  void set_items(std::vector<events::NoticeItem> items);

  size_t visible_count() const { return items_.size(); }
  const std::vector<events::NoticeItem>& items() const { return items_; }

  // 单条高度 + 堆叠
  float item_height = 36;
  float gap = 6;

  SizeF on_measure(float max_w, float max_h) override;
  void on_paint(IRenderTarget& rt) override;

  // 绘制参数
  Color4f bg_ = Color4f::hex(0x2E303D, 0.95f);  // ← 与标题栏深色一致（应用自定 Snackbar）
  Color4f text_ = Color4f::hex(0xFFFFFF);  // ← Snackbar 文字白
  Color4f success_ = Color4f::hex(0x34C759);  // ← 成功语义色（iOS green）
  Color4f info_ = Color4f::hex(0x007AFF);  // ← 信息语义色（iOS blue）
  Color4f error_ = Color4f::hex(0xFF3B30);  // src: _theme.tsx:8 error_color

 private:
  void refresh();
  std::vector<events::NoticeItem> items_;
  bool attached_ = false;
};

}  // namespace clashwin::ui
