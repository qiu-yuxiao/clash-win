#include "add_url_dialog.h"

#include <windows.h>
#include <algorithm>

#include "ui/theme/theme.h"
#include "util/str_utils.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

namespace {

std::string read_win32_clipboard() {
  if (!OpenClipboard(nullptr)) return "";
  HANDLE hData = GetClipboardData(CF_UNICODETEXT);
  if (!hData) {
    CloseClipboard();
    return "";
  }
  const wchar_t* pszText = static_cast<const wchar_t*>(GlobalLock(hData));
  std::string res;
  if (pszText) {
    res = utf::utf16_to_utf8(pszText);
    GlobalUnlock(hData);
  }
  CloseClipboard();
  return res;
}

}  // namespace

AddUrlDialog::AddUrlDialog(Widget* parent) : Widget(parent) {
  set_z_order(960);
}

void AddUrlDialog::open() {
  open_ = true;
  error_message_.clear();
  hover_close_x_ = false;
  hover_paste_ = false;
  hover_cancel_ = false;
  hover_add_ = false;
  input_focused_ = true;
  invalidate();
}

void AddUrlDialog::close() {
  if (!open_) return;
  open_ = false;
  input_focused_ = false;
  invalidate();
  if (on_dismiss) on_dismiss();
}

void AddUrlDialog::set_payload(const std::string& p) {
  payload_ = p;
  invalidate();
}

void AddUrlDialog::set_target(core::quick_routing::RuleTarget t) {
  target_ = t;
  invalidate();
}

void AddUrlDialog::do_paste() {
  std::string text = read_win32_clipboard();
  if (!text.empty()) {
    // Strip trailing newlines or spaces
    payload_ = util::trim(text);
    error_message_.clear();
    invalidate();
  }
}

void AddUrlDialog::do_submit() {
  std::string val = util::trim(payload_);
  if (val.empty()) {
    error_message_ = "请输入有效的域名或 IP";
    invalidate();
    return;
  }
  auto res = core::quick_routing::add_quick_routing_rule(
      core::quick_routing::RuleType::Domain, val, target_);
  if (res.ok) {
    error_message_.clear();
    std::string r = res.rule;
    close();
    if (on_rule_added) on_rule_added(r);
  } else {
    error_message_ = res.error.empty() ? "添加分流规则失败" : res.error;
    invalidate();
  }
}

void AddUrlDialog::on_arrange(const RectF& area) {
  set_bounds(area);
  const float cw = 360.0f;
  const float ch = 290.0f;
  float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};

  input_rect_ = {cx + 18.0f, cy + 68.0f, 252.0f, 32.0f};
  btn_paste_rect_ = {cx + 278.0f, cy + 68.0f, 64.0f, 32.0f};

  seg_rect_ = {cx + 18.0f, cy + 134.0f, 324.0f, 30.0f};
  float seg_w = 324.0f / 3.0f;
  seg_direct_rect_ = {cx + 18.0f, cy + 134.0f, seg_w, 30.0f};
  seg_proxy_rect_ = {cx + 18.0f + seg_w, cy + 134.0f, seg_w, 30.0f};
  seg_reject_rect_ = {cx + 18.0f + seg_w * 2.0f, cy + 134.0f, seg_w, 30.0f};

  btn_cancel_rect_ = {cx + 194.0f, cy + ch - 42.0f, 68.0f, 28.0f};
  btn_add_rect_ = {cx + 270.0f, cy + ch - 42.0f, 72.0f, 28.0f};
}

