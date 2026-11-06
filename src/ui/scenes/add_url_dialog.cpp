// 添加网址弹窗实现（T21，按 add-url-dialog.tsx 源码重做）。
#include "add_url_dialog.h"

#include <windows.h>

#include <algorithm>

#include "ui/theme/theme.h"
#include "util/str_utils.h"
#include "util/utf.h"

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

Color4f card_text() {
  // src: button-styles.ts:535 get3DCardStyle('default').color
  //   light '#3C2F0F' / dark '#2C1F03'
  return is_light() ? Color4f::hex(0x3C2F0F) : Color4f::hex(0x2C1F03);
}

void draw_card(IRenderTarget& rt, const RectF& r) {
  // src: button-styles.ts:534-565 get3DCardStyle('default')：radius 6 + 135° 4 停靠
  GradientStop stops[4];
  if (is_light()) {
    // src: button-styles.ts:538 light #FFFDE7@0 #FFF9C4@35 #FFF59D@70 #FFE082@100
    stops[0] = {0.00f, Color4f::hex(0xFFFDE7)};
    stops[1] = {0.35f, Color4f::hex(0xFFF9C4)};
    stops[2] = {0.70f, Color4f::hex(0xFFF59D)};
    stops[3] = {1.00f, Color4f::hex(0xFFE082)};
  } else {
    // src: button-styles.ts:539 dark #FFF59D@0 #FBC02D@35 #F57F17@70 #E65100@100
    stops[0] = {0.00f, Color4f::hex(0xFFF59D)};
    stops[1] = {0.35f, Color4f::hex(0xFBC02D)};
    stops[2] = {0.70f, Color4f::hex(0xF57F17)};
    stops[3] = {1.00f, Color4f::hex(0xE65100)};
  }
  rt.fill_round_rect_gradient_angle(r, 6.0f, stops, 4, 135.0f);  // src: button-styles.ts:544
  // src: :540 bevelShadowDark light '#B8860B' / dark '#755500'
  Brush bevel{is_light() ? Color4f::hex(0xB8860B) : Color4f::hex(0x755500)};
  rt.fill_rect({r.x + 6.0f, r.bottom() - 1.0f, r.w - 12.0f, 1.0f}, bevel);
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
  invalidate();
}

void AddUrlDialog::close() {
  if (!open_) return;
  open_ = false;
  input_focused_ = false;
  payload_.clear();  // src: add-url-dialog.tsx:49-53 handleClose setText('')
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
  auto res = core::quick_routing::add_quick_routing_rule(
      core::quick_routing::RuleType::Domain, val, target_);
  if (res.ok) {
    error_message_.clear();
    std::string r = res.rule;
    close();
    if (on_rule_added) on_rule_added(r);
  } else {
    // src: :74-76 showNotice.error(message)
    error_message_ = res.error.empty() ? "添加分流规则失败" : res.error;
    invalidate();
  }
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
  if (!open_) return;
  // 1. backdrop（src: MUI Dialog 默认 backdrop rgba(0,0,0,.5)；组件库默认值）
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  // 2. paper：3D 渐变卡（get3DCardStyle default）+ radius 6
  draw_card(rt, card_rect_);

  // 3. 标题 src: :206-216 15px bold、px 16、py 12
  TextStyle title_ts;
  title_ts.size = kTitleFs;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = card_text();
  title_ts.align_v = AlignV::Center;
  rt.draw_text(L"添加网址",
               {title_rect_.x + kTitlePx, title_rect_.y, title_rect_.w - kTitlePx * 2,
                title_rect_.h},
               title_ts);

  // 4. TextField multiline（get3DInputStyle）
  // src: button-styles.ts:734-778 bg rgba(255,253,231,.8)/rgba(26,22,14,.4)、
  //   outline 2px rgba(212,175,55,.35/.25)、focus border '#B8860B'/'#FFA000'
  Brush input_bg{is_light() ? Color4f::hex(0xFFFDE7, 0.8f)
                            : Color4f::hex(0x1A160E, 0.4f)};
  Pen input_pen{input_focused_
                    ? (is_light() ? Color4f::hex(0xB8860B) : Color4f::hex(0xFFA000))
                    : Color4f::hex(0xD4AF37, is_light() ? 0.35f : 0.25f),
                1.0f};
  rt.round_rect(input_rect_, 4.0f, input_pen, &input_bg);
  {
    TextStyle ts;
    ts.size = 13.0f;  // src: get3DInputStyle fontSize 13px
    ts.color = is_light() ? Color4f::hex(0x2C1F03) : Color4f::hex(0xFFFDE7);
    ts.wrap = true;
    ts.max_lines = static_cast<int>(kRows);
    std::wstring body = utf::utf8_to_utf16(payload_);
    if (body.empty()) {
      // placeholder src: :227-229 '请填入需要代理的网址（每行一个）'
      // MUI 空值时即使聚焦也显示 placeholder，光标叠加在行首
      TextStyle ph = ts;
      ph.color.a *= 0.5f;
      rt.draw_text(L"请填入需要代理的网址（每行一个）",
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
  {
    // outlined default src: button-styles.ts get3DButtonStyle(outlined, default)
    Brush c_bg{hover_cancel_ ? Color4f::hex(0xD4AF37, 0.14f)
                             : Color4f::hex(0xD4AF37, 0.06f)};
    Pen c_pen{Color4f::hex(0xD4AF37, is_light() ? 0.35f : 0.22f), 1.0f};
    rt.round_rect(btn_cancel_rect_, 6.0f, c_pen, &c_bg);
    TextStyle ts;
    ts.size = kBtnFs;
    ts.weight = static_cast<int>(Weight::Bold);
    ts.color = card_text();
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt.draw_text(L"取消", btn_cancel_rect_, ts);
  }
  {
    // contained primary src: button-styles.ts:38-39 retro-3d 135° 六停（与卡片同源）
    GradientStop st[6];
    st[0] = {0.00f, Color4f::hex(0xFFE082)};
    st[1] = {0.20f, Color4f::hex(0xFFD54F)};
    st[2] = {0.45f, Color4f::hex(0xFFC107)};
    st[3] = {0.70f, Color4f::hex(0xFFB300)};
    st[4] = {0.85f, Color4f::hex(0xD4AF37)};
    st[5] = {1.00f, Color4f::hex(0xB8860B)};
    rt.fill_round_rect_gradient_angle(btn_add_rect_, 6.0f, st, 6, 135.0f);  // src: :544
    // src: button-styles.ts:36-37 borderColor light '#8A6D00' / dark '#634E00'，2px
    Pen bevel{is_light() ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00), 2.0f};
    rt.round_rect({btn_add_rect_.x + 1, btn_add_rect_.y + 1,
                   btn_add_rect_.w - 2, btn_add_rect_.h - 2},
                  5.0f, bevel, nullptr);
    TextStyle ts;
    ts.size = kBtnFs;
    ts.weight = static_cast<int>(Weight::Bold);
    ts.color = Color4f::hex(0x2C1F03);  // src: button-styles.ts:1105-1121 retro-3d
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt.draw_text(L"添加", btn_add_rect_, ts);
  }

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

}  // namespace clashwin::ui::scene
