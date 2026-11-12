// 添加网址弹窗实现（T21，按 add-url-dialog.tsx 源码重做）。
#include "add_url_dialog.h"

#include <windows.h>

#include <algorithm>

#include "ui/card_draw.h"
#include "ui/theme/theme.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01
#include "events/notice.h"

namespace clashwin::ui::scene {

namespace {

// ---------- 源码常量（逻辑 DIP） ----------
// src: add-url-dialog.tsx:200-202 width 'calc(100% - 32px)'; maxWidth '380px'
constexpr float kMaxW = 380.0f, kEdge = 32.0f;
// src: add-url-dialog.tsx:210-212 fontSize '15px'、px 2=16、py 1.5=12
constexpr float kTitlePx = 16.0f, kTitlePy = 12.0f, kTitleFs = 15.0f;
// src: add-url-dialog.tsx:217 px 2=16、pb 1=8
constexpr float kContentPx = 16.0f, kContentPb = 8.0f;
// src: add-url-dialog.tsx:235 px 2=16、py 1.5=12、gap 1=8
constexpr float kActsPx = 16.0f, kActsPy = 12.0f, kActsGap = 8.0f;
// src: add-url-dialog.tsx:221-222 multiline minRows 4（13px × 行高 1.4375 ≈ 18.7）
constexpr float kRows = 4.0f, kLineH = 20.0f;
// src: add-url-dialog.tsx:236-249 MUI Button medium 高 36.5、minWidth 64、fontSize 13
constexpr float kBtnH = 36.5f, kBtnW = 64.0f, kBtnFs = 13.0f;

bool is_light() { return theme::ThemeService::instance().is_light(); }

// 卡面文字色 ← get3DCardStyle(theme,'default').color（6 套皮肤不同）
Color4f card_text() {
  return theme::ThemeService::instance().card_paint("default").text_color;
}

}  // namespace

AddUrlDialog::AddUrlDialog(Widget* parent) : Widget(parent) {
  set_z_order(960);
}

void AddUrlDialog::open() {
  open_ = true;
  error_message_.clear();
  hover_cancel_ = false;
  hover_add_ = false;
  input_focused_ = true;  // src: :220 autoFocus
  fade_.show();           // ← MUI Dialog 缺省 Fade（enter 225ms）
  invalidate();
}

void AddUrlDialog::close() {
  if (!open_) return;
  open_ = false;
  input_focused_ = false;
  payload_.clear();  // src: add-url-dialog.tsx:49-53 handleClose setText('')
  fade_.hide();      // ← MUI Fade leave 195ms：画面延后消失，交互立刻失效
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

void AddUrlDialog::do_submit() {
  std::string val = util::trim(payload_);
  if (val.empty()) {
    // src: :56-59 空文本 → 直接关闭（不报错）
    close();
    return;
  }
  // ← add-url-dialog.tsx:54-72：批量解析（换行/逗号/分号），返回新增条数。
  //   count > 0 → success「已添加 {{count}} 条规则」；count == 0 → info「没有新增规则」；
  //   异常 → error 原文。此前 Win 只走单条接口，多行输入只加第一条，且无任何提示。
  const int count = core::quick_routing::add_quick_routing_rules(val);
  error_message_.clear();
  if (count > 0) {
    events::NoticeService::instance().push(
        "success", tr8("settings.mini.addUrlSuccess", {{"count", std::to_string(count)}}));
  } else {
    events::NoticeService::instance().push("info", tr8("settings.mini.addUrlNoChange"));
  }
  close();
}

void AddUrlDialog::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: :200-202 width calc(100%-32px)、maxWidth 380
  float cw = std::min(kMaxW, std::max(0.0f, area.w - kEdge));
  // 高度 = 标题(12+20+12) + 内容(8+4行×20+8) + 动作(12+36.5+12)
  const float title_h = kTitlePy * 2 + 20.0f;         // 44
  const float input_h = kRows * kLineH + 8.0f;        // 88（文本框内上下留白 4）
  const float content_h = 8.0f + input_h + kContentPb;  // 104
  const float acts_h = kActsPy * 2 + kBtnH;           // 60.5
  float ch = title_h + content_h + acts_h;            // 208.5
  float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  title_rect_ = {cx, cy, cw, title_h};
  input_rect_ = {cx + kContentPx, cy + title_h + 8.0f,
                 cw - kContentPx * 2, input_h};

  // 动作行右对齐：添加在右，取消在其左（gap 8）
  float acts_y = cy + ch - kActsPy - kBtnH;
  btn_add_rect_ = {cx + cw - kActsPx - kBtnW, acts_y, kBtnW, kBtnH};
  btn_cancel_rect_ = {btn_add_rect_.x - kActsGap - kBtnW, acts_y, kBtnW, kBtnH};
}

void AddUrlDialog::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();  // 自续帧，直到过渡结束
  set_opacity(fade_.value());        // 每帧同步整棵子树的不透明度
  if (fade_.hidden()) return;
  // 1. backdrop（src: MUI Dialog 默认 backdrop rgba(0,0,0,.5)；组件库默认值）
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  // 2. paper：3D 渐变卡（get3DCardStyle default）+ radius 6
  draw_card(rt, card_rect_, theme::ThemeService::instance().card_paint("default"));

