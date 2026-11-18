// 添加网址弹窗实现（T21，按 add-url-dialog.tsx 源码重做）。
#include "add_url_dialog.h"

#include <windows.h>

#include <algorithm>

#include "ui/card_draw.h"
#include "ui/soft_shadow.h"
#include "ui/theme/theme.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01
#include "events/notice.h"

namespace clashwin::ui::scene {

namespace {

// ---------- 源码常量（逻辑 DIP） ----------
// src: popup-styles.ts:173-185 窄窗弹窗锁定 271px 左锚定
constexpr float kTitlePx = 14.0f, kTitlePy = 10.8f, kTitleFs = 15.0f;  // src: popup-styles.ts:198-206 py:1.35=10.8
constexpr float kTitleLineH = 20.25f;      // src: popup-styles.ts:207 lineHeight 1.35 × 15px
constexpr float kContentPx = 14.0f, kContentPb = 12.0f;  // src: popup-styles.ts:219-220
constexpr float kActsPx = 14.0f, kActsPy = 10.8f, kActsGap = 8.0f;  // src: popup-styles.ts:239-241 py:1.35
// ★ 输入框盒模型（MUI multiline + 非 small）：
//   文本域行高不是 13×1.4375，而是 InputBase 根 `lineHeight:'1.4375em'`
//   在 16px 字号下算出的 **23px 绝对值**（子元素 fontSize 不改行高）；
//   根内距 16.5×2（OutlinedInput.js:101-106 multiline 分支），文本域自身 padding 0。
//   → 4 行 = 4×23 = 92，外框 = 92 + 33 = 125
constexpr float kRows = 4.0f, kLineH = 23.0f;
constexpr float kInputPadX = 14.0f, kInputPadY = 16.5f;
// src: add-url-dialog.tsx:249-268 全宽纵向堆叠按钮（Primary + Cancel）
//   按钮是 border-box（MUI ButtonBase），高度 = line 1.75×fs + py×2 + border×2
//   主：13×1.75 + 0.85×8×2 = 36.35；次：12.5×1.75 + 0.7×8×2 = 35.08；
//   retro-3d 皮肤再各加 2×2px 实边框（button-styles.ts:80 border 2px×depth）
//   border 宽随皮肤（retro 2px / 其余 1px），故只存"内容+内距"部分，
//   实际高度在 on_arrange 里 + border_w×2（读 ButtonPaint.border_w）
constexpr float kPrimaryBtnContentH = 36.35f, kSecondaryBtnContentH = 35.08f;
constexpr float kBtnFs = 13.0f;

std::function<std::wstring()>& test_clipboard_reader() {
  static std::function<std::wstring()> fn;
  return fn;
}

std::string read_clipboard_utf8_normalized() {
  std::wstring wtext;
  if (test_clipboard_reader()) {
    wtext = test_clipboard_reader()();
  } else {
    if (!OpenClipboard(nullptr)) return "";
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
      if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
        wtext = p;
        GlobalUnlock(h);
      }
    }
    CloseClipboard();
  }
  if (wtext.empty()) return "";
  std::string utf8 = utf::utf16_to_utf8(wtext);
  std::string out;
  out.reserve(utf8.size());
  for (size_t i = 0; i < utf8.size(); ++i) {
    if (utf8[i] == '\r') {
      out.push_back('\n');
      if (i + 1 < utf8.size() && utf8[i + 1] == '\n') ++i;
    } else {
      out.push_back(utf8[i]);
    }
  }
  return out;
}

void write_clipboard_utf8(const std::string& utf8) {
  if (!OpenClipboard(nullptr)) return;
  EmptyClipboard();
  std::wstring w = utf::utf8_to_utf16(utf8);
  const size_t bytes = (w.size() + 1) * sizeof(wchar_t);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (mem) {
    if (void* ptr = GlobalLock(mem)) {
      memcpy(ptr, w.data(), bytes);
      GlobalUnlock(mem);
      SetClipboardData(CF_UNICODETEXT, mem);
    }
  }
  CloseClipboard();
}

}  // namespace

void AddUrlDialog::set_clipboard_reader_for_test(std::function<std::wstring()> fn) {
  test_clipboard_reader() = std::move(fn);
}

AddUrlDialog::AddUrlDialog(Widget* parent) : Widget(parent) {
  set_z_order(960);
}

