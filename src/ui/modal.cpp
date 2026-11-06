// Modal 实现（T10）：遮罩 + 居中对话框。
#include "modal.h"

#include <algorithm>

#include "button.h"

namespace clashwin::ui {

void Modal::build_tree() {
  auto dlg = std::make_unique<Column>();
  dialog_ = dlg.get();
  dialog_->spacing = 12;
  dialog_->set_padding(20, 16, 20, 16);  // ← MUI Dialog 内容内距 p2.5/py2
  auto tl = std::make_unique<Label>();
  title_label_ = tl.get();
  title_label_->style.size = 16;  // ← MUI DialogTitle 字号 16
  title_label_->style.weight = static_cast<int>(Weight::Medium);
  title_label_->style.color = title_text_;
  dialog_->add_child(std::move(tl));
  auto br = std::make_unique<Row>();
  buttons_row_ = br.get();
  buttons_row_->main_align = MainAlign::End;
  buttons_row_->spacing = 8;
  dialog_->add_child(std::move(br));
  add_child(std::move(dlg));
  dialog_->set_visible(false);
}

void Modal::set_buttons(std::vector<std::wstring> labels) {
  buttons_ = std::move(labels);
  if (open_) layout_dialog();
}

void Modal::open(std::unique_ptr<Widget> content, const SizeF& dialog_size,
                 const std::wstring& title) {
  if (content_) {
    dialog_->remove_child(content_);  // 旧内容丢弃
    content_ = nullptr;
  }
  content_ = content.get();
  title_ = title;
  title_label_->set_text(title);
  dialog_size_ = dialog_size;
  if (content_) {
    content_->flex_weight = 1;  // Column content 占据剩余
    dialog_->add_child(std::move(content));
  }
  // 按钮排重建
  while (buttons_row_->child_count() > 0)
    buttons_row_->remove_child(buttons_row_->child(0));
  for (size_t i = 0; i < buttons_.size(); ++i) {
    auto b = std::make_unique<Button>(buttons_[i]);
    Button* bp = b.get();
    int idx = static_cast<int>(i);
    bp->on_click = [this, idx] {
      if (on_button) on_button(idx);
    };
    buttons_row_->add_child(std::move(b));
  }
  open_ = true;
  dialog_->set_visible(true);
  layout_dialog();  // 立即居中落位（on_arrange 只在根重排时触发）
  invalidate();
  if (on_open) on_open();
}

void Modal::close() {
  if (!open_) return;
  open_ = false;
  dialog_->set_visible(false);
  invalidate();
  if (on_close) on_close();
}

RectF Modal::dialog_bounds() const {
  return dialog_ ? dialog_->bounds() : RectF{};
}

void Modal::layout_dialog() {
  RectF self{0, 0, bounds().w, bounds().h};
  float x = (self.w - dialog_size_.w) / 2;
  float y = (self.h - dialog_size_.h) / 2;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  dialog_->layout_tree({x, y, dialog_size_.w, dialog_size_.h});
  // content 占据剩余（Column flex）
  if (content_) content_->flex_weight = 1;
}

bool Modal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  switch (e.type) {
    case MouseEventType::Down:
      // 点遮罩（对话框外）→ 默认关闭（可拦截）；对话框内则穿透给子
      if (!dialog_->bounds().contains(e.pos.x, e.pos.y)) {
        if (on_mask_click && on_mask_click()) return true;
        close();
        return true;
      }
      return false;  // 内部命中交 hit_test → 子处理
    case MouseEventType::Up:
      return true;   // 遮罩吞 Up
    default:
      return false;
  }
}

void Modal::on_paint(IRenderTarget& rt) {
  if (!open_) return;
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{mask_});
}

void Modal::on_arrange(const RectF& area) {
  set_bounds(area);
  if (open_) layout_dialog();
  else dialog_->set_visible(false);
}

}  // namespace clashwin::ui