  // 3. 标题 src: :206-216 15px bold、px 16、py 12
  TextStyle title_ts;
  title_ts.size = kTitleFs;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = card_text();
  title_ts.align_v = AlignV::Center;
  rt.draw_text(tr("settings.mini.addUrl"),
               {title_rect_.x + kTitlePx, title_rect_.y, title_rect_.w - kTitlePx * 2,
                title_rect_.h},
               title_ts);

  // 4. TextField multiline（get3DInputStyle）
  // src: button-styles.ts:734-778 bg rgba(255,253,231,.8)/rgba(26,22,14,.4)、
  //   outline 2px rgba(212,175,55,.35/.25)、focus border '#B8860B'/'#FFA000'
  // T12：6 套皮肤走 input_paint() 查表（← get3DInputStyle）
  const theme::InputPaint ip = theme::ThemeService::instance().input_paint();
  Brush input_bg{ip.bg};
  Pen input_pen{input_focused_ ? ip.border_focused : ip.border,
                input_focused_ ? ip.border_w_focused : ip.border_w};
  rt.round_rect(input_rect_, ip.radius, input_pen, &input_bg);
  {
    TextStyle ts;
    ts.size = 13.0f;  // src: get3DInputStyle fontSize 13px
    ts.color = ip.text;
    ts.wrap = true;
    ts.max_lines = static_cast<int>(kRows);
    std::wstring body = utf::utf8_to_utf16(payload_);
    if (body.empty()) {
      // placeholder src: :227-229 '请填入需要代理的网址（每行一个）'
      // MUI 空值时即使聚焦也显示 placeholder，光标叠加在行首
      TextStyle ph = ts;
      ph.color.a *= 0.5f;
      rt.draw_text(tr("settings.mini.addUrlPlaceholder"),
                   {input_rect_.x + 6, input_rect_.y + 4, input_rect_.w - 12,
                    input_rect_.h - 8},
                   ph);
      if (input_focused_) {
        rt.draw_text(L"|", {input_rect_.x + 6, input_rect_.y + 4,
                            input_rect_.w - 12, input_rect_.h - 8}, ts);
      }
    } else {
      body += input_focused_ ? L"|" : L"";
      rt.draw_text(body,
                   {input_rect_.x + 6, input_rect_.y + 4, input_rect_.w - 12,
                    input_rect_.h - 8},
                   ts);
    }
  }