void AddUrlDialog::open() {
  open_ = true;
  error_message_.clear();
  hover_cancel_ = false;
  hover_add_ = false;
  input_focused_ = true;  // src: add-url-dialog.tsx:220 autoFocus
  all_selected_ = false;
  fade_.show();           // ← MUI Dialog 缺省 Fade（enter 225ms）
  invalidate();
}

void AddUrlDialog::close() {
  if (!open_) return;
  open_ = false;
  input_focused_ = false;
  all_selected_ = false;
  payload_.clear();  // src: add-url-dialog.tsx:49-53 handleClose setText('')
  // ← MUI Fade leave 195ms：画面延后消失，交互立刻失效
  fade_.hide();
  if (context_menu_ && context_menu_->is_open()) context_menu_->close();
  invalidate();
  if (on_dismiss) on_dismiss();
}

void AddUrlDialog::set_payload(const std::string& p) {
  payload_ = p;
  all_selected_ = false;
  invalidate();
}

void AddUrlDialog::set_target(core::quick_routing::RuleTarget t) {
  target_ = t;
  invalidate();
}

void AddUrlDialog::copy_payload_to_clipboard() {
  if (payload_.empty()) return;
  write_clipboard_utf8(payload_);
}

void AddUrlDialog::open_context_menu(PointF anchor) {
  if (!context_menu_) return;
  const auto& svc = theme::ThemeService::instance();
  const auto tokens = svc.popup_tokens();
  MenuStyle st;
  st.item_h = 40;
  st.sep_h = 13;
  st.min_w = 160;  // src: add-url-dialog.tsx:288 minWidth '160px'
  st.radius = 6;
  st.font_size = 13.0f;
  st.pad_v = 4;
  st.padding_h = 16;
  st.bg = svc.popup_card_paint().bg;
  st.border = tokens.divider_color;
  st.text = tokens.primary_text;
  st.disabled_text = tokens.disabled_text;
  st.danger_text = tokens.error_color;
  const bool has_text = !payload_.empty();
  const bool has_sel = has_text && all_selected_;

  std::vector<MenuItem> items;
  items.push_back({L"✂️ " + tr("shared.actions.cut"), !has_sel, false, L"", false});
  items.push_back({L"📋 " + tr("shared.actions.copy"), !has_sel, false, L"", false});
  items.push_back({L"📥 " + tr("shared.actions.paste"), false, false, L"", false});
  items.push_back({L"🔍 " + tr("shared.actions.selectAll"), !has_text, false, L"", false});
  items.push_back({L"", false, true, L"", false});
  items.push_back({L"🧹 " + tr("shared.actions.clearInput"), !has_text, false, L"", true});

  context_menu_->style_ = st;
  context_menu_->on_select = [this](int idx) {
    switch (idx) {
      case 0:  // cut
        if (!payload_.empty()) {
          copy_payload_to_clipboard();
          payload_.clear();
          all_selected_ = false;
          invalidate();
        }
        break;
      case 1:  // copy
        copy_payload_to_clipboard();
        break;
      case 2: {  // paste
        std::string pasted = read_clipboard_utf8_normalized();
        if (!pasted.empty()) {
          if (all_selected_) {
            payload_ = pasted;
            all_selected_ = false;
          } else {
            payload_ += pasted;
          }
          invalidate();
        }
        break;
      }
      case 3:  // selectAll
        if (!payload_.empty()) {
          all_selected_ = true;
          invalidate();
        }
        break;
      case 5:  // clear
        payload_.clear();
        all_selected_ = false;
        invalidate();
        break;
      default:
        break;
    }
    input_focused_ = true;
  };
  PointF local = anchor;
  if (context_menu_->parent()) {
    local = context_menu_->parent()->to_local(anchor);
  }
  context_menu_->open(items, local);
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
  // src: popup-styles.ts:120-185 窄窗弹窗锁定左侧 271px
  const float cw = std::min(theme::kPopupPaneWidth, area.w);
  const float title_h = kTitlePy * 2 + kTitleLineH;      // 41.85
  const float input_h = kRows * kLineH + kInputPadY * 2;  // 125
  const float content_h = 12.0f + input_h + kContentPb;   // 149
  const auto& svc = theme::ThemeService::instance();
  const float kPrimaryBtnH =
      kPrimaryBtnContentH + svc.button_paint("primary", false).border_w * 2.0f;
  const float kSecondaryBtnH =
      kSecondaryBtnContentH + svc.button_paint("default", true).border_w * 2.0f;
  const float acts_h = kActsPy * 2 + kPrimaryBtnH + kActsGap + kSecondaryBtnH;
  const float ch = std::min(title_h + content_h + acts_h, std::max(140.0f, area.h - 8.0f));
  const float cx = 0.0f;  // src: popup-styles.ts:123 left: 0
  const float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  title_rect_ = {cx, cy, cw, title_h};
  input_rect_ = {cx + kContentPx, cy + title_h + 12.0f,
                 std::max(60.0f, cw - kContentPx * 2), input_h};

  // src: popup-styles.ts:230-245 getPopupActionsSx 全宽纵向堆叠（添加在上，取消在下）
  const float btn_w = std::max(60.0f, cw - kActsPx * 2);
  const float acts_top = cy + ch - acts_h;
  btn_add_rect_ = {cx + kActsPx, acts_top + kActsPy, btn_w, kPrimaryBtnH};
  btn_cancel_rect_ = {cx + kActsPx, btn_add_rect_.bottom() + kActsGap, btn_w, kSecondaryBtnH};
}