void AddUrlDialog::on_paint(IRenderTarget& rt) {
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
  rt.draw_text(L"✚", {card_rect_.x + 16, card_rect_.y + 8, 20, 22}, icon_ts);

  TextStyle title_ts;
  title_ts.size = 13.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = Color4f::hex(0x1A1A1A);
  title_ts.align_v = AlignV::Center;
  rt.draw_text(L"添加自定义分流规则",
               {card_rect_.x + 36, card_rect_.y + 8, card_rect_.w - 76, 22}, title_ts);

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

  // 4. 输入框提示文字
  TextStyle subtitle_ts;
  subtitle_ts.size = 10.5f;
  subtitle_ts.color = Color4f::hex(0x222222);
  subtitle_ts.align_v = AlignV::Center;
  rt.draw_text(L"请输入域名或 IP (例如: google.com 或 1.1.1.1):",
               {card_rect_.x + 18, card_rect_.y + 46, card_rect_.w - 36, 18}, subtitle_ts);

  // 5. 规则输入框（深色底 + 橙色边框 + 白色文字）
  Brush input_bg{Color4f::hex(0x6E6E6E)};
  Pen input_pen{Color4f::hex(0xE65100), 1.5f};
  rt.round_rect(input_rect_, 4.0f, input_pen, &input_bg);

  TextStyle input_ts;
  input_ts.size = 11.5f;
  input_ts.color = Color4f::hex(0xFFFFFF);
  input_ts.align_v = AlignV::Center;
  std::wstring payload_w = utf::utf8_to_utf16(payload_);
  if (input_focused_) payload_w += L"|";
  rt.draw_text(payload_w,
               {input_rect_.x + 8, input_rect_.y, input_rect_.w - 16, input_rect_.h},
               input_ts);

  // 粘贴按钮
  Color4f p_bg = hover_paste_ ? Color4f::hex(0xE0E0E0) : Color4f::hex(0xF4F4F4);
  Brush pb{p_bg};
  rt.round_rect(btn_paste_rect_, 4.0f, {Color4f::hex(0xBEBEBE), 1.0f}, &pb);
  TextStyle paste_ts;
  paste_ts.size = 11.0f;
  paste_ts.color = Color4f::hex(0x222222);
  paste_ts.align_h = AlignH::Center;
  paste_ts.align_v = AlignV::Center;
  rt.draw_text(L"📋粘贴", btn_paste_rect_, paste_ts);

  // 6. 分流策略偏好提示
  TextStyle pref_ts;
  pref_ts.size = 10.5f;
  pref_ts.color = Color4f::hex(0x222222);
  pref_ts.align_v = AlignV::Center;
  rt.draw_text(L"分流策略偏好:",
               {card_rect_.x + 18, card_rect_.y + 112, card_rect_.w - 36, 18}, pref_ts);

  // 7. 三段式策略开关（DIRECT / PROXY / REJECT）
  Brush seg_bg{Color4f::hex(0xD8D8D8)};
  rt.round_rect(seg_rect_, 4.0f, {Color4f::hex(0xAAAAAA), 1.0f}, &seg_bg);

  auto draw_seg_item = [&](const RectF& r, const wchar_t* label, bool active) {
    if (active) {
      Brush act_b{Color4f::hex(0xFFFFFF)};
      rt.round_rect(r, 3.0f, {Color4f::hex(0xE65100), 1.0f}, &act_b);
      TextStyle ts;
      ts.size = 11.0f;
      ts.weight = static_cast<int>(Weight::Bold);
      ts.color = Color4f::hex(0xE65100);
      ts.align_h = AlignH::Center;
      ts.align_v = AlignV::Center;
      rt.draw_text(label, r, ts);
    } else {
      TextStyle ts;
      ts.size = 11.0f;
      ts.color = Color4f::hex(0x616161);
      ts.align_h = AlignH::Center;
      ts.align_v = AlignV::Center;
      rt.draw_text(label, r, ts);
    }
  };

  draw_seg_item(seg_direct_rect_, L"DIRECT",
                target_ == core::quick_routing::RuleTarget::Direct);
  draw_seg_item(seg_proxy_rect_, L"PROXY",
                target_ == core::quick_routing::RuleTarget::Proxy);
  draw_seg_item(seg_reject_rect_, L"REJECT",
                target_ == core::quick_routing::RuleTarget::Reject);

  // 8. 错误信息提示
  if (!error_message_.empty()) {
    TextStyle err_ts;
    err_ts.size = 10.0f;
    err_ts.color = Color4f::hex(0xD32F2F);
    err_ts.align_v = AlignV::Center;
    rt.draw_text(utf::utf8_to_utf16(error_message_),
                 {card_rect_.x + 18, card_rect_.y + 172, card_rect_.w - 36, 18}, err_ts);
  }

  // 9. 底部取消与添加按钮
  Color4f c_bg = hover_cancel_ ? Color4f::hex(0xE0E0E0) : Color4f::hex(0xF4F4F4);
  Brush cb{c_bg};
  rt.round_rect(btn_cancel_rect_, 4.0f, {Color4f::hex(0xBEBEBE), 1.0f}, &cb);
  TextStyle cancel_ts;
  cancel_ts.size = 11.0f;
  cancel_ts.color = Color4f::hex(0x333333);
  cancel_ts.align_h = AlignH::Center;
  cancel_ts.align_v = AlignV::Center;
  rt.draw_text(L"取消", btn_cancel_rect_, cancel_ts);

  Color4f a_bg = hover_add_ ? Color4f::hex(0xFFF3E0) : Color4f::hex(0xFBFBFB);
  Brush ab{a_bg};
  rt.round_rect(btn_add_rect_, 4.0f, {Color4f::hex(0xBEBEBE), 1.0f}, &ab);
  TextStyle add_ts;
  add_ts.size = 11.5f;
  add_ts.weight = static_cast<int>(Weight::Bold);
  add_ts.color = Color4f::hex(0xE65100);
  add_ts.align_h = AlignH::Center;
  add_ts.align_v = AlignV::Center;
  rt.draw_text(L"添加", btn_add_rect_, add_ts);
}

