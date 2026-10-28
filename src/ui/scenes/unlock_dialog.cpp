#include "unlock_dialog.h"

#include <windows.h>
#include <algorithm>

#include "system/unlock_checker.h"
#include "ui/theme/theme.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

UnlockDialog::UnlockDialog(Widget* parent) : Widget(parent) {
  set_z_order(960);
  items_ = system::UnlockChecker::instance().get_default_items();
}

void UnlockDialog::open() {
  open_ = true;
  hover_close_x_ = false;
  hover_retest_ = false;
  reload_from_cache();
  invalidate();
}

void UnlockDialog::close() {
  if (!open_) return;
  open_ = false;
  invalidate();
  if (on_dismiss) on_dismiss();
}

void UnlockDialog::set_items(std::vector<config::unlock_cache::UnlockItem> items) {
  items_ = std::move(items);
  invalidate();
}

void UnlockDialog::reload_from_cache() {
  items_ = system::UnlockChecker::instance().load_cached_items();
}

void UnlockDialog::start_check() {
  if (is_checking_) return;
  is_checking_ = true;

  // 立即将各项标记为检测中
  for (auto& it : items_) {
    it.status = "Pending";
  }
  invalidate();

  // 触发并发检测
  std::thread([this]() {
    auto results = system::UnlockChecker::instance().check_all();
    this->items_ = results;
    this->is_checking_ = false;
    this->invalidate();
  }).detach();
}

void UnlockDialog::on_arrange(const RectF& area) {
  set_bounds(area);
  const float cw = 360.0f;
  const float ch = 620.0f;
  float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};
  btn_retest_rect_ = {cx + 14.0f, cy + ch - 48.0f, cw - 28.0f, 34.0f};
}

