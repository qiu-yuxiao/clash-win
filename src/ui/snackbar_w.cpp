// SnackbarW 实现（T10）：NoticeService → 通知条堆叠渲染。
#include "snackbar_w.h"

#include <algorithm>

namespace clashwin::ui {

SnackbarW::~SnackbarW() {
  if (attached_) events::NoticeService::instance().unsubscribe();
}

void SnackbarW::attach(events::NoticeService& svc) {
  svc.subscribe([this]() { refresh(); });
  attached_ = true;
  refresh();
}

void SnackbarW::set_items(std::vector<events::NoticeItem> items) {
  items_ = std::move(items);
  invalidate();
}

void SnackbarW::refresh() {
  items_ = events::NoticeService::instance().snapshot();
  invalidate();
}

SizeF SnackbarW::on_measure(float max_w, float max_h) {
  (void)max_h;
  float h = static_cast<float>(items_.size()) * (item_height + gap) + gap;
  return {max_w > 0 ? max_w : 240, h};
}

void SnackbarW::on_paint(IRenderTarget& rt) {
  RectF self{0, 0, bounds().w, bounds().h};
  float y = self.h - gap;  // 自底向上堆叠（最新在下）
  for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
    y -= item_height;
    if (y < -item_height) break;
    RectF bar{8, y, self.w - 16, item_height};
    Brush bg_b{bg_};
    rt.round_rect(bar, 6, {}, &bg_b);
    // 类型色竖条
    Color4f accent = it->type == "success"
                         ? success_
                         : (it->type == "error" ? error_ : info_);
    Brush accent_b{accent};
    rt.round_rect({bar.x + 8, bar.y + 8, 3, item_height - 16}, 1.5f, {},
                  &accent_b);
    TextStyle ts;
    ts.color = text_;
    ts.align_v = AlignV::Center;
    ts.ellipsis = true;
    rt.draw_text(
        std::wstring(it->message.begin(), it->message.end()),  // UTF-8→宽近似（T13 接 util/utf）
        {bar.x + 24, y, bar.w - 32, item_height}, ts);
    y -= gap;
  }
}

}  // namespace clashwin::ui
