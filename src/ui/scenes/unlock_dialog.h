#pragma once
// 流媒体与 AI 解锁检测弹窗（T21）：对齐 clashwin_modal_unlock.png（360×620 px）。
// - 12 大平台列表（ChatGPT/Claude/Gemini/YouTube/Netflix/Disney+/Spotify/TikTok/B站港澳台/动画疯/B站大陆/Prime Video）
// - 状态标记：☑ 已解锁（绿）、✖ 未解锁（红）、⏳ 检测中（蓝）、⚠️ 错误（橙）
// - 地区代码显示（如 usUS, jpJP, sgSG, hkHK, twTW）
// - 底部"🔄 重新检测全部平台"全宽按钮
// - 严格遵守 Direct2D 局部坐标规则 {0, y}
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config/unlock_cache.h"
#include "ui/widget.h"

namespace clashwin::ui::scene {

class UnlockDialog : public Widget {
 public:
  explicit UnlockDialog(Widget* parent = nullptr);
  ~UnlockDialog() override = default;

  void open();
  void close();
  bool is_open() const { return open_; }

  const std::vector<config::unlock_cache::UnlockItem>& items() const { return items_; }
  void set_items(std::vector<config::unlock_cache::UnlockItem> items);

  bool is_checking() const { return is_checking_; }
  void start_check();

  std::function<void()> on_dismiss;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;

  bool hit_self(PointF local) const override {
    return open_ && Widget::hit_self(local);
  }

  RectF card_rect() const { return card_rect_; }
  RectF retest_btn_rect() const { return btn_retest_rect_; }

 private:
  void reload_from_cache();

  bool open_ = false;
  bool is_checking_ = false;
  std::vector<config::unlock_cache::UnlockItem> items_;

  RectF card_rect_;
  RectF btn_close_x_rect_;
  RectF btn_retest_rect_;
  bool hover_close_x_ = false;
  bool hover_retest_ = false;
};

}  // namespace clashwin::ui::scene