bool AddUrlDialog::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hx = btn_close_x_rect_.contains(pt.x, pt.y);
    bool hp = btn_paste_rect_.contains(pt.x, pt.y);
    bool hc = btn_cancel_rect_.contains(pt.x, pt.y);
    bool ha = btn_add_rect_.contains(pt.x, pt.y);
    if (hx != hover_close_x_ || hp != hover_paste_ || hc != hover_cancel_ || ha != hover_add_) {
      hover_close_x_ = hx;
      hover_paste_ = hp;
      hover_cancel_ = hc;
      hover_add_ = ha;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_x_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (btn_paste_rect_.contains(pt.x, pt.y)) {
      do_paste();
      return true;
    }
    if (seg_direct_rect_.contains(pt.x, pt.y)) {
      target_ = core::quick_routing::RuleTarget::Direct;
      invalidate();
      return true;
    }
    if (seg_proxy_rect_.contains(pt.x, pt.y)) {
      target_ = core::quick_routing::RuleTarget::Proxy;
      invalidate();
      return true;
    }
    if (seg_reject_rect_.contains(pt.x, pt.y)) {
      target_ = core::quick_routing::RuleTarget::Reject;
      invalidate();
      return true;
    }
    if (btn_cancel_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (btn_add_rect_.contains(pt.x, pt.y)) {
      do_submit();
      return true;
    }
    if (input_rect_.contains(pt.x, pt.y)) {
      input_focused_ = true;
      invalidate();
      return true;
    }
    // 点击卡片外部遮罩：关闭
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

bool AddUrlDialog::on_key(const KeyEvent& e) {
  if (!open_) return false;

  if (e.type == KeyEventType::Down) {
    if (e.key == VK_ESCAPE || e.key == 27) {
      close();
      return true;
    }
    if (e.key == VK_RETURN || e.key == 13) {
      do_submit();
      return true;
    }
    if (e.key == VK_BACK && input_focused_) {
      if (!payload_.empty()) {
        payload_.pop_back();
        invalidate();
      }
      return true;
    }
  } else if (e.type == KeyEventType::Char && input_focused_) {
    if (e.ch >= 32 && e.ch < 127) {
      payload_.push_back(static_cast<char>(e.ch));
      invalidate();
      return true;
    }
  }

  return false;
}

}  // namespace clashwin::ui::scene