void AddUrlDialog::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();  // 自续帧，直到过渡结束
  set_opacity(fade_.value());        // 每帧同步整棵子树的不透明度
  if (fade_.hidden()) return;
  const auto& svc = theme::ThemeService::instance();
  const auto tokens = svc.popup_tokens();

  // 1. backdrop（src: popup-styles.ts:120-168 左侧 271px 窄窗区域）
  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  rt.fill_rect({0, 0, pane_w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  // 2. paper：统一走 popup_card_paint()
  draw_card(rt, card_rect_, svc.popup_card_paint());

  // 3. 标题 + 底部分隔线（src: popup-styles.ts:191-212）
  TextStyle title_ts;
  title_ts.size = kTitleFs;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = tokens.primary_text;
  title_ts.align_h = AlignH::Center;
  title_ts.align_v = AlignV::Center;
  rt.draw_text(tr("settings.mini.addUrl"),
               {title_rect_.x + kTitlePx, title_rect_.y, title_rect_.w - kTitlePx * 2,
                title_rect_.h},
               title_ts);
  rt.fill_rect({card_rect_.x, title_rect_.bottom() - 1.0f, card_rect_.w, 1.0f},
               Brush{tokens.divider_color});

  // 4. TextField multiline（get3DInputStyle）
  const theme::InputPaint ip = svc.input_paint();
  Brush input_bg{ip.bg};
  Pen input_pen{input_focused_ ? ip.border_focused : ip.border,
                input_focused_ ? ip.border_w_focused : ip.border_w};
  rt.round_rect(input_rect_, ip.radius, input_pen, &input_bg);
  {
    TextStyle ts;
    ts.size = 13.0f;  // src: get3DInputStyle fontSize 13px
    ts.color = ip.text;
    ts.wrap = true;
    // ★ V6：行盒 23px（InputBase lineHeight 1.4375em @16）。框内 4 行必须与
    //   P16 的外框几何（4×23+16.5×2=125）同源，否则文字比框紧 5.7px/行。
    ts.line_h = kLineH;
    ts.max_lines = static_cast<int>(kRows);
    std::wstring body = utf::utf8_to_utf16(payload_);
    if (body.empty()) {
      TextStyle ph = ts;
      ph.color.a *= 0.5f;
      rt.draw_text(tr("settings.mini.addUrlPlaceholder"),
                   {input_rect_.x + kInputPadX, input_rect_.y + kInputPadY,
                    input_rect_.w - kInputPadX * 2,
                    input_rect_.h - kInputPadY * 2},
                   ph);
      if (input_focused_) {
        rt.draw_text(L"|", {input_rect_.x + kInputPadX, input_rect_.y + kInputPadY,
                            input_rect_.w - kInputPadX * 2,
                            input_rect_.h - kInputPadY * 2}, ts);
      }
    } else {
      if (all_selected_) {
        Color4f sel_bg = svc.primary();
        sel_bg.a = 0.25f;
        rt.fill_rect({input_rect_.x + kInputPadX, input_rect_.y + kInputPadY,
                      input_rect_.w - kInputPadX * 2,
                      input_rect_.h - kInputPadY * 2},
                     Brush{sel_bg});
      } else if (input_focused_) {
        body += L"|";
      }
      rt.draw_text(body,
                   {input_rect_.x + kInputPadX, input_rect_.y + kInputPadY,
                    input_rect_.w - kInputPadX * 2,
                    input_rect_.h - kInputPadY * 2},
                   ts);
    }
  }

  // 动作区顶部分隔线（src: popup-styles.ts:243）
  rt.fill_rect({card_rect_.x, btn_add_rect_.y - kActsPy, card_rect_.w, 1.0f},
               Brush{tokens.divider_color});

  // 5. 动作按钮：添加 contained primary（上）、取消 outlined default（下）
  auto paint_btn = [&](const RectF& r, const char* ctype, bool outlined,
                       bool hover, const std::wstring& label) {
    const auto bp = svc.button_paint(ctype, outlined);
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
  paint_btn(btn_add_rect_, "primary", /*outlined=*/false, hover_add_,
            tr("settings.mini.addUrlButton"));
  paint_btn(btn_cancel_rect_, "default", /*outlined=*/true, hover_cancel_,
            tr("shared.actions.cancel"));

  // 6. 错误提示（showNotice.error → 对话框内红字，源码为 toast；此处保留内联提示）
  if (!error_message_.empty()) {
    TextStyle err_ts;
    err_ts.size = 11.0f;
    err_ts.color = tokens.error_color;
    err_ts.align_v = AlignV::Center;
    rt.draw_text(utf::utf8_to_utf16(error_message_),
                 {input_rect_.x, input_rect_.bottom() + 1.0f, input_rect_.w, 14.0f},
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

  if (e.type == MouseEventType::Down) {
    if ((e.buttons & kMouseRight) && input_rect_.contains(pt.x, pt.y)) {
      input_focused_ = true;
      open_context_menu(to_global(pt));
      invalidate();
      return true;
    }
    return true;  // 拦截下层点击
  }

  if (e.type == MouseEventType::Up) {
    if (e.buttons & kMouseRight) {
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
      all_selected_ = false;
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

  return true;
}

bool AddUrlDialog::on_key(const KeyEvent& e) {
  if (!open_) return false;

  if (e.type == KeyEventType::Down) {
    if (e.key == VK_ESCAPE || e.key == 27) {
      close();
      return true;
    }
    // src: :230-233 multiline → Enter 插入换行（不提交）
    if ((e.key == VK_RETURN || e.key == 13) && input_focused_) {
      if (all_selected_) {
        payload_.clear();
        all_selected_ = false;
      }
      payload_.push_back('\n');
      invalidate();
      return true;
    }
    if (e.key == VK_BACK && input_focused_) {
      if (all_selected_) {
        payload_.clear();
        all_selected_ = false;
        invalidate();
      } else if (!payload_.empty()) {
        payload_.pop_back();
        invalidate();
      }
      return true;
    }
    if (input_focused_ && e.ctrl && (e.key == 'A' || e.key == 'a')) {
      all_selected_ = !payload_.empty();
      invalidate();
      return true;
    }
    if (input_focused_ && e.ctrl && (e.key == 'C' || e.key == 'c')) {
      copy_payload_to_clipboard();
      return true;
    }
    if (input_focused_ && e.ctrl && (e.key == 'X' || e.key == 'x')) {
      if (!payload_.empty()) {
        copy_payload_to_clipboard();
        payload_.clear();
        all_selected_ = false;
        invalidate();
      }
      return true;
    }
    // src: add-url-dialog.tsx:274-311 支持 Ctrl+V / Shift+Insert 多行粘贴
    if (input_focused_ &&
        ((e.ctrl && (e.key == 'V' || e.key == 'v')) ||
         (e.shift && e.key == VK_INSERT))) {
      std::string pasted = read_clipboard_utf8_normalized();
      if (!pasted.empty()) {
        if (all_selected_) {
          payload_ = pasted;
          all_selected_ = false;
        } else {
          payload_ += pasted;
        }
        invalidate();
      }
      return true;
    }
  } else if (e.type == KeyEventType::Char && input_focused_) {
    if (e.ctrl) return false;
    if (e.ch >= 32 && e.ch < 127) {
      if (all_selected_) {
        payload_.clear();
        all_selected_ = false;
      }
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