void UnlockDialog::on_paint(IRenderTarget& rt) {
  if (!open_) return;

  // 1. 半透明遮罩（局部坐标 {0,0,w,h}）
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.45f)});

  // 2. 卡片底座（圆角 8px + 阴影）
  Brush shadow_b{Color4f(0, 0, 0, 0.22f)};
  rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5}, 9.0f,
                {}, &shadow_b);

  Brush card_bg{Color4f::hex(0xEBEBEB)};
  rt.round_rect(card_rect_, 8.0f, {Color4f::hex(0xA0A0A0), 1.0f}, &card_bg);

  // 3. 标题行
  TextStyle icon_ts;
  icon_ts.size = 14.0f;
  icon_ts.weight = static_cast<int>(Weight::Bold);
  icon_ts.color = Color4f::hex(0x1A1A1A);
  icon_ts.align_v = AlignV::Center;
  rt.draw_text(L"🎬", {card_rect_.x + 16, card_rect_.y + 8, 20, 22}, icon_ts);

  TextStyle title_ts;
  title_ts.size = 13.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = Color4f::hex(0x1A1A1A);
  title_ts.align_v = AlignV::Center;
  rt.draw_text(L"流媒体与 AI 解锁检测",
               {card_rect_.x + 38, card_rect_.y + 8, card_rect_.w - 78, 22}, title_ts);

  // 关闭 ✕ 按钮
  Color4f x_bg = hover_close_x_ ? Color4f::hex(0xDCDCDC) : Color4f::hex(0xE4E4E4);
  Brush xb{x_bg};
  rt.round_rect(btn_close_x_rect_, 3.0f, {Color4f::hex(0xBEBEBE), 1.0f}, &xb);
  TextStyle x_ts;
  x_ts.size = 11.5f;
  x_ts.color = Color4f::hex(0x333333);
  x_ts.align_h = AlignH::Center;
  x_ts.align_v = AlignV::Center;
  rt.draw_text(L"✕", btn_close_x_rect_, x_ts);

  // 标题下横线
  rt.fill_rect({card_rect_.x, card_rect_.y + 36, card_rect_.w, 1.0f},
               Brush{Color4f::hex(0xCCCCCC)});

  // 4. 12 平台行绘制
  float row_y = card_rect_.y + 44.0f;
  const float row_h = 37.0f;
  const float row_spacing = 4.0f;

  TextStyle name_ts;
  name_ts.size = 11.0f;
  name_ts.weight = static_cast<int>(Weight::Bold);
  name_ts.color = Color4f::hex(0x1E1E1E);
  name_ts.align_v = AlignV::Center;
  name_ts.ellipsis = true;

  TextStyle reg_ts;
  reg_ts.size = 10.5f;
  reg_ts.color = Color4f::hex(0x616161);
  reg_ts.align_v = AlignV::Center;

  for (size_t i = 0; i < items_.size() && i < 12; ++i) {
    const auto& it = items_[i];
    RectF row_rect = {card_rect_.x + 14.0f, row_y, card_rect_.w - 28.0f, row_h};

    // 行底框（微微凹陷或圆角框）
    Brush row_bg{Color4f::hex(0xE0E0E0)};
    rt.round_rect(row_rect, 4.0f, {Color4f::hex(0xBDBDBD), 1.0f}, &row_bg);

    // 平台名称
    rt.draw_text(utf::utf8_to_utf16(it.name),
                 {row_rect.x + 8.0f, row_rect.y, 140.0f, row_h}, name_ts);

    // 地区代码
    if (it.region && !it.region->empty()) {
      rt.draw_text(utf::utf8_to_utf16(*it.region),
                   {row_rect.x + 152.0f, row_rect.y, 45.0f, row_h}, reg_ts);
    }

    // 状态 Badge
    RectF badge_rect = {row_rect.x + row_rect.w - 84.0f, row_rect.y + 7.0f, 76.0f, 23.0f};
    TextStyle badge_ts;
    badge_ts.size = 10.5f;
    badge_ts.weight = static_cast<int>(Weight::Bold);
    badge_ts.align_h = AlignH::Center;
    badge_ts.align_v = AlignV::Center;

    if (it.status == "Yes" || it.status == "已解锁") {
      badge_ts.color = Color4f::hex(0x2E7D32);  // 绿色
      Brush b_bg{Color4f::hex(0xE8F5E9)};
      rt.round_rect(badge_rect, 3.0f, {Color4f::hex(0x81C784), 1.0f}, &b_bg);
      rt.draw_text(L"☑ 已解锁", badge_rect, badge_ts);
    } else if (it.status == "No" || it.status == "未解锁") {
      badge_ts.color = Color4f::hex(0xD32F2F);  // 红色
      Brush b_bg{Color4f::hex(0xFFEBEE)};
      rt.round_rect(badge_rect, 3.0f, {Color4f::hex(0xE57373), 1.0f}, &b_bg);
      rt.draw_text(L"✖ 未解锁", badge_rect, badge_ts);
    } else if (it.status == "Pending" || it.status == "检测中") {
      badge_ts.color = Color4f::hex(0x1976D2);  // 蓝色
      Brush b_bg{Color4f::hex(0xE3F2FD)};
      rt.round_rect(badge_rect, 3.0f, {Color4f::hex(0x64B5F6), 1.0f}, &b_bg);
      rt.draw_text(L"⏳ 检测中", badge_rect, badge_ts);
    } else {
      badge_ts.color = Color4f::hex(0xE65100);  // 橙色错误
      Brush b_bg{Color4f::hex(0xFFF3E0)};
      rt.round_rect(badge_rect, 3.0f, {Color4f::hex(0xFFB74D), 1.0f}, &b_bg);
      rt.draw_text(L"⚠️ 错误", badge_rect, badge_ts);
    }

    row_y += row_h + row_spacing;
  }

  // 5. 底部"重新检测全部平台"按钮
  Color4f ret_bg = hover_retest_ ? Color4f::hex(0xFFF3E0) : Color4f::hex(0xF4F4F4);
  Brush ret_b{ret_bg};
  rt.round_rect(btn_retest_rect_, 4.0f, {Color4f::hex(0xBEBEBE), 1.0f}, &ret_b);

  TextStyle ret_ts;
  ret_ts.size = 11.5f;
  ret_ts.weight = static_cast<int>(Weight::Bold);
  ret_ts.color = Color4f::hex(0xE65100);
  ret_ts.align_h = AlignH::Center;
  ret_ts.align_v = AlignV::Center;
  std::wstring ret_label = is_checking_ ? L"⏳ 正在并发检测 12 平台..." : L"🔄 重新检测全部平台";
  rt.draw_text(ret_label, btn_retest_rect_, ret_ts);
}

bool UnlockDialog::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hx = btn_close_x_rect_.contains(pt.x, pt.y);
    bool hr = btn_retest_rect_.contains(pt.x, pt.y);
    if (hx != hover_close_x_ || hr != hover_retest_) {
      hover_close_x_ = hx;
      hover_retest_ = hr;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_x_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (btn_retest_rect_.contains(pt.x, pt.y)) {
      start_check();
      return true;
    }
    // 点击卡片外遮罩关闭
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }

  if (e.type == MouseEventType::Down) {
    return true;  // 拦截下层点击
  }

  return true;
}

bool UnlockDialog::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
    close();
    return true;
  }
  return false;
}

}  // namespace clashwin::ui::scene