  // 5. 动作按钮：取消 outlined default、添加 contained primary（src: :236-249）
  // T12：6 套皮肤走 button_paint() 查表（← get3DButtonStyle）
  auto paint_btn = [&](const RectF& r, const char* ctype, bool outlined,
                       bool hover, const std::wstring& label) {
    const auto bp =
        theme::ThemeService::instance().button_paint(ctype, outlined);
    if (bp.has_shadow)
      draw_soft_shadow(rt, r, bp.radius, bp.shadow_color, bp.shadow_dy,
                       bp.shadow_blur);
    if (bp.glow.a > 0) draw_glow(rt, r, bp.radius, bp.glow, bp.glow_blur);
    if (bp.use_grad) {
      const GradientStop* gs = hover ? bp.grad_hover : bp.grad;
      rt.fill_round_rect_gradient_angle(r, bp.radius, gs, bp.grad_count,
                                        bp.grad_angle);
    } else {
      Brush fb{hover ? bp.fill_hover : bp.fill};
      rt.round_rect(r, bp.radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &fb);
    }
    if (bp.border_w > 0.01f)
      rt.round_rect(r, bp.radius, Pen{bp.border, bp.border_w}, nullptr);
    if (bp.use_grad && bp.bevel_dark.a > 0) {
      rt.draw_line({r.x + bp.radius, r.y + 1.0f},
                   {r.right() - bp.radius, r.y + 1.0f}, Pen{bp.bevel_hi, 1.0f});
      rt.draw_line({r.x + bp.radius, r.bottom() - 1.0f},
                   {r.right() - bp.radius, r.bottom() - 1.0f},
                   Pen{bp.bevel_dark, 1.0f});
    }
    TextStyle ts;
    ts.size = kBtnFs;
    ts.weight = bp.weight;
    ts.color = bp.text;
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt.draw_text(label, r, ts);
  };
  paint_btn(btn_cancel_rect_, "default", /*outlined=*/true, hover_cancel_,
            tr("shared.actions.cancel"));
  paint_btn(btn_add_rect_, "primary", /*outlined=*/false, hover_add_,
            tr("settings.mini.addUrlButton"));

  // 6. 错误提示（showNotice.error → 对话框内红字，源码为 toast；此处保留内联提示）
  if (!error_message_.empty()) {
    TextStyle err_ts;
    err_ts.size = 11.0f;
    err_ts.color = Color4f::hex(0xD32F2F);  // src: MUI error.main（palettes.cpp 同值）
    err_ts.align_v = AlignV::Center;
    rt.draw_text(utf::utf8_to_utf16(error_message_),
                 {input_rect_.x, btn_cancel_rect_.y - 18, input_rect_.w, 18},
                 err_ts);
  }
}

bool AddUrlDialog::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hc = btn_cancel_rect_.contains(pt.x, pt.y);
    bool ha = btn_add_rect_.contains(pt.x, pt.y);
    if (hc != hover_cancel_ || ha != hover_add_) {
      hover_cancel_ = hc;
      hover_add_ = ha;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
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
    // src: :190-191 onClose（点 backdrop 关闭）
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }

  if (e.type == MouseEventType::Down) return true;  // 拦截下层点击

  return true;
}

bool AddUrlDialog::on_key(const KeyEvent& e) {
  if (!open_) return false;

  if (e.type == KeyEventType::Down) {
    if (e.key == VK_ESCAPE || e.key == 27) {
      close();
      return true;
    }
    // src: :220-226 multiline → Enter 插入换行（不提交）
    if ((e.key == VK_RETURN || e.key == 13) && input_focused_) {
      payload_.push_back('\n');
      invalidate();
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

std::wstring AddUrlDialog::a11y_default_name() const {
  return tr("settings.mini.addUrl");
}

A11ySubItem AddUrlDialog::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  if (idx == 0) {
    sub.role = A11yRole::Edit;
    sub.name = tr("settings.mini.addUrlPlaceholder");
    sub.local_bounds = input_rect_;
    sub.focusable = true;
    sub.can_set_value = true;
    sub.value = utf::utf8_to_utf16(payload_);
    return sub;
  }
  sub.role = A11yRole::Button;
  sub.can_invoke = enabled();
  if (idx == 1) {
    sub.name = tr("shared.actions.cancel");
    sub.local_bounds = btn_cancel_rect_;
  } else {
    sub.name = tr("settings.mini.addUrlButton");
    sub.local_bounds = btn_add_rect_;
  }
  return sub;
}

bool AddUrlDialog::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  if (idx == 1) {
    close();
    return true;
  }
  if (idx == 2) {
    do_submit();
    return true;
  }
  return false;
}

bool AddUrlDialog::a11y_set_virtual_child_value(size_t idx, const std::wstring& v) {
  if (!open_ || !enabled() || idx != 0) return false;
  set_payload(utf::utf16_to_utf8(v));
  return true;
}

}  // namespace clashwin::ui::scene
