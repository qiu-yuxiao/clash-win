#include "utility_modals.h"

#include <windows.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

#include "ui/button_draw.h"
#include "ui/card_draw.h"
#include "ui/icon_w.h"  // G9: 日志工具钮 18px MUI 图标
#include "ui/soft_shadow.h"
#include "ui/theme/theme.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01
#include "events/notice.h"

namespace clashwin::ui::scene {

using theme::ThemeService;

namespace {

// ---------- 弹窗族公共三段几何（← utils/popup-styles.ts）----------
// 上游所有窄窗弹窗都用同一组 DialogTitle / DialogContent / DialogActions sx：
//   getPopupTitleSx   :191-212  px 1.75(14) py 1.35(10.8) fontSize 15 bold lineHeight 1.35
//                                borderBottom 1px dividerColor
//   getPopupContentSx :215-228  px 1.75 py 1.5(12)（&.MuiDialogContent-root 再 pt 1.5）
//   getPopupActionsSx :231-246  px 1.75 py 1.35(10.8) gap 1(8) borderTop 1px
// 按钮高不由 MUI 的 size 决定，而由 lineHeight 1.75 与 py 覆写算出：
//   高 = fontSize × 1.75 + py × 2 + 皮肤边框宽 × 2（border-box，P16 已按此推导）
constexpr float kPopupPadX = 14.0f;         // ← popup-styles.ts:198 px: 1.75
constexpr float kPopupTitlePadY = 10.8f;    // ← popup-styles.ts:199 py: 1.35
constexpr float kPopupContentPadY = 12.0f;  // ← popup-styles.ts:220 py: 1.5
constexpr float kPopupActionsPadY = 10.8f;  // ← popup-styles.ts:240 py: 1.35
constexpr float kPopupActionsGap = 8.0f;    // ← popup-styles.ts:241 gap: 1
constexpr float kPopupBtnLineH = 1.75f;     // ← MUI Button 根 lineHeight 1.75
constexpr float kPopupLabelLineH = 1.4f;    // ← use-custom-theme.ts:220 body2 lineHeight 1.4
// src: node_modules/@mui/material/InputBase/InputBase.js:76-78 MUI TextField size="small" 高 40 = 8.5×2 + 23 行盒
constexpr float kPopupFieldH = 40.0f;
// 标题带：弹窗内层 Box 把 fontSize 覆写成 15.5、lineHeight 1.35
//   （← layout-dialogs.tsx:152-153 / :286-287，两处一致）
constexpr float kPopupTitleTextH = 15.5f * 1.35f;
// 标题带总高 = py×2 + 文字行盒 + 1px 底边线（borderBottom 计入布局）
constexpr float kPopupTitleBlockH =
    kPopupTitlePadY * 2.0f + kPopupTitleTextH + 1.0f;
// body2 行盒：Mini 主题 body2 = 0.75rem × lineHeight 1.4（← use-custom-theme.ts:219-221）
constexpr float kPopupBody2H = 12.5f * kPopupLabelLineH;
// 更新日志框：p 1.25(10) + fontSize 12 / lineHeight 1.5 / maxHeight 160
//   （← layout-dialogs.tsx:341-355）
constexpr float kChgPad = 10.0f;
constexpr float kChgLineH = 12.0f * 1.5f;
constexpr float kChgMaxH = 160.0f;

// 弹窗按钮高：fontSize × 1.75 + py × 2 + 皮肤边框宽 × 2
float popup_btn_h(const char* ctype, float font_size, float pad_y) {
  return font_size * kPopupBtnLineH + pad_y * 2.0f +
         ThemeService::instance().button_paint(ctype).border_w * 2.0f;
}

std::string get_current_log_time() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  // src: src/components/log/log-item.tsx:45 dayjs MM-DD HH:mm:ss
  oss << std::put_time(&tm, "%m-%d %H:%M:%S");
  return oss.str();
}

// ← style-helpers.tsx:221-225 formatCoreVersion：去前缀 v/V 后加 "Ver."
std::string format_core_version(const std::string& version) {
  if (version.empty()) return "";
  size_t i = 0;
  while (i < version.size() && (version[i] == 'v' || version[i] == 'V')) ++i;
  return "Ver." + version.substr(i);
}

// src: src/pages/_layout/components/layout-dialogs.tsx:303 客户端版本前缀 v
std::string ensure_v_prefix(const std::string& version) {
  if (version.empty()) return "";
  if (version[0] == 'v' || version[0] == 'V') return version;
  return "v" + version;
}

std::string strip_ver_prefix(std::string ver) {
  auto trim_ws = [](std::string& s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
  };
  trim_ws(ver);
  if (ver.rfind("Ver.", 0) == 0 || ver.rfind("ver.", 0) == 0) {
    ver.erase(0, 4);  // 豁免: 字符串前缀长度 "Ver."（出处: 无）
    trim_ws(ver);
  }
  while (!ver.empty() && (ver[0] == 'v' || ver[0] == 'V')) {
    ver.erase(0, 1);
  }
  trim_ws(ver);
  return ver;
}

bool versions_equal(const std::string& a, const std::string& b) {
  const std::string sa = strip_ver_prefix(a);
  const std::string sb = strip_ver_prefix(b);
  return !sa.empty() && !sb.empty() && sa == sb;
}

std::wstring to_lower_w(const std::wstring& s) {
  std::wstring out = s;
  for (auto& ch : out) ch = static_cast<wchar_t>(::towlower(ch));
  return out;
}

bool icontains_ascii_or_utf8(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return true;
  const std::wstring wh = to_lower_w(utf::utf8_to_utf16(hay));
  const std::wstring wn = to_lower_w(utf::utf8_to_utf16(needle));
  return wh.find(wn) != std::wstring::npos;
}

void apply_menu_style_to_list(MenuList* ml, const MenuStyle& st) {
  if (!ml) return;
  ml->item_height = st.item_h;
  ml->sep_height_ = st.sep_h;
  ml->min_width_ = st.min_w;
  ml->radius_ = st.radius;
  ml->font_size_ = st.font_size;
  ml->font_weight_ = st.font_weight;
  ml->gold_hover_ = st.gold_hover;
  ml->gradient_panel_ = st.gradient_panel;
  ml->pad_top_ = st.pad_v;
  ml->pad_bottom_ = st.pad_v;
  ml->padding_h = st.padding_h;
  ml->hover_inset_ = st.hover_inset;
  ml->bg_ = st.bg;
  ml->border_ = st.border;
  ml->text_ = st.text;
  ml->disabled_text_ = st.disabled_text;
  ml->danger_text_ = st.danger_text;
  ml->selected_bg_ = st.selected_bg;
  ml->selected_text_ = st.selected_text;
  ml->hover_grad_ = st.hover_grad;
  ml->hover_n_ = st.hover_n;
  ml->hover_angle_ = st.hover_angle;
  ml->hover_bg_ = st.hover_bg;
  ml->hover_radius_ = st.hover_radius;
  ml->hover_text_ = st.hover_text;
  ml->hover_border_ = st.hover_border;
  ml->hover_border_w_ = st.hover_border_w;
  ml->hover_glow_ = st.hover_glow;
  ml->hover_glow_color_ = st.hover_glow_color;
  ml->hover_glow_blur_ = st.hover_glow_blur;
  ml->panel_grad_on_ = st.panel_grad_on;
  ml->panel_n_ = st.panel_n;
  ml->panel_angle_ = st.panel_angle;
  ml->panel_bg_ = st.panel_bg;
  ml->panel_border_ = st.panel_border;
  ml->panel_border_w_ = st.panel_border_w;
  for (int i = 0; i < 4; ++i) {  // src: src/utils/button-styles.ts:44 四停渐变数组长度
    ml->hover_stops_[i] = st.hover_stops[i];
    ml->panel_stops_[i] = st.panel_stops[i];
  }
}

void draw_text_with_highlights(IRenderTarget& rt, ITextEngine* eng,
                               const std::wstring& text, const std::wstring& query_w,
                               const RectF& box, const TextStyle& base_ts) {
  if (query_w.empty() || !eng || text.empty()) {
    rt.draw_text(text, box, base_ts);
    return;
  }
  const std::wstring q_low = to_lower_w(query_w);
  const std::wstring t_low = to_lower_w(text);
  if (t_low.find(q_low) == std::wstring::npos) {
    rt.draw_text(text, box, base_ts);
    return;
  }

  // 按视觉行拆分（单行/多行统一高亮渲染），对齐 logs highlightText
  // src: src/components/log/log-item.tsx:48-70 #ffeb3b 高亮块
  const Brush hl_bg{Color4f::hex(0xFFEB3B, 0.7f)};  // src: src/components/log/log-item.tsx:58
  constexpr float kRowStep = 16.0f;  // src: src/components/log/log-item.tsx:11 lineHeight 16px
  std::vector<std::wstring> lines;
  if (!base_ts.wrap) {
    lines.push_back(text);
  } else {
    size_t pos = 0;
    const size_t n = text.size();
    while (pos < n) {
      size_t end = pos + 1;
      while (end < n) {
        if (text[end] == L'\n') break;
        const float w = eng->measure_text(text.substr(pos, end - pos + 1), base_ts, 0).w;
        if (w > box.w && end > pos) break;
        ++end;
      }
      lines.push_back(text.substr(pos, end - pos));
      pos = (end < n && text[end] == L'\n') ? (end + 1) : end;
    }
    if (lines.empty()) lines.push_back(text);
  }

  TextStyle row_ts = base_ts;
  row_ts.wrap = false;
  row_ts.ellipsis = false;
  TextStyle hl_ts = row_ts;
  hl_ts.color = Color4f::hex(0x000000);  // src: src/components/log/log-item.tsx:59

  const float row_h = base_ts.wrap ? kRowStep : box.h;
  for (size_t li = 0; li < lines.size(); ++li) {
    const float ry = box.y + static_cast<float>(li) * row_h;
    if (ry > box.bottom()) break;
    const RectF row_rc{box.x, ry, box.w, row_h};
    rt.draw_text(lines[li], row_rc, row_ts);

    const std::wstring row_low = to_lower_w(lines[li]);
    size_t search_at = 0;
    while (true) {
      const size_t hit = row_low.find(q_low, search_at);
      if (hit == std::wstring::npos) break;
      const float x0 = hit > 0 ? eng->measure_text(lines[li].substr(0, hit), row_ts, 0).w : 0.0f;
      const std::wstring match_s = lines[li].substr(hit, q_low.size());
      const float mw = eng->measure_text(match_s, row_ts, 0).w;
      // src: src/components/log/log-item.tsx:58 borderRadius 2px
      const RectF hl_rc{box.x + x0, ry, mw, row_h};
      rt.round_rect(hl_rc, 2.0f, Pen{Color4f(0, 0, 0, 0), 0.0f}, &hl_bg);
      rt.draw_text(match_s, hl_rc, hl_ts);
      search_at = hit + q_low.size();
    }
  }
}

}  // namespace

// ==========================================
// 1. EditProfileModal
// ==========================================
EditProfileModal::EditProfileModal(Widget* parent) : Widget(parent) {
  set_z_order(960);  // 豁免: Mini 无对应物 — 抽屉模态层序（出处: src/pages/_layout/components/layout-dialogs.tsx:119）

  auto make_field = [&](int idx) -> TextField* {
    auto tf_up = std::make_unique<TextField>(this);
    TextField* tf = tf_up.get();
    // src: node_modules/@mui/material/InputBase/InputBase.js:76-78 MUI size=small 高 40px
    tf->height_ = kPopupFieldH;
    // src: src/pages/_layout/components/layout-dialogs.tsx:164-216 fontSize 14px, px 10
    tf->set_padding_x(10.0f);
    tf->style_.size = 14.0f;
    tf->apply_theme();
    tf->set_visible(false);
    tf->on_submit = [this] { trigger_save(); };
    tf->on_key_intercept = [this](const KeyEvent& e) {
      if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
        close();
        return true;
      }
      if (e.type == KeyEventType::Down && (e.key == VK_RETURN || e.key == 13)) {
        trigger_save();
        return true;
      }
      return false;
    };
    tf->on_context_menu = [this, tf](PointF anchor) { open_field_menu(tf, anchor); };
    tf->on_text_changed = [this, tf, idx](const std::wstring& w) {
      if (syncing_fields_) return;
      focused_field_ = idx;
      if (idx == 1) {
        name_ = utf::utf16_to_utf8(w);
      } else if (idx == 2) {
        url_ = utf::utf16_to_utf8(w);
      } else if (idx == 3) {
        std::wstring digits;
        digits.reserve(w.size());
        for (wchar_t ch : w) {
          if (ch >= L'0' && ch <= L'9') digits.push_back(ch);
        }
        int val = digits.empty() ? 0 : _wtoi(digits.c_str());
        // src: src/pages/_layout/components/layout-dialogs.tsx:207-216 更新间隔上限
        if (val > 100000) val = 100000;
        interval_mins_ = val;
        if (digits != w) {
          syncing_fields_ = true;
          tf->set_text(digits);
          syncing_fields_ = false;
        }
      }
      invalidate();
    };
    add_child(std::move(tf_up));
    return tf;
  };

  name_field_ = make_field(1);
  url_field_ = make_field(2);
  interval_field_ = make_field(3);

  auto fm = std::make_unique<ContextMenu>(this);
  field_menu_ = fm.get();
  field_menu_->set_z_order(1020);  // 豁免: Mini 无对应物 — 弹窗内右键菜单叠在输入框之上（出处: src/utils/popup-styles.ts）
  add_child(std::move(fm));
}

void EditProfileModal::set_name(const std::string& n) {
  name_ = n;
  if (name_field_) {
    syncing_fields_ = true;
    name_field_->set_text(utf::utf8_to_utf16(n));
    syncing_fields_ = false;
  }
  invalidate();
}

void EditProfileModal::set_url(const std::string& u) {
  url_ = u;
  if (url_field_) {
    syncing_fields_ = true;
    url_field_->set_text(utf::utf8_to_utf16(u));
    syncing_fields_ = false;
  }
  invalidate();
}

void EditProfileModal::set_interval_mins(int mins) {
  interval_mins_ = mins;
  if (interval_field_) {
    syncing_fields_ = true;
    interval_field_->set_text(std::to_wstring(mins));
    syncing_fields_ = false;
  }
  invalidate();
}

void EditProfileModal::trigger_save() {
  if (on_save) on_save(name_, url_, interval_mins_);
  close();
}

void EditProfileModal::open_field_menu(TextField* tf, PointF anchor) {
  if (!field_menu_ || !tf || !tf->enabled()) return;
  MenuStyle st;
  apply_menu_skin(st);
  // src: src/utils/popup-styles.ts / src/utils/style-helpers.tsx:126 菜单行高 32
  st.item_h = 32.0f;
  st.min_w = 160.0f;
  st.font_size = 13.0f;
  st.padding_h = 14.0f;

  std::vector<MenuItem> items;
  items.push_back({tr("shared.actions.cut"), !tf->can_cut(), false, L"", false});
  items.push_back({tr("shared.actions.copy"), !tf->can_copy(), false, L"", false});
  items.push_back({tr("shared.actions.paste"), !tf->can_paste(), false, L"", false});
  items.push_back({tr("shared.actions.selectAll"), !tf->can_select_all(), false, L"", false});
  items.push_back({L"", false, true, L"", false});
  items.push_back({tr("shared.actions.clearInput"), !tf->can_clear(), false, L"", true});

  field_menu_->style_ = st;
  field_menu_->on_select = [tf](int idx) {
    switch (idx) {
      case 0: tf->cut(); break;
      case 1: tf->copy(); break;
      case 2: tf->paste(); break;
      case 3: tf->select_all(); break;
      case 5: tf->clear_text(); break;
      default: break;
    }
  };
  PointF local = anchor;
  if (field_menu_->parent()) {
    local = field_menu_->parent()->to_local(anchor);
  }
  field_menu_->open(items, local);
}

void EditProfileModal::open(const std::string& name, const std::string& url, int interval,
                            bool is_local) {
  open_ = true;
  fade_.show();
  name_ = name;
  url_ = url;
  interval_mins_ = interval;  // src: _layout.tsx:973 直接取 option.update_interval（0=禁用）
  is_local_ = is_local;
  hover_cancel_ = false;
  hover_save_ = false;
  focused_field_ = 0;  // src: layout-dialogs.tsx:163-217 各 TextField 无 autoFocus

  syncing_fields_ = true;
  if (name_field_) {
    name_field_->apply_theme();
    name_field_->style_.size = 14.0f;  // src: layout-dialogs.tsx:164 fontSize 14px
    name_field_->set_text(utf::utf8_to_utf16(name_));
    name_field_->set_enabled(true);
    name_field_->set_opacity(1.0f);
    name_field_->set_visible(true);
  }
  // src: node_modules/@mui/material/styles opacity 0.38 disabled
  constexpr float kDisabledOpacity = 0.38f;
  if (url_field_) {
    url_field_->apply_theme();
    url_field_->style_.size = 14.0f;  // src: layout-dialogs.tsx:184 fontSize 14px
    url_field_->set_text(utf::utf8_to_utf16(url_));
    url_field_->set_enabled(!is_local_);
    url_field_->set_opacity(is_local_ ? kDisabledOpacity : 1.0f);
    url_field_->set_visible(true);
  }
  if (interval_field_) {
    interval_field_->apply_theme();
    interval_field_->style_.size = 14.0f;  // src: layout-dialogs.tsx:207 fontSize 14px
    interval_field_->set_text(std::to_wstring(interval_mins_));
    interval_field_->set_enabled(!is_local_);
    interval_field_->set_opacity(is_local_ ? kDisabledOpacity : 1.0f);
    interval_field_->set_visible(true);
  }
  syncing_fields_ = false;

  if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
  invalidate();
}

void EditProfileModal::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();  // 画面延后 195ms 消失，交互立刻失效
  if (field_menu_ && field_menu_->is_open()) field_menu_->close();
  if (name_field_) name_field_->set_visible(false);
  if (url_field_) url_field_->set_visible(false);
  if (interval_field_) interval_field_->set_visible(false);
  invalidate();
  if (on_dismiss) on_dismiss();
}

void EditProfileModal::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: popup-styles.ts:120-185 窄窗弹窗锁定左侧 271px
  const float cw = std::min(theme::kPopupPaneWidth, area.w);
  const float cx = 0.0f;  // src: popup-styles.ts:123 left: 0

  // ---- 三段高（← layout-dialogs.tsx:144-263）----
  // 标题：DialogTitle py 10.8 + 内层 Box fontSize 15.5 / lineHeight 1.35 + 1px 底边线
  constexpr float kTitleTextH = 15.5f * 1.35f;  // ← layout-dialogs.tsx:152-153
  const float title_h = kPopupTitlePadY * 2.0f + kTitleTextH + 1.0f;
  // 内容：三组「label(body2 12.5×1.4 + mb 4) + TextField 40」，组间 gap 1.5(12)
  constexpr float kLabelH = 12.5f * kPopupLabelLineH + 4.0f;  // ← layout-dialogs.tsx:168-170
  const float group_h = kLabelH + kPopupFieldH;
  const float content_h = kPopupContentPadY * 2.0f + 3.0f * group_h + 2.0f * 12.0f;
  // 动作：Save(13px, py .85) 在上、Cancel(12.5px, py .7) 在下，gap 1(8)
  const float save_h = popup_btn_h("primary", 13.0f, 6.8f);
  const float cancel_h = popup_btn_h("default", 12.5f, 5.6f);
  const float actions_h =
      kPopupActionsPadY * 2.0f + save_h + kPopupActionsGap + cancel_h;

  // 上游 paper 无固定高：内容撑开，上限 maxHeight = 100vh - topOffset - 8
  const float need = title_h + content_h + actions_h;
  const float ch = std::min(need, std::max(200.0f, area.h - 8.0f));
  const float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  const float inner_w = std::max(80.0f, cw - kPopupPadX * 2.0f);
  const float content_y = cy + title_h + kPopupContentPadY;
  name_input_rect_ = {cx + kPopupPadX, content_y + kLabelH, inner_w, kPopupFieldH};
  url_input_rect_ = {cx + kPopupPadX, content_y + group_h + 12.0f + kLabelH,
                     inner_w, kPopupFieldH};
  interval_input_rect_ = {cx + kPopupPadX, content_y + 2.0f * (group_h + 12.0f) + kLabelH,
                          inner_w, kPopupFieldH};

  if (name_field_) name_field_->layout_tree(name_input_rect_);
  if (url_field_) url_field_->layout_tree(url_input_rect_);
  if (interval_field_) interval_field_->layout_tree(interval_input_rect_);
  if (field_menu_) field_menu_->layout_tree({0.0f, 0.0f, area.w, area.h});

  // src: popup-styles.ts:231-246 全宽纵向堆叠按钮（Save 在上 / Cancel 在下）
  const float actions_y = cy + ch - actions_h;
  btn_save_rect_ = {cx + kPopupPadX, actions_y + kPopupActionsPadY, inner_w, save_h};
  btn_cancel_rect_ = {cx + kPopupPadX, btn_save_rect_.bottom() + kPopupActionsGap,
                      inner_w, cancel_h};
}

void EditProfileModal::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();  // 自续帧，直到过渡结束
  set_opacity(fade_.value());        // 每帧同步整棵子树的不透明度
  if (fade_.hidden()) return;

  const auto& svc = ThemeService::instance();
  const auto tokens = svc.popup_tokens();

  // 1. MUI 缺省遮罩 rgba(0,0,0,.5)（锁定 271px 窄窗宽度）
  // src: src/utils/popup-styles.ts:120
  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  rt.fill_rect({0, 0, pane_w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  // 2. 卡片底座（popup_card_paint：6 皮肤查表 + frosted-glass 实底防穿透）
  ::clashwin::ui::draw_card(rt, card_rect_, svc.popup_card_paint());

  // 3. 标题 + 分隔线（src: popup-styles.ts:191-212 + layout-dialogs.tsx:144-159）
  const float pad = kPopupPadX;
  const float inner_w = std::max(80.0f, card_rect_.w - pad * 2.0f);
  constexpr float kTitleTextH = 15.5f * 1.35f;  // ← layout-dialogs.tsx:152-153
  constexpr float kLabelGapH = 12.5f * kPopupLabelLineH + 4.0f;  // label 行高 + mb 0.5
  TextStyle title_ts;
  title_ts.size = 15.5f;  // ← layout-dialogs.tsx:152 内层 Box fontSize '15.5px'
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = tokens.primary_text;
  title_ts.align_h = AlignH::Center;
  title_ts.align_v = AlignV::Center;
  rt.draw_text(L"⚙️ " + tr("layout.components.dialogs.editProfile.title"),
               {card_rect_.x + pad, card_rect_.y + kPopupTitlePadY, inner_w, kTitleTextH},
               title_ts);
  rt.fill_rect({card_rect_.x, card_rect_.y + kPopupTitlePadY * 2.0f + kTitleTextH,
                card_rect_.w, 1.0f},
               Brush{tokens.divider_color});

  // 4. 字段 label（body2 12.5 bold，mb 0.5 落在 label 盒之外）
  TextStyle label_ts;
  label_ts.size = 12.5f;
  label_ts.weight = static_cast<int>(Weight::Bold);
  label_ts.color = tokens.primary_text;
  label_ts.align_v = AlignV::Center;
  rt.draw_text(tr("layout.components.dialogs.editProfile.nameLabel"),
               {card_rect_.x + pad, name_input_rect_.y - kLabelGapH, inner_w,
                kLabelGapH - 4.0f},
               label_ts);
  rt.draw_text(tr("layout.components.dialogs.editProfile.urlLabel"),
               {card_rect_.x + pad, url_input_rect_.y - kLabelGapH, inner_w,
                kLabelGapH - 4.0f},
               label_ts);
  rt.draw_text(tr("layout.components.dialogs.editProfile.intervalLabel"),
               {card_rect_.x + pad, interval_input_rect_.y - kLabelGapH, inner_w,
                kLabelGapH - 4.0f},
               label_ts);

  // 5. 子控件 TextField 热刷新皮肤主题（src: layout-dialogs.tsx:164-216）
  if (name_field_) {
    name_field_->apply_theme();
    name_field_->style_.size = 14.0f;  // src: layout-dialogs.tsx:164
  }
  if (url_field_) {
    url_field_->apply_theme();
    url_field_->style_.size = 14.0f;  // src: layout-dialogs.tsx:184
  }
  if (interval_field_) {
    interval_field_->apply_theme();
    interval_field_->style_.size = 14.0f;  // src: layout-dialogs.tsx:207
  }

  // 6. 按钮（Save 13px / py .85，Cancel 12.5px / py .7；全宽，Save 在上）
  // src: layout-dialogs.tsx:240-263 取消 defaultBtn3DStyle、保存 primaryBtn3DStyle，
  //   两者都是 get3DButtonStyle(theme,'contained', ...) → 6 套皮肤查表
  TextStyle btn_ts;
  btn_ts.align_h = AlignH::Center;
  btn_ts.align_v = AlignV::Center;

  auto paint_btn = [&](const RectF& r, const char* ctype, bool hover,
                       const std::wstring& label, float fs, bool outlined) {
    const auto bp = ThemeService::instance().button_paint(ctype, outlined);
    // 四段视觉统一由查表给出
    draw_button_skin(rt, r, bp, hover ? 1.0f : 0.0f, /*pressed=*/false);
    btn_ts.size = fs;
    btn_ts.weight = bp.weight;
    btn_ts.color = bp.text;
    rt.draw_text(label, r, btn_ts);
  };
  // src: layout-dialogs.tsx:252-259 Cancel 是 defaultBtn3DStyle（contained default）
  paint_btn(btn_cancel_rect_, "default", hover_cancel_,
            tr("shared.actions.cancel"), 12.5f, /*outlined=*/false);
  paint_btn(btn_save_rect_, "primary", hover_save_,
            tr("shared.actions.save"), 13.0f, /*outlined=*/false);
}

bool EditProfileModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hc = btn_cancel_rect_.contains(pt.x, pt.y);
    bool hs = btn_save_rect_.contains(pt.x, pt.y);
    if (hc != hover_cancel_ || hs != hover_save_) {
      hover_cancel_ = hc;
      hover_save_ = hs;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_cancel_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (btn_save_rect_.contains(pt.x, pt.y)) {
      trigger_save();
      return true;
    }
    if (name_input_rect_.contains(pt.x, pt.y)) {
      focused_field_ = 1;
      if (name_field_) name_field_->request_focus();
      invalidate();
      return true;
    }
    if (!is_local_ && url_input_rect_.contains(pt.x, pt.y)) {
      focused_field_ = 2;
      if (url_field_) url_field_->request_focus();
      invalidate();
      return true;
    }
    if (!is_local_ && interval_input_rect_.contains(pt.x, pt.y)) {
      focused_field_ = 3;
      if (interval_field_) interval_field_->request_focus();
      invalidate();
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }

  return true;
}

bool EditProfileModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down) {
    if (e.key == VK_ESCAPE || e.key == 27) {
      close();
      return true;
    }
    if (e.key == VK_RETURN || e.key == 13) {
      trigger_save();
      return true;
    }
  }
  TextField* active_tf = nullptr;
  if (focused_field_ == 1) active_tf = name_field_;
  else if (focused_field_ == 2 && !is_local_) active_tf = url_field_;
  else if (focused_field_ == 3 && !is_local_) active_tf = interval_field_;
  if (active_tf) {
    return active_tf->on_key(e);
  }
  return false;
}

// ==========================================
// 2. ClientUpdateModal
// ==========================================
ClientUpdateModal::ClientUpdateModal(Widget* parent) : Widget(parent) {
  set_z_order(960);
  subscribe_events();
}

ClientUpdateModal::~ClientUpdateModal() {
  unsubscribe_events();
}

void ClientUpdateModal::subscribe_events() {
  listener_id_ = events::EventBus::instance().subscribe(
      "client-update-progress", [this](const events::Payload& payload) {
        if (!payload.is_object()) return;
        std::string st = payload.value("status", "downloading");
        int pr = payload.value("progress", 0);
        std::string msg = payload.value("message", "");
        this->handle_progress_event(st, pr, msg);
      });
}

void ClientUpdateModal::unsubscribe_events() {
  if (listener_id_ != 0) {
    events::EventBus::instance().unsubscribe(listener_id_);
    listener_id_ = 0;
  }
}

void ClientUpdateModal::handle_progress_event(const std::string& status, int progress,
                                              const std::string& msg) {
  status_ = status;
  progress_ = std::clamp(progress, 0, 100);
  if (!msg.empty()) status_msg_ = msg;
  if (status_ == "done") {
    is_downloading_ = false;
  } else if (status_ == "error") {
    is_downloading_ = false;
  } else {
    is_downloading_ = true;
  }
  invalidate();
}

void ClientUpdateModal::open(const std::string& current_ver, const std::string& latest_ver,
                             const std::string& changelog, const std::string& download_url) {
  open_ = true;
  fade_.show();
  current_version_ = current_ver;
  latest_version_ = latest_ver;
  changelog_ = changelog;
  download_url_ = download_url;
  changelog_scroll_y_ = 0.0f;
  changelog_text_h_ = 0.0f;
  progress_ = 0;
  status_ = "idle";
  is_downloading_ = false;
  status_msg_.clear();
  hover_close_x_ = false;
  hover_close_ = false;
  hover_update_ = false;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

void ClientUpdateModal::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();  // 画面延后 195ms 消失，交互立刻失效
  invalidate();
  if (on_dismiss) on_dismiss();
}

void ClientUpdateModal::set_progress(int p, const std::string& msg) {
  progress_ = std::clamp(p, 0, 100);
  if (!msg.empty()) status_msg_ = msg;
  invalidate();
}

void ClientUpdateModal::on_arrange(const RectF& area) {
  set_bounds(area);
  const auto& svc = ThemeService::instance();
  // src: popup-styles.ts:120-185 窄窗弹窗锁定左侧 271px
  const float cw = std::min(theme::kPopupPaneWidth, area.w);
  const float cx = 0.0f;
  const float inner_w = std::max(80.0f, cw - kPopupPadX * 2.0f);

  // ---- 三段高（← layout-dialogs.tsx:278-437）----
  const float update_h = popup_btn_h("primary", 13.0f, 6.8f);   // ← :410-414
  const float close_h = popup_btn_h("default", 12.5f, 5.6f);    // ← :429-433 outlined
  const float actions_h =
      kPopupActionsPadY * 2.0f + update_h + kPopupActionsGap + close_h;

  // 更新日志框高度：文字自然高 + p×2 + 1px 双边框，上限 160（overflowY auto）
  TextStyle chg_ts;
  chg_ts.size = 12.0f;
  chg_ts.wrap = true;
  chg_ts.line_h = kChgLineH;
  float text_h = kChgLineH;
  if (ITextEngine* e = text_engine()) {
    const std::wstring w =
        changelog_.empty()
            ? tr("layout.components.dialogs.client.changelogEmpty")
            : utf::utf8_to_utf16(changelog_);
    text_h = e->measure_text(w, chg_ts, inner_w - kChgPad * 2.0f).h;
  }
  changelog_text_h_ = text_h;
  float chg_box_h =
      std::min(kChgMaxH, text_h + kChgPad * 2.0f + 2.0f);  // ← :343 maxHeight '160px'

  // 内容带：版本两行（第一行 mb 0.5）→ Box mb 1.25 → 日志标题 mb 0.75 → 日志框
  //        → 可选进度区（mt 1.5 + 8px 条 + mt 0.75 + 说明文字）
  const float meta_h = kPopupBody2H + 4.0f + kPopupBody2H;  // ← :296-327
  const float extra_h =
      (status_ != "idle") ? 12.0f + 8.0f + 6.0f + kPopupBody2H : 0.0f;  // ← :361-395
  float need = kPopupTitleBlockH + kPopupContentPadY * 2.0f + meta_h + 10.0f +
               kPopupBody2H + 6.0f + chg_box_h + extra_h + actions_h;
  const float max_h = std::max(200.0f, area.h - 8.0f);
  if (need > max_h) {  // 超出可用高时先压日志框（上游是 overflowY auto）
    chg_box_h = std::max(56.0f, chg_box_h - (need - max_h));
    need = max_h;
  }
  const float ch = need;
  const float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};  // 保留成员（Mini 无 ✕，不绘制）

  // 内容带起点与各块纵坐标（on_paint 用同一串常量，避免两处漂移）
  const float content_y = cy + kPopupTitleBlockH + kPopupContentPadY;
  changelog_box_rect_ = {cx + kPopupPadX,
                         content_y + meta_h + 10.0f + kPopupBody2H + 6.0f,
                         inner_w, chg_box_h};
  const float max_chg_scroll = std::max(
      0.0f, changelog_text_h_ - std::max(0.0f, changelog_box_rect_.h - kChgPad * 2.0f));
  changelog_scroll_y_ = std::clamp(changelog_scroll_y_, 0.0f, max_chg_scroll);
  // ← layout-dialogs.tsx:363-381 bar 高 8px / radius 4 / mt 1.5
  progress_bar_rect_ = {cx + kPopupPadX, changelog_box_rect_.bottom() + 12.0f,
                        inner_w, 8.0f};

  // src: popup-styles.ts:231-246 全宽纵向堆叠按钮（更新在上，关闭在下）
  const float actions_y = cy + ch - actions_h;
  btn_update_rect_ = {cx + kPopupPadX, actions_y + kPopupActionsPadY, inner_w, update_h};
  btn_close_rect_ = {cx + kPopupPadX, btn_update_rect_.bottom() + kPopupActionsGap,
                     inner_w, close_h};
}

void ClientUpdateModal::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();  // 自续帧，直到过渡结束
  set_opacity(fade_.value());        // 每帧同步整棵子树的不透明度
  if (fade_.hidden()) return;

  const auto& svc = ThemeService::instance();
  const auto tokens = svc.popup_tokens();

  // src: popup-styles.ts:120-168 左侧 271px 窄窗 Backdrop
  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  rt.fill_rect({0, 0, pane_w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  // 统一弹窗卡片（popup_card_paint：6 套皮肤 + frosted-glass 实底防穿透）
  ::clashwin::ui::draw_card(rt, card_rect_, svc.popup_card_paint());

  const float pad = kPopupPadX;
  const float inner_w = std::max(80.0f, card_rect_.w - pad * 2.0f);
  // 标题 + 分隔线（src: popup-styles.ts:191-212 + layout-dialogs.tsx:278-293）
  TextStyle title_ts;
  title_ts.size = 15.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = tokens.primary_text;
  title_ts.align_h = AlignH::Center;
  title_ts.align_v = AlignV::Center;
  title_ts.ellipsis = true;
  // src: locales/zh/layout.json:76 「"title": "软件本体更新"」（layout-dialogs.tsx:290 🚀）
  rt.draw_text(L"🚀 " + tr("layout.components.dialogs.client.title"),
               {card_rect_.x + pad, card_rect_.y + kPopupTitlePadY, inner_w,
                kPopupTitleTextH},
               title_ts);
  rt.fill_rect({card_rect_.x, card_rect_.y + kPopupTitleBlockH - 1.0f, card_rect_.w,
                1.0f},
               Brush{tokens.divider_color});

  // 版本信息两行（body2 12.5 × 1.4；第一行 secondaryText，最新版本行 bold + primaryText ← layout-dialogs.tsx:296-327）
  const float content_y = card_rect_.y + kPopupTitleBlockH + kPopupContentPadY;
  const float meta_h = kPopupBody2H + 4.0f + kPopupBody2H;
  const std::wstring unknown = tr("layout.components.dialogs.update.unknown");
  const std::string cur_disp = current_version_.empty()
                                   ? utf::utf16_to_utf8(unknown)
                                   : ensure_v_prefix(current_version_);
  const std::string lat_disp = latest_version_.empty()
                                   ? utf::utf16_to_utf8(unknown)
                                   : ensure_v_prefix(latest_version_);
  TextStyle ver_ts;
  ver_ts.size = 12.5f;
  ver_ts.color = tokens.secondary_text;  // src: layout-dialogs.tsx:302
  ver_ts.align_v = AlignV::Center;
  ver_ts.ellipsis = true;
  rt.draw_text(tr("layout.components.dialogs.update.currentVersion",
                  {{"version", cur_disp}}),
               {card_rect_.x + pad, content_y, inner_w, kPopupBody2H}, ver_ts);
  TextStyle ver_bold = ver_ts;
  ver_bold.weight = static_cast<int>(Weight::Bold);
  ver_bold.color = tokens.primary_text;  // src: layout-dialogs.tsx:317
  rt.draw_text(tr("layout.components.dialogs.update.latestVersion",
                  {{"version", lat_disp}}),
               {card_rect_.x + pad, content_y + kPopupBody2H + 4.0f, inner_w,
                kPopupBody2H},
               ver_bold);

  TextStyle chg_label = ver_bold;
  // src: locales/zh/layout.json:77 「"更新日志:"」（layout-dialogs.tsx:329-340）
  rt.draw_text(tr("layout.components.dialogs.client.changelog"),
               {card_rect_.x + pad, content_y + meta_h + 10.0f, inner_w,
                kPopupBody2H},
               chg_label);

  // 更新日志框（fontSize 12 / lineHeight 1.5 / p 10 / radius 6 ← layout-dialogs.tsx:341-355）
  Brush cl_bg{tokens.subtle_bg};
  rt.round_rect(changelog_box_rect_, 6.0f, {tokens.subtle_border, 1.0f}, &cl_bg);

  TextStyle cl_ts;
  cl_ts.size = 12.0f;
  cl_ts.color = tokens.primary_text;
  cl_ts.wrap = true;
  cl_ts.line_h = kChgLineH;  // ← :348 lineHeight 1.5 × 12 = 18
  // src: locales/zh/layout.json:78 「"暂无详细更新日志"」（layout-dialogs.tsx:335）
  std::wstring chg_w = changelog_.empty() ? tr("layout.components.dialogs.client.changelogEmpty") : utf::utf8_to_utf16(changelog_);
  const RectF chg_inner{changelog_box_rect_.x + kChgPad,
                        changelog_box_rect_.y + kChgPad,
                        std::max(10.0f, changelog_box_rect_.w - kChgPad * 2.0f),
                        std::max(10.0f, changelog_box_rect_.h - kChgPad * 2.0f)};
  rt.push_clip(chg_inner);
  rt.draw_text(chg_w,
               {chg_inner.x, chg_inner.y - changelog_scroll_y_, chg_inner.w,
                std::max(changelog_text_h_, chg_inner.h)},
               cl_ts);
  rt.pop_clip();

  // 进度条与状态（仅 status ≠ idle 显示；← layout-dialogs.tsx:361-395）
  if (status_ != "idle") {
    // ← :367-368 bgcolor subtleSurfaceBg + 1px subtleSurfaceBorder
    Brush bar_bg{tokens.subtle_bg};
    rt.round_rect(progress_bar_rect_, 4.0f, {tokens.subtle_border, 1.0f}, &bar_bg);
    float fill_w = progress_bar_rect_.w * (static_cast<float>(progress_) / 100.0f);
    Brush fill_b{tokens.accent_primary};
    rt.round_rect({progress_bar_rect_.x, progress_bar_rect_.y, fill_w, progress_bar_rect_.h},
                  4.0f, {}, &fill_b);

    TextStyle stat_ts;
    stat_ts.size = 12.0f;
    stat_ts.color = tokens.secondary_text;
    std::wstring stat_str = status_msg_.empty()
                                ? tr("layout.components.dialogs.progress.clientDownloading")
                                : utf::utf8_to_utf16(status_msg_);
    // ← :385 mt: 0.75（6px）
    rt.draw_text(stat_str, {progress_bar_rect_.x, progress_bar_rect_.y + 8.0f + 6.0f,
                            progress_bar_rect_.w, kPopupBody2H},
                 stat_ts);
  }

  // 动作区分隔线（src: popup-styles.ts:243 borderTop 1px）
  rt.fill_rect({card_rect_.x, btn_update_rect_.y - kPopupActionsPadY, card_rect_.w,
                1.0f},
               Brush{tokens.divider_color});

  // 底部按钮（立即更新 contained primary 上、关闭 outlined 下 ← layout-dialogs.tsx:398-437）
  const bool busy = is_downloading_;
  const bool same_ver = versions_equal(latest_version_, current_version_);
  const bool upd_disabled = busy || status_ == "done" || same_ver;
  auto paint_themed_btn = [&](IRenderTarget& rt2, const RectF& r, bool hovered,
                              bool disabled, const char* color_type, bool outlined,
                              const std::wstring& label) {
    auto p = svc.button_paint(color_type, outlined);
    // 四段视觉统一由查表给出（此前禁用描边在这里被乘了个自创的 0.38 alpha）
    draw_button_skin(rt2, r, p, hovered ? 1.0f : 0.0f, /*pressed=*/false, disabled);
    TextStyle ts;
    ts.size = 12.5f;  // MUI Button body2
    ts.weight = p.weight;
    ts.color = disabled ? p.text_disabled : p.text;
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt2.draw_text(label, r, ts);
  };

  std::wstring upd_label;
  if (status_ == "done")
    upd_label = tr("layout.components.dialogs.client.prepareRestart");
  else if (busy)
    upd_label = tr("layout.components.dialogs.update.updating");
  else if (same_ver)
    upd_label = tr("layout.components.dialogs.update.upToDate");
  else
    upd_label = tr("layout.components.dialogs.update.updateNow");
  paint_themed_btn(rt, btn_update_rect_, hover_update_ && !upd_disabled, upd_disabled,
                   "primary", false, upd_label);
  paint_themed_btn(rt, btn_close_rect_, hover_close_ && !busy, busy, "default", true,
                   tr("shared.actions.close"));
}

bool ClientUpdateModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hc = btn_close_rect_.contains(pt.x, pt.y);
    bool hu = btn_update_rect_.contains(pt.x, pt.y);
    if (hc != hover_close_ || hu != hover_update_) {
      hover_close_ = hc;
      hover_update_ = hu;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Wheel) {
    if (changelog_box_rect_.contains(pt.x, pt.y)) {
      const float max_scroll = std::max(
          0.0f, changelog_text_h_ - std::max(0.0f, changelog_box_rect_.h - kChgPad * 2.0f));
      if (max_scroll > 0.0f) {
        // 豁免: Mini 无对应物 — Win32 滚轮 120 刻度映射 36px（出处: src/pages/_layout/components/layout-dialogs.tsx:343）
        const float delta = static_cast<float>(e.wheel_delta) / 120.0f * 36.0f;
        changelog_scroll_y_ = std::clamp(changelog_scroll_y_ - delta, 0.0f, max_scroll);
        invalidate();
      }
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_rect_.contains(pt.x, pt.y)) {
      // src: layout-dialogs.tsx:250-254 仅非 downloading 时 onClose 生效
      if (!is_downloading_) close();
      return true;
    }
    if (btn_update_rect_.contains(pt.x, pt.y)) {
      // src: use-client-update.ts:56-62 无更新对象/已是新版 → 不发起下载
      if (download_url_.empty() || is_downloading_ || status_ == "done" ||
          progress_ >= 100 || versions_equal(latest_version_, current_version_)) {
        return true;
      }
      // ← use-client-update.ts:63-67 点击后先显示 clientDownloading
      handle_progress_event("downloading", 0,
                            utf::utf16_to_utf8(
                                tr("layout.components.dialogs.progress.clientDownloading")));
      if (on_start_update) on_start_update();
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      // src: layout-dialogs.tsx:250-254 仅非 downloading 时 onClose 生效
      if (!is_downloading_) close();
      return true;
    }
    return true;
  }

  return true;
}

bool ClientUpdateModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
    if (!is_downloading_) close();
    return true;
  }
  return false;
}

// ==========================================
// 3. CoreUpdateModal
// ==========================================
CoreUpdateModal::CoreUpdateModal(Widget* parent) : Widget(parent) {
  set_z_order(960);
  subscribe_events();
}

CoreUpdateModal::~CoreUpdateModal() {
  unsubscribe_events();
}

void CoreUpdateModal::set_external_event_driver(bool ext) {
  external_event_driver_ = ext;
  if (ext) {
    unsubscribe_events();
  } else if (listener_id_ == 0) {
    subscribe_events();
  }
}

void CoreUpdateModal::open(const std::string& current_ver, const std::string& latest_ver) {
  open_ = true;
  fade_.show();
  current_version_ = current_ver;
  latest_version_ = latest_ver;
  progress_ = 0;
  status_ = "idle";
  is_upgrading_ = false;
  message_.clear();
  hover_close_x_ = false;
  hover_close_ = false;
  hover_upgrade_ = false;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

void CoreUpdateModal::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();  // 画面延后 195ms 消失，交互立刻失效
  invalidate();
  if (on_dismiss) on_dismiss();
}

void CoreUpdateModal::subscribe_events() {
  listener_id_ = events::EventBus::instance().subscribe(
      "core-upgrade-progress", [this](const events::Payload& payload) {
        if (!payload.is_object()) return;
        std::string st = payload.value("status", "checking");
        int pr = payload.value("progress", 0);
        std::string msg = payload.value("message", "");
        this->handle_progress_event(st, pr, msg);
      });
}

void CoreUpdateModal::unsubscribe_events() {
  if (listener_id_ != 0) {
    events::EventBus::instance().unsubscribe(listener_id_);
    listener_id_ = 0;
  }
}

void CoreUpdateModal::handle_progress_event(const std::string& status, int progress,
                                            const std::string& msg) {
  status_ = status;
  progress_ = std::clamp(progress, 0, 100);
  message_ = msg;
  if (status_ == "done") {
    is_upgrading_ = false;
    current_version_ = latest_version_;
    // ← use-core-update.ts:44-46 coreUpdateSuccess 通知
    events::NoticeService::instance().push("success", tr8("layout.notices.coreUpdateSuccess"));
  } else if (status_ == "error") {
    is_upgrading_ = false;
    // ← use-core-update.ts:47-51 coreUpdateFailed 通知
    events::NoticeService::instance().push(
        "error", tr8("layout.notices.coreUpdateFailed", {{"message", msg}}));
  }
  invalidate();
}

void CoreUpdateModal::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: popup-styles.ts:120-185 窄窗弹窗锁定左侧 271px
  const float cw = std::min(theme::kPopupPaneWidth, area.w);
  const float cx = 0.0f;
  const float inner_w = std::max(80.0f, cw - kPopupPadX * 2.0f);

  // ---- 三段高（← layout-dialogs.tsx:441-560）----
  const float upgrade_h = popup_btn_h("primary", 13.0f, 6.8f);
  const float close_h = popup_btn_h("default", 12.5f, 5.6f);
  const float actions_h =
      kPopupActionsPadY * 2.0f + upgrade_h + kPopupActionsGap + close_h;
  // 版本盒：p 1.25(10) + 两行 body2（第一行 mb 0.6 = 4.8）+ 1px 双边框（← :468-505）
  constexpr float kMetaBoxH = kChgPad * 2.0f + kPopupBody2H + 4.8f + kPopupBody2H + 2.0f;
  const float extra_h =
      (status_ != "idle") ? 12.0f + 8.0f + 6.0f + kPopupBody2H : 0.0f;  // ← :507-529
  const float content_h = kPopupContentPadY * 2.0f + kMetaBoxH + extra_h;
  const float need = kPopupTitleBlockH + content_h + actions_h;
  const float ch = std::min(need, std::max(200.0f, area.h - 8.0f));
  const float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};  // 保留成员（Mini 无 ✕，不绘制）

  const float content_y = cy + kPopupTitleBlockH + kPopupContentPadY;
  meta_box_rect_ = {cx + kPopupPadX, content_y, inner_w, kMetaBoxH};
  // ← layout-dialogs.tsx:510-516 bar 高 8px / radius 4 / mt 1.5
  progress_bar_rect_ = {cx + kPopupPadX, meta_box_rect_.bottom() + 12.0f, inner_w, 8.0f};

  // src: popup-styles.ts:231-246 全宽纵向堆叠按钮（升级在上，关闭在下）
  const float actions_y = cy + ch - actions_h;
  btn_upgrade_rect_ = {cx + kPopupPadX, actions_y + kPopupActionsPadY, inner_w,
                       upgrade_h};
  btn_close_rect_ = {cx + kPopupPadX, btn_upgrade_rect_.bottom() + kPopupActionsGap,
                     inner_w, close_h};
}

void CoreUpdateModal::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();  // 自续帧，直到过渡结束
  set_opacity(fade_.value());        // 每帧同步整棵子树的不透明度
  if (fade_.hidden()) return;

  const auto& svc = ThemeService::instance();
  const auto tokens = svc.popup_tokens();

  // src: popup-styles.ts:120-168 左侧 271px 窄窗 Backdrop
  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  rt.fill_rect({0, 0, pane_w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  // 统一弹窗卡片（popup_card_paint：6 套皮肤 + frosted-glass 实底防穿透）
  ::clashwin::ui::draw_card(rt, card_rect_, svc.popup_card_paint());

  const float pad = kPopupPadX;
  const float inner_w = std::max(80.0f, card_rect_.w - pad * 2.0f);
  // 标题 + 分隔线（src: popup-styles.ts:191-212 + layout-dialogs.tsx:456-471）
  TextStyle title_ts;
  title_ts.size = 15.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = tokens.primary_text;
  title_ts.align_h = AlignH::Center;
  title_ts.align_v = AlignV::Center;
  title_ts.ellipsis = true;
  // src: locales/zh/layout.json:82 「"title": "Mihomo 内核更新"」（layout-dialogs.tsx:469 ⚙️）
  rt.draw_text(L"⚙️ " + tr("layout.components.dialogs.core.title"),
               {card_rect_.x + pad, card_rect_.y + kPopupTitlePadY, inner_w,
                kPopupTitleTextH},
               title_ts);
  rt.fill_rect({card_rect_.x, card_rect_.y + kPopupTitleBlockH - 1.0f, card_rect_.w,
                1.0f},
               Brush{tokens.divider_color});

  // 版本信息盒（← layout-dialogs.tsx:468-505：p 1.25 / radius 6 / subtleSurface 底与边）
  Brush mb_bg{tokens.subtle_bg};
  rt.round_rect(meta_box_rect_, 6.0f, {tokens.subtle_border, 1.0f}, &mb_bg);
  // 版本两行（body2 12.5 × 1.4；第一行 mb 0.6 = 4.8；最新版本行 bold）
  TextStyle ver_ts;
  ver_ts.size = 12.5f;
  ver_ts.color = tokens.secondary_text;  // ← :482 第一行用 secondaryText
  ver_ts.align_v = AlignV::Center;
  ver_ts.ellipsis = true;
  rt.draw_text(tr("layout.components.dialogs.update.currentVersion",
                  {{"version", format_core_version(current_version_)}}),
               {meta_box_rect_.x + kChgPad, meta_box_rect_.y + kChgPad,
                meta_box_rect_.w - kChgPad * 2.0f, kPopupBody2H},
               ver_ts);
  // src: layout.json:69/83 「最新版本: {{version}} / 获取中...」（layout-dialogs.tsx:492-495）
  TextStyle ver_bold = ver_ts;
  ver_bold.weight = static_cast<int>(Weight::Bold);
  ver_bold.color = tokens.primary_text;  // ← :497 primaryText
  rt.draw_text(tr("layout.components.dialogs.update.latestVersion",
                  {{"version", latest_version_.empty()
                                   ? utf::utf16_to_utf8(
                                         tr("layout.components.dialogs.core.fetching"))
                                   : format_core_version(latest_version_)}}),
               {meta_box_rect_.x + kChgPad,
                meta_box_rect_.y + kChgPad + kPopupBody2H + 4.8f,
                meta_box_rect_.w - kChgPad * 2.0f, kPopupBody2H},
               ver_bold);

  // 升级进度条（仅 status ≠ idle；← layout-dialogs.tsx:507-529
  //   bgcolor subtleSurfaceBg + 1px subtleSurfaceBorder + radius 4，填充 primary）
  if (status_ != "idle") {
    Brush bar_bg{tokens.subtle_bg};
    rt.round_rect(progress_bar_rect_, 4.0f, {tokens.subtle_border, 1.0f}, &bar_bg);
    float fill_w = progress_bar_rect_.w * (static_cast<float>(progress_) / 100.0f);
    Brush fill_b{tokens.accent_primary};
    rt.round_rect({progress_bar_rect_.x, progress_bar_rect_.y, fill_w, progress_bar_rect_.h},
                  4.0f, {}, &fill_b);

    TextStyle msg_ts;
    msg_ts.size = 12.0f;
    msg_ts.color = tokens.secondary_text;
    std::wstring display_msg = utf::utf8_to_utf16(message_);
    // ← :524 mt: 0.75（6px）
    rt.draw_text(display_msg, {progress_bar_rect_.x, progress_bar_rect_.y + 8.0f + 6.0f,
                               progress_bar_rect_.w, kPopupBody2H},
                 msg_ts);
  }

  // 动作区分隔线（src: popup-styles.ts:243 borderTop 1px）
  rt.fill_rect({card_rect_.x, btn_upgrade_rect_.y - kPopupActionsPadY, card_rect_.w,
                1.0f},
               Brush{tokens.divider_color});

  // 底部按钮（立即升级 contained primary 上、关闭 outlined 下 ← layout-dialogs.tsx:530-565）
  const bool busy = is_upgrading_ || status_ == "checking" || status_ == "downloading" ||
                    status_ == "extracting";
  const bool same_ver =
      !latest_version_.empty() && versions_equal(current_version_, latest_version_);
  const bool upd_disabled = busy || status_ == "done" || same_ver;
  auto paint_themed_btn = [&](IRenderTarget& rt2, const RectF& r, bool hovered,
                              bool disabled, const char* color_type, bool outlined,
                              const std::wstring& label) {
    auto p = svc.button_paint(color_type, outlined);
    // 四段视觉统一由查表给出（此前禁用描边在这里被乘了个自创的 0.38 alpha）
    draw_button_skin(rt2, r, p, hovered ? 1.0f : 0.0f, /*pressed=*/false, disabled);
    TextStyle ts;
    ts.size = 12.5f;  // MUI Button body2
    ts.weight = p.weight;
    ts.color = disabled ? p.text_disabled : p.text;
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt2.draw_text(label, r, ts);
  };

  std::wstring upd_label;
  if (status_ == "done")
    upd_label = tr("layout.components.dialogs.core.updateDone");
  else if (busy)
    upd_label = tr("layout.components.dialogs.update.updating");
  else if (same_ver)
    upd_label = tr("layout.components.dialogs.update.upToDate");
  else
    upd_label = tr("layout.components.dialogs.update.updateNow");
  paint_themed_btn(rt, btn_upgrade_rect_, hover_upgrade_ && !upd_disabled, upd_disabled,
                   "primary", false, upd_label);
  paint_themed_btn(rt, btn_close_rect_, hover_close_ && !busy, busy, "default", true,
                   tr("shared.actions.close"));
}

bool CoreUpdateModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hc = btn_close_rect_.contains(pt.x, pt.y);
    bool hu = btn_upgrade_rect_.contains(pt.x, pt.y);
    if (hc != hover_close_ || hu != hover_upgrade_) {
      hover_close_ = hc;
      hover_upgrade_ = hu;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_rect_.contains(pt.x, pt.y)) {
      // src: layout-dialogs.tsx:415-421 busy 时禁用关闭
      if (!(is_upgrading_ || status_ == "checking" || status_ == "downloading" ||
            status_ == "extracting")) {
        close();
      }
      return true;
    }
    if (btn_upgrade_rect_.contains(pt.x, pt.y)) {
      const bool busy = is_upgrading_ || status_ == "checking" || status_ == "downloading" ||
                        status_ == "extracting";
      const bool same_ver =
          !latest_version_.empty() && versions_equal(current_version_, latest_version_);
      if (!busy && status_ != "done" && !same_ver) {
        is_upgrading_ = true;
        // ← use-core-update.ts:98-100 点击后 coreStarting
        handle_progress_event("checking", 5,
                              utf::utf16_to_utf8(
                                  tr("layout.components.dialogs.progress.coreStarting")));
        if (on_start_upgrade) on_start_upgrade();
      }
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      // src: layout-dialogs.tsx:415-421 checking/downloading/extracting 时 onClose 不生效
      if (!(is_upgrading_ || status_ == "checking" || status_ == "downloading" ||
            status_ == "extracting")) {
        close();
      }
      return true;
    }
    return true;
  }

  return true;
}

bool CoreUpdateModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
    if (!(is_upgrading_ || status_ == "checking" || status_ == "downloading" ||
          status_ == "extracting")) {
      close();
    }
    return true;
  }
  return false;
}

// ==========================================
// 4. LogViewerModal
// ==========================================
LogViewerModal::LogViewerModal(Widget* parent) : Widget(parent) {
  set_z_order(960);
  line_height_ = 38.0f;
  last_flush_time_ = std::chrono::steady_clock::now();

  // src: src/components/base/base-search-box.tsx:117-126 真实可编辑过滤框
  {
    auto sf = std::make_unique<TextField>(this);
    search_field_ = sf.get();
    search_field_->height_ = 28.0f;  // src: src/pages/logs.tsx:191-221
    search_field_->set_padding_x(6.0f);
    search_field_->style_.size = 11.0f;
    search_field_->set_placeholder(tr("shared.placeholders.filter"));
    search_field_->set_i18n_a11y_name("shared.placeholders.filter");
    search_field_->set_clear_button(true, tr("shared.placeholders.resetInput"));
    search_field_->apply_theme();
    search_field_->set_visible(false);
    search_field_->on_text_changed = [this](const std::wstring& w) {
      if (syncing_search_) return;
      set_search_query(utf::utf16_to_utf8(w));
    };
    search_field_->on_key_intercept = [this](const KeyEvent& e) {
      if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
        close();
        return true;
      }
      return false;
    };
    search_field_->on_context_menu = [this](PointF anchor) { open_search_menu(anchor); };
    add_child(std::move(sf));
  }

  // src: logs.tsx:179-191 BaseStyledSelect 5 项
  //   （shared.json:135-141 ALL/DEBUG/INFO/WARN/ERROR）
  auto menu = std::make_unique<MenuList>();
  lvl_menu_ = menu.get();
  {
    const wchar_t* labels[5] = {L"ALL", L"DEBUG", L"INFO", L"WARN", L"ERROR"};
    for (int k = 0; k < 5; ++k) {
      MenuItem it;
      it.label = labels[k];
      it.selected = (k == 0);
      menu->items.push_back(it);
    }
    MenuStyle mst;
    apply_menu_skin(mst);
    // src: src/utils/style-helpers.tsx:126 MenuItem 32px
    mst.item_h = 32.0f;
    mst.min_w = 100.0f;
    apply_menu_style_to_list(lvl_menu_, mst);
    menu->set_visible(false);
    menu->on_select = [this](int i) {
      static const char* vals[5] = {"all", "debug", "info", "warn", "error"};
      if (lvl_menu_) {
        for (int k = 0; k < 5; ++k) lvl_menu_->items[k].selected = (k == i);
        lvl_menu_->set_visible(false);
      }
      set_level_filter(vals[i]);
    };
  }
  add_child(std::move(menu));

  auto sm = std::make_unique<ContextMenu>(this);
  search_menu_ = sm.get();
  search_menu_->set_z_order(1020);  // 豁免: Mini 无对应物 — 弹窗内搜索右键菜单叠在搜索框之上（出处: src/pages/logs.tsx:215）
  add_child(std::move(sm));
}

void LogViewerModal::open() {
  open_ = true;
  fade_.show();
  flush_incoming();
  hover_close_x_ = false;
  hover_pause_ = false;
  hover_order_ = false;
  hover_clear_ = false;
  if (search_field_) {
    search_field_->apply_theme();
    search_field_->style_.size = 11.0f;  // src: src/components/base/base-search-box.tsx:117
    search_field_->set_visible(true);
  }
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

void LogViewerModal::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();  // 画面延后 195ms 消失，交互立刻失效
  if (lvl_menu_) lvl_menu_->set_visible(false);
  if (search_menu_ && search_menu_->is_open()) search_menu_->close();
  if (search_field_) {
    search_field_->blur();
    search_field_->set_visible(false);
  }
  invalidate();
  if (on_dismiss) on_dismiss();
}

void LogViewerModal::set_paused(bool p) {
  if (paused_ == p) return;
  paused_ = p;
  if (!paused_) flush_incoming();
  invalidate();
  if (on_pause_changed) on_pause_changed(paused_);
}

void LogViewerModal::open_search_menu(PointF anchor) {
  if (!search_menu_ || !search_field_ || !search_field_->enabled()) return;
  MenuStyle st;
  apply_menu_skin(st);
  // src: src/utils/style-helpers.tsx:126 菜单行高 32
  st.item_h = 32.0f;
  st.min_w = 160.0f;
  st.font_size = 13.0f;
  st.padding_h = 14.0f;

  std::vector<MenuItem> items;
  items.push_back({tr("shared.actions.cut"), !search_field_->can_cut(), false, L"", false});
  items.push_back({tr("shared.actions.copy"), !search_field_->can_copy(), false, L"", false});
  items.push_back({tr("shared.actions.paste"), !search_field_->can_paste(), false, L"", false});
  items.push_back({tr("shared.actions.selectAll"), !search_field_->can_select_all(), false, L"", false});
  items.push_back({L"", false, true, L"", false});
  items.push_back({tr("shared.actions.clearInput"), !search_field_->can_clear(), false, L"", true});

  search_menu_->style_ = st;
  search_menu_->on_select = [this](int idx) {
    if (!search_field_) return;
    switch (idx) {
      case 0: search_field_->cut(); break;
      case 1: search_field_->copy(); break;
      case 2: search_field_->paste(); break;
      case 3: search_field_->select_all(); break;
      case 5: search_field_->clear_text(); break;
      default: break;
    }
  };
  PointF local = anchor;
  if (search_menu_->parent()) {
    local = search_menu_->parent()->to_local(anchor);
  }
  search_menu_->open(items, local);
}

void LogViewerModal::push_log(const std::string& level, const std::string& msg,
                              const std::string& time_str) {
  LogEntry entry;
  entry.level = level.empty() ? "info" : level;
  entry.message = msg;
  entry.time = time_str.empty() ? get_current_log_time() : time_str;

  incoming_queue_.push_back(std::move(entry));

  // 50ms 批处理检测：若时间跨度超出 kFlushDelayMs，立即自动落盘至展示缓冲区
  auto now = std::chrono::steady_clock::now();
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush_time_).count();
  if (elapsed_ms >= static_cast<int64_t>(kFlushDelayMs)) {
    flush_incoming();
  }
}

void LogViewerModal::push_log_line(const std::string& raw_line) {
  // 简易解析 level=xxx msg="xxx"
  std::string lvl = "info";
  if (raw_line.find("level=debug") != std::string::npos) lvl = "debug";
  else if (raw_line.find("level=warn") != std::string::npos) lvl = "warn";
  else if (raw_line.find("level=error") != std::string::npos) lvl = "error";
  push_log(lvl, raw_line);
}

void LogViewerModal::flush_incoming() {
  last_flush_time_ = std::chrono::steady_clock::now();
  if (incoming_queue_.empty()) return;

  if (!paused_) {
    const float item_w = std::max(40.0f, log_area_rect_.w - 12.0f);  // src: src/components/log/log-item.tsx:10
    float added_h = 0.0f;
    float evicted_h = 0.0f;
    for (auto& item : incoming_queue_) {
      if (matches_filter(item)) {
        added_h += measure_entry_height(item, item_w);
      }
      logs_.push_back(std::move(item));
      // 1000 行环形截断 (FIFO clamp)
      if (logs_.size() > kMaxLogLines) {
        if (matches_filter(logs_.front())) {
          evicted_h += measure_entry_height(logs_.front(), item_w);
        }
        logs_.pop_front();
      }
    }
    recompute_total_height();
    const float max_s = max_scroll();
    if (desc_) {
      if (scroll_y_ > 4.0f) {  // src: src/pages/logs.tsx:74
        scroll_y_ = std::clamp(scroll_y_ + added_h, 0.0f, max_s);
      } else {
        scroll_y_ = 0.0f;
      }
    } else {
      if (stick_to_bottom_) {
        scroll_y_ = max_s;
      } else {
        scroll_y_ = std::clamp(scroll_y_ - evicted_h, 0.0f, max_s);
      }
    }
  }
  incoming_queue_.clear();
  invalidate();
}

void LogViewerModal::clear_logs() {
  logs_.clear();
  incoming_queue_.clear();
  total_content_h_ = 0.0f;
  scroll_y_ = 0.0f;
  stick_to_bottom_ = true;
  invalidate();
}

void LogViewerModal::set_desc(bool d) {
  if (desc_ == d) return;
  desc_ = d;
  recompute_total_height();
  if (desc_) {
    scroll_y_ = 0.0f;
  } else {
    stick_to_bottom_ = true;
    scroll_y_ = max_scroll();
  }
  invalidate();
}

void LogViewerModal::set_level_filter(const std::string& lvl) {
  if (level_filter_ == lvl) return;
  level_filter_ = lvl;
  recompute_total_height();
  if (!desc_ && stick_to_bottom_) {
    scroll_y_ = max_scroll();
  } else {
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
  }
  invalidate();
}

void LogViewerModal::set_search_query(const std::string& q) {
  if (search_query_ == q) return;
  search_query_ = q;
  if (search_field_ && !syncing_search_) {
    const std::wstring wq = utf::utf8_to_utf16(q);
    if (search_field_->text() != wq) {
      syncing_search_ = true;
      search_field_->set_text_silent(wq);
      syncing_search_ = false;
    }
  }
  recompute_total_height();
  if (!desc_ && stick_to_bottom_) {
    scroll_y_ = max_scroll();
  } else {
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
  }
  invalidate();
}

float LogViewerModal::max_scroll() const {
  return std::max(0.0f, total_content_h_ - std::max(0.0f, log_area_rect_.h));
}

void LogViewerModal::set_scroll_y(float y) {
  const float max_s = max_scroll();
  scroll_y_ = std::clamp(y, 0.0f, max_s);
  if (!desc_) {
    stick_to_bottom_ = (max_s - scroll_y_ <= 48.0f);  // src: src/pages/logs.tsx:74
  }
  invalidate();
}

float LogViewerModal::measure_entry_height(const LogEntry& e, float item_w) const {
  TextStyle log_ts;
  log_ts.size = 10.0f;  // src: src/components/log/log-item.tsx:13
  log_ts.weight = static_cast<int>(Weight::Normal);
  log_ts.align_v = AlignV::Center;
  log_ts.wrap = true;  // src: src/components/log/log-item.tsx:38
  log_ts.ellipsis = false;
  float msg_h = 16.0f;  // src: src/components/log/log-item.tsx:11
  if (ITextEngine* eng = text_engine()) {
    msg_h = eng->measure_text(utf::utf8_to_utf16(e.message), log_ts, item_w).h;
  }
  if (msg_h < 16.0f) msg_h = 16.0f;  // src: src/components/log/log-item.tsx:11
  return 17.0f + msg_h + 5.0f;  // src: src/components/log/log-item.tsx:9
}

void LogViewerModal::recompute_total_height() {
  const float item_w = std::max(40.0f, log_area_rect_.w - 12.0f);  // src: src/components/log/log-item.tsx:10
  float sum = 0.0f;
  size_t count = 0;
  for (const auto& item : logs_) {
    if (!matches_filter(item)) continue;
    sum += measure_entry_height(item, item_w);
    ++count;
  }
  total_content_h_ = count > 0 ? (sum + 12.0f) : 0.0f;  // src: src/components/log/log-item.tsx:9
}

bool LogViewerModal::matches_filter(const LogEntry& entry) const {
  // src: logs.tsx:184-190 级别值为 debug/info/warn/err；
  //   内核实际吐出 warning/error，需归一后比较（否则 warn 过滤永远为空）
  if (level_filter_ != "all") {
    std::string want = level_filter_;
    if (want == "warn") want = "warning";
    const bool hit = entry.level == want ||
                     (want == "error" && entry.level == "err");
    if (!hit) return false;
  }
  if (!search_query_.empty()) {
    if (!icontains_ascii_or_utf8(entry.time, search_query_) &&
        !icontains_ascii_or_utf8(entry.level, search_query_) &&
        !icontains_ascii_or_utf8(entry.message, search_query_)) {
      return false;
    }
  }
  return true;
}

void LogViewerModal::on_arrange(const RectF& area) {
  set_bounds(area);
  // 对齐 Mini（popup-styles.ts:43-50）：贴左弹出、宽度收敛至主窗窄面板 271px
  const float cw = std::min(area.w, theme::kPopupPaneWidth);
  const float ch = std::min(480.0f, area.h - 16.0f);
  float cx = 0.0f;
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  // 第 1 行（G6 header 44px：layout-dialogs.tsx:574 flex '0 0 44px'）：
  //   标题在左，按钮靠右 [⏸/▶] [⇅] [清除] [✕]
  // G7: 关闭钮 top/right 6→10（layout-dialogs.tsx:594-595 top:10 right:10）
  // G9: 图标钮 26×26（IconButton p:0.5 + 图标 18，logs.tsx:121/:138）、清除钮高 24（:175）
  btn_close_x_rect_ = {cx + cw - 30.0f, cy + 10.0f, 20.0f, 22.0f};
  btn_clear_rect_ = {btn_close_x_rect_.x - 4.0f - 40.0f, cy + 10.0f, 40.0f, 24.0f};
  btn_order_rect_ = {btn_clear_rect_.x - 4.0f - 26.0f, cy + 9.0f, 26.0f, 26.0f};
  btn_pause_rect_ = {btn_order_rect_.x - 4.0f - 26.0f, cy + 9.0f, 26.0f, 26.0f};

  // 过滤行（G6: header 44 之后；logs.tsx:178-186 pt1 + 39px + mb0.5、mx:10px）
  // G8: 级别下拉宽 76→95（logs.tsx:190 width:95, minWidth:90），行内垂直居中
  toolbar_rect_ = {cx + 10.0f, cy + 44.0f, cw - 20.0f, 39.0f};
  seg_level_rect_ = {toolbar_rect_.x, toolbar_rect_.y + 5.0f, 95.0f, 28.0f};
  search_box_rect_ = {seg_level_rect_.x + seg_level_rect_.w + 6.0f, toolbar_rect_.y + 5.0f,
                      toolbar_rect_.w - (seg_level_rect_.w + 6.0f), 28.0f};
  if (search_field_) search_field_->layout_tree(search_box_rect_);
  if (search_menu_) search_menu_->layout_tree({0.0f, 0.0f, area.w, area.h});

  // G6: 日志区起于 header44 + 过滤行39 + mb4 = cy+87；底边距 8 不变
  log_area_rect_ = {cx + 10.0f, cy + 87.0f, cw - 20.0f, ch - 95.0f};
  recompute_total_height();
  if (!desc_ && stick_to_bottom_) {
    scroll_y_ = max_scroll();
  } else {
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
  }
}

void LogViewerModal::on_paint(IRenderTarget& rt) {
  // Mini 的日志为 MUI Dialog（src/pages/_layout/components/layout-dialogs.tsx:556），
  // 默认 Backdrop = rgba(0,0,0,0.5)（@mui/material/Backdrop/Backdrop.js:48）；
  // 点遮罩/按 Esc 均关闭（layout-dialogs.tsx:558 `onClose={() => setLogsOpen(false)}`）。
  if (fade_.advance()) invalidate();  // 自续帧，直到过渡结束
  set_opacity(fade_.value());        // 每帧同步整棵子树的不透明度
  if (fade_.hidden()) return;

  // src: src/pages/logs.tsx + src/pages/_layout/components/layout-dialogs.tsx:595-641
  // 已按 Mini 主题色实现，无终端风配色字面量（过时豁免注释已删）。
  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  rt.fill_rect({0, 0, pane_w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  // ★ 卡片底座走 popup_card_paint（6 皮肤半透磨砂/全不透高对比适配）：
  //   src: layout-dialogs.tsx:556-594 Dialog paper + popup-styles.ts
  {
    const auto& svc = ThemeService::instance();
    const auto& pal = svc.palette();
    const auto& vars = svc.vars();
    const auto pp = svc.popup_card_paint();
    if (!pp.has_shadow && !pp.has_glow) {
      Brush shadow_b{Color4f(0, 0, 0, 0.22f)};
      rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5},
                    pp.radius, {}, &shadow_b);
    }
    ::clashwin::ui::draw_card(rt, card_rect_, pp);

  // 标题
  TextStyle title_ts;
  title_ts.size = 15.0f;   // G6: 13.5 Bold → 15px/600（layout-dialogs.tsx:577-578）
  title_ts.weight = 600;
  title_ts.color = pal.primary_text;
  title_ts.align_v = AlignV::Center;
  // src: locales/zh/logs.json:3 「"title": "日志"」（logs.tsx:106 BasePage title）
  // G6: pl 1.5 = 12px、整头高 44 垂直居中
  rt.draw_text(tr("logs.page.title"), {card_rect_.x + 12.0f, card_rect_.y, btn_pause_rect_.x - (card_rect_.x + 16.0f), 44.0f},
               title_ts);

  // 关闭 ✕（MUI IconButton：hover = action.hover）
  const Color4f action_hover =
      svc.is_light() ? Color4f::hex(0x000000, 0.04f)
                      : Color4f::hex(0xFFFFFF, 0.08f);
  Color4f x_bg = hover_close_x_ ? action_hover : Color4f(0, 0, 0, 0);
  Brush xb{x_bg};
  rt.round_rect(btn_close_x_rect_, 3.0f, Pen{Color4f(0, 0, 0, 0), 0.0f}, &xb);
  TextStyle x_ts;
  x_ts.size = 11.5f;
  x_ts.color = pal.primary_text;
  x_ts.align_h = AlignH::Center;
  x_ts.align_v = AlignV::Center;
  rt.draw_text(L"✕", btn_close_x_rect_, x_ts);

  rt.fill_rect({card_rect_.x, card_rect_.y + 44.0f, card_rect_.w, 1.0f},
               Brush{vars.theme_border});

  // 工具栏按钮（active = warning 主色；hover = action.hover）
  // F-D01：label 允许 locale 译文（std::wstring）与图标字面量（const wchar_t*）
  const Color4f warn_c = svc.is_light() ? Color4f::hex(0xE65100)   // warning.dark
                                        : Color4f::hex(0xFFD54F);  // warning.main
  auto draw_tool_btn = [&](const RectF& r, const std::wstring& label, bool hover,
                           bool active = false) {
    Color4f bg = active ? Color4f(0, 0, 0, 0)
                        : (hover ? action_hover : Color4f(0, 0, 0, 0));
    Brush b{bg};
    Pen p{active ? warn_c : Color4f(0, 0, 0, 0), active ? 1.0f : 0.0f};
    rt.round_rect(r, 4.0f, p, &b);
    TextStyle ts;
    ts.size = 11.0f;  // G9: 清除钮 11px 字号（logs.tsx:172 fontSize '11px'）
    ts.color = active ? warn_c : pal.primary_text;
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt.draw_text(label, r, ts);
  };

  // G9: 暂停/排序 → 18px MUI 图标 + p:0.5（logs.tsx:118-152 PauseCircleOutlineRounded /
  //   PlayCircleOutlineRounded / SwapVertRounded fontSize 18；降序态 scaleY(-1) 翻转）
  auto draw_icon_btn = [&](const RectF& r, const char* icon, bool hover, bool active,
                           bool flip) {
    Color4f bg = hover ? action_hover : Color4f(0, 0, 0, 0);
    Brush b{bg};
    rt.round_rect(r, r.w * 0.5f, Pen{Color4f(0, 0, 0, 0), 0.0f}, &b);  // IconButton 圆形 hover
    IconW::paint_at(rt, icon,
                    {r.x + (r.w - 18.0f) * 0.5f, r.y + (r.h - 18.0f) * 0.5f, 18.0f, 18.0f},
                    active ? warn_c : pal.primary_text, flip);
  };
  draw_icon_btn(btn_pause_rect_, paused_ ? "playcircleoutline" : "pausecircleoutline",
                hover_pause_, paused_, false);
  draw_icon_btn(btn_order_rect_, "swapvert", hover_order_, desc_, desc_);
  // src: locales/zh/shared.json:24 「"clear": "清除"」（logs.tsx:164）
  draw_tool_btn(btn_clear_rect_, tr("shared.actions.clear"), hover_clear_);

  // 级别过滤显示
  TextStyle lvl_ts;
  lvl_ts.size = 10.0f;
  lvl_ts.color = pal.secondary_text;
  lvl_ts.align_v = AlignV::Center;
  // src: locales/zh/shared.json:137-141 级别取值「ALL/DEBUG/INFO/WARN/ERROR」（logs.tsx:179-191）
  std::wstring lvl_str;
  if (level_filter_ == "all") lvl_str = L"ALL";
  else if (level_filter_ == "debug") lvl_str = L"DEBUG";
  else if (level_filter_ == "info") lvl_str = L"INFO";
  else if (level_filter_ == "warn") lvl_str = L"WARN";
  else lvl_str = L"ERROR";
  Brush lb{vars.theme_input_bg};
  rt.round_rect(seg_level_rect_, 4.0f, {vars.theme_input_border, 1.0f}, &lb);
  lvl_ts.color = pal.primary_text;
  rt.draw_text(lvl_str, {seg_level_rect_.x + 8.0f, seg_level_rect_.y, seg_level_rect_.w - 22.0f, seg_level_rect_.h}, lvl_ts);
  TextStyle arrow_ts;
  arrow_ts.size = 8.5f;
  arrow_ts.color = pal.secondary_text;
  arrow_ts.align_h = AlignH::Right;
  arrow_ts.align_v = AlignV::Center;
  rt.draw_text(L"▼", {seg_level_rect_.x, seg_level_rect_.y, seg_level_rect_.w - 6.0f, seg_level_rect_.h}, arrow_ts);

  // 搜索框由真实子控件 search_field_ 自绘，仅随主题热刷新参数
  if (search_field_) {
    search_field_->apply_theme();
    // src: src/components/base/base-search-box.tsx:117 fontSize 11px
    search_field_->style_.size = 11.0f;
    search_field_->set_placeholder(tr("shared.placeholders.filter"));
  }
  }

  // src: components/log/log-item.tsx:8-14 日志项底色随页面（无终端暗底），
  //   分隔线 palette.divider；文字色为 text.primary/secondary + 级别色
  Brush log_bg{theme::ThemeService::instance().vars().theme_panel_bg};
  rt.round_rect(log_area_rect_, 4.0f,
                {theme::ThemeService::instance().vars().theme_border, 1.0f}, &log_bg);

  // 搜集过滤后的日志列表
  std::vector<const LogEntry*> visible_entries;
  visible_entries.reserve(logs_.size());
  if (desc_) {
    for (auto it = logs_.rbegin(); it != logs_.rend(); ++it) {
      if (matches_filter(*it)) visible_entries.push_back(&*it);
    }
  } else {
    for (const auto& item : logs_) {
      if (matches_filter(item)) visible_entries.push_back(&item);
    }
  }

  rt.push_clip(log_area_rect_);
  float y = log_area_rect_.y + 6.0f - scroll_y_;
  TextStyle log_ts;
  log_ts.size = 10.5f;
  // src: log-item.tsx:14 fontFamily: typography.fontFamily（非等宽终端字体）
  log_ts.align_v = AlignV::Center;
  log_ts.wrap = true;       // G10: overflowWrap 'anywhere'（log-item.tsx .data）
  log_ts.ellipsis = false;  // 271px 下无横向滚动、自动折行
  const auto& pal = theme::ThemeService::instance().palette();
  const std::wstring q_w = utf::utf8_to_utf16(search_query_);

  float x = log_area_rect_.x + 6.0f;
  float item_w = log_area_rect_.w - 12.0f;
  for (size_t i = 0; i < visible_entries.size(); ++i) {
    const auto* e = visible_entries[i];
    const std::wstring t_msg = utf::utf8_to_utf16(e->message);

    // G10: 条目高度随折行数自适应（Mini VirtualList estimateSize 50 动态高）；
    //   视区上方条目也需测高以正确推进 y。
    log_ts.size = 10.0f;
    log_ts.weight = static_cast<int>(Weight::Normal);
    log_ts.color = pal.primary_text;
    float msg_h = text_engine()->measure_text(t_msg, log_ts, item_w).h;
    if (msg_h < 16.0f) msg_h = 16.0f;
    const float item_h = 17.0f + msg_h + 5.0f;  // 行1 17 + 正文 + 底距 5（单行 ≥ 旧 38）

    if (y + item_h >= log_area_rect_.y && y <= log_area_rect_.y + log_area_rect_.h) {
      // 对齐 Mini log-item.tsx:108-118 双行呈现：
      // 第 1 行：时间 (text.secondary) + 级别 (error/warning/info 主色，大写加粗)
      // 第 2 行：正文 payload (text.primary，G10 折行 + 搜索词 #ffeb3b 高亮)
      // 底部：1px divider 分隔线
      const std::wstring t_time = utf::utf8_to_utf16(e->time);
      std::wstring t_type = utf::utf8_to_utf16(e->level);
      for (auto& c : t_type) c = towupper(c);
      Color4f type_c = (e->level == "error" || e->level == "err")
                           ? pal.error_color
                           : ((e->level == "warn" || e->level == "warning")
                                  ? pal.warning_color
                                  : pal.info_color);

      // 第 1 行：time + type
      log_ts.size = 9.5f;
      log_ts.weight = static_cast<int>(Weight::Normal);
      log_ts.color = pal.secondary_text;
      draw_text_with_highlights(rt, text_engine(), t_time, q_w,
                                {x, y + 2.0f, item_w, 14.0f}, log_ts);

      float time_w = text_engine()->measure_text(t_time, log_ts, 0).w;
      log_ts.weight = static_cast<int>(Weight::Bold);
      log_ts.color = type_c;
      draw_text_with_highlights(rt, text_engine(), t_type, q_w,
                                {x + time_w + 8.0f, y + 2.0f, item_w - time_w - 8.0f, 14.0f},
                                log_ts);

      // 第 2 行：payload 正文（G10 折行，高度 = 测量行数）
      log_ts.size = 10.0f;
      log_ts.weight = static_cast<int>(Weight::Normal);
      log_ts.color = pal.primary_text;
      draw_text_with_highlights(rt, text_engine(), t_msg, q_w,
                                {x, y + 17.0f, item_w, msg_h}, log_ts);

      // 底部 1px 分隔线
      rt.fill_rect({x, y + item_h - 1.0f, item_w, 1.0f},
                   Brush{theme::ThemeService::instance().vars().theme_border});
    }
    y += item_h;
    if (y > log_area_rect_.y + log_area_rect_.h) break;  // 已过可视底界
  }
  rt.pop_clip();
}

bool LogViewerModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (lvl_menu_ && lvl_menu_->visible()) {
    if (lvl_menu_->bounds().contains(pt.x, pt.y)) {
      MouseEvent child_e = e;
      child_e.pos.x -= lvl_menu_->bounds().x;
      child_e.pos.y -= lvl_menu_->bounds().y;
      return lvl_menu_->on_mouse(child_e);
    }
    if (e.type == MouseEventType::Up || e.type == MouseEventType::Down) {
      if (!seg_level_rect_.contains(pt.x, pt.y)) {
        lvl_menu_->set_visible(false);
        invalidate();
        return true;
      }
    }
  }

  if (e.type == MouseEventType::Move) {
    bool hx = btn_close_x_rect_.contains(pt.x, pt.y);
    bool hp = btn_pause_rect_.contains(pt.x, pt.y);
    bool ho = btn_order_rect_.contains(pt.x, pt.y);
    const bool hc = btn_clear_rect_.contains(pt.x, pt.y);
    if (hx != hover_close_x_ || hp != hover_pause_ || ho != hover_order_ || hc != hover_clear_) {
      hover_close_x_ = hx;
      hover_pause_ = hp;
      hover_order_ = ho;
      hover_clear_ = hc;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Wheel) {
    float delta = static_cast<float>(e.wheel_delta) / 120.0f * line_height_ * 3.0f;  // src: src/pages/logs.tsx:227
    set_scroll_y(scroll_y_ - delta);
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_x_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (btn_pause_rect_.contains(pt.x, pt.y)) {
      set_paused(!paused_);
      return true;
    }
    if (btn_order_rect_.contains(pt.x, pt.y)) {
      set_desc(!desc_);
      return true;
    }
    if (btn_clear_rect_.contains(pt.x, pt.y)) {
      clear_logs();
      return true;
    }
    if (seg_level_rect_.contains(pt.x, pt.y)) {
      // src: logs.tsx:179-191 点击展开 5 项下拉（非点击循环）
      if (lvl_menu_) {
        const bool opening = !lvl_menu_->visible();
        if (opening) {
          MenuStyle st;
          apply_menu_skin(st);
          // src: src/utils/style-helpers.tsx:126 MenuItem 32px
          st.item_h = 32.0f;
          st.min_w = 100.0f;
          apply_menu_style_to_list(lvl_menu_, st);
          const float menu_w = 100.0f;
          const float menu_h = lvl_menu_->content_height() + 2.0f;
          float menu_x = std::max(card_rect_.x + 8.0f,
                                  std::min(seg_level_rect_.x, card_rect_.x + card_rect_.w - menu_w - 8.0f));
          float menu_y = (seg_level_rect_.bottom() + 2.0f + menu_h <= card_rect_.y + card_rect_.h - 8.0f)
                             ? (seg_level_rect_.bottom() + 2.0f)
                             : (seg_level_rect_.y - menu_h - 2.0f);
          lvl_menu_->layout_tree({menu_x, menu_y, menu_w, menu_h});
          lvl_menu_->set_z_order(1000);
        }
        lvl_menu_->set_visible(opening);
      }
      invalidate();
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }

  return true;
}

bool LogViewerModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
    close();
    return true;
  }
  return false;
}

// ==========================================
// UI Automation 虚拟子项实现
// ==========================================
std::wstring EditProfileModal::a11y_default_name() const {
  return tr("layout.components.dialogs.editProfile.title");
}

A11ySubItem EditProfileModal::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  if (idx == 0) {
    sub.role = A11yRole::Edit;
    sub.name = tr("layout.components.dialogs.editProfile.nameLabel");
    sub.local_bounds = name_input_rect_;
    sub.focusable = true;
    sub.can_set_value = true;
    sub.value = utf::utf8_to_utf16(name_);
    return sub;
  }
  if (!is_local_ && idx == 1) {
    sub.role = A11yRole::Edit;
    sub.name = tr("layout.components.dialogs.editProfile.urlLabel");
    sub.local_bounds = url_input_rect_;
    sub.focusable = true;
    sub.can_set_value = true;
    sub.value = utf::utf8_to_utf16(url_);
    return sub;
  }
  if (!is_local_ && idx == 2) {
    sub.role = A11yRole::Edit;
    sub.name = tr("layout.components.dialogs.editProfile.intervalLabel");
    sub.local_bounds = interval_input_rect_;
    sub.focusable = true;
    sub.can_set_value = true;
    sub.value = std::to_wstring(interval_mins_);
    return sub;
  }
  const size_t base = is_local_ ? 1u : 3u;
  const size_t btn_idx = (idx >= base) ? (idx - base) : 99u;
  sub.role = A11yRole::Button;
  sub.can_invoke = enabled();
  if (btn_idx == 0) {
    sub.name = tr("shared.actions.cancel");
    sub.local_bounds = btn_cancel_rect_;
  } else {
    sub.name = tr("shared.actions.save");
    sub.local_bounds = btn_save_rect_;
  }
  return sub;
}

bool EditProfileModal::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  const size_t base = is_local_ ? 1u : 3u;
  if (idx < base) return false;
  const size_t btn_idx = idx - base;
  if (btn_idx == 0) {
    close();
    return true;
  }
  if (btn_idx == 1) {
    if (on_save) on_save(name_, url_, interval_mins_);
    close();
    return true;
  }
  return false;
}

bool EditProfileModal::a11y_set_virtual_child_value(size_t idx, const std::wstring& v) {
  if (!open_ || !enabled()) return false;
  if (idx == 0) {
    set_name(utf::utf16_to_utf8(v));
    return true;
  }
  if (!is_local_ && idx == 1) {
    set_url(utf::utf16_to_utf8(v));
    return true;
  }
  if (!is_local_ && idx == 2) {
    set_interval_mins(_wtoi(v.c_str()));
    return true;
  }
  return false;
}

std::wstring ClientUpdateModal::a11y_default_name() const {
  return tr("layout.components.dialogs.client.title");
}

A11ySubItem ClientUpdateModal::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  const bool busy = is_downloading_;
  const bool same_ver = versions_equal(latest_version_, current_version_);
  if (idx == 0) {
    sub.role = A11yRole::Text;
    const std::wstring unknown = tr("layout.components.dialogs.update.unknown");
    const std::string cur_disp = current_version_.empty()
                                     ? utf::utf16_to_utf8(unknown)
                                     : ensure_v_prefix(current_version_);
    const std::string lat_disp = latest_version_.empty()
                                     ? utf::utf16_to_utf8(unknown)
                                     : ensure_v_prefix(latest_version_);
    sub.name = tr("layout.components.dialogs.update.currentVersion",
                  {{"version", cur_disp}}) +
               L", " +
               tr("layout.components.dialogs.update.latestVersion",
                  {{"version", lat_disp}});
    sub.local_bounds = card_rect_;
    return sub;
  }
  sub.role = A11yRole::Button;
  if (idx == 1) {
    sub.enabled = !busy;
    sub.can_invoke = !busy;
    sub.name = tr("shared.actions.close");
    sub.local_bounds = btn_close_rect_;
  } else {
    const bool upd_disabled = busy || status_ == "done" || same_ver;
    sub.enabled = !upd_disabled;
    sub.can_invoke = !upd_disabled && !download_url_.empty() && progress_ < 100;
    if (status_ == "done")
      sub.name = tr("layout.components.dialogs.client.prepareRestart");
    else if (busy)
      sub.name = tr("layout.components.dialogs.update.updating");
    else if (same_ver)
      sub.name = tr("layout.components.dialogs.update.upToDate");
    else
      sub.name = tr("layout.components.dialogs.update.updateNow");
    sub.local_bounds = btn_update_rect_;
  }
  return sub;
}

bool ClientUpdateModal::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  if (idx == 1) {
    if (is_downloading_) return false;
    close();
    return true;
  }
  if (idx == 2) {
    if (download_url_.empty() || is_downloading_ || status_ == "done" ||
        progress_ >= 100 || versions_equal(latest_version_, current_version_)) {
      return false;
    }
    handle_progress_event("downloading", 0,
                          utf::utf16_to_utf8(
                              tr("layout.components.dialogs.progress.clientDownloading")));
    if (on_start_update) on_start_update();
    return true;
  }
  return false;
}

std::wstring CoreUpdateModal::a11y_default_name() const {
  return tr("layout.components.dialogs.core.title");
}

A11ySubItem CoreUpdateModal::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  const bool busy = is_upgrading_ || status_ == "checking" ||
                    status_ == "downloading" || status_ == "extracting";
  const bool same_ver =
      !latest_version_.empty() && versions_equal(current_version_, latest_version_);
  if (idx == 0) {
    sub.role = A11yRole::Text;
    sub.name = tr("layout.components.dialogs.update.currentVersion",
                  {{"version", format_core_version(current_version_)}}) +
               L", " +
               tr("layout.components.dialogs.update.latestVersion",
                  {{"version", latest_version_.empty()
                                   ? utf::utf16_to_utf8(
                                         tr("layout.components.dialogs.core.fetching"))
                                   : format_core_version(latest_version_)}});
    sub.local_bounds = card_rect_;
    return sub;
  }
  sub.role = A11yRole::Button;
  if (idx == 1) {
    sub.enabled = !busy;
    sub.can_invoke = !busy;
    sub.name = tr("shared.actions.close");
    sub.local_bounds = btn_close_rect_;
  } else {
    const bool upd_disabled = busy || status_ == "done" || same_ver;
    sub.enabled = !upd_disabled;
    sub.can_invoke = !upd_disabled;
    if (status_ == "done")
      sub.name = tr("layout.components.dialogs.core.updateDone");
    else if (busy)
      sub.name = tr("layout.components.dialogs.update.updating");
    else if (same_ver)
      sub.name = tr("layout.components.dialogs.update.upToDate");
    else
      sub.name = tr("layout.components.dialogs.update.updateNow");
    sub.local_bounds = btn_upgrade_rect_;
  }
  return sub;
}

bool CoreUpdateModal::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  const bool busy = is_upgrading_ || status_ == "checking" ||
                    status_ == "downloading" || status_ == "extracting";
  const bool same_ver =
      !latest_version_.empty() && versions_equal(current_version_, latest_version_);
  if (idx == 1) {
    if (busy) return false;
    close();
    return true;
  }
  if (idx == 2) {
    if (busy || status_ == "done" || same_ver) return false;
    is_upgrading_ = true;
    handle_progress_event("checking", 5,
                          utf::utf16_to_utf8(
                              tr("layout.components.dialogs.progress.coreStarting")));
    if (on_start_upgrade) on_start_upgrade();
    return true;
  }
  return false;
}

std::wstring LogViewerModal::a11y_default_name() const {
  return tr("settings.mini.debugLogs");
}

A11ySubItem LogViewerModal::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  if (idx == 0) {
    sub.role = A11yRole::Button;
    sub.name = tr("layout.a11y.closeLogsDialog");
    sub.local_bounds = btn_close_x_rect_;
    sub.can_invoke = true;
  } else if (idx == 1) {
    sub.role = A11yRole::Button;
    sub.name = paused_ ? tr("shared.actions.resume") : tr("shared.actions.pause");
    sub.local_bounds = btn_pause_rect_;
    sub.can_invoke = true;
  } else if (idx == 2) {
    sub.role = A11yRole::Button;
    sub.name = desc_ ? tr("logs.actions.showAscending") : tr("logs.actions.showDescending");
    sub.local_bounds = btn_order_rect_;
    sub.can_invoke = true;
  } else if (idx == 3) {
    sub.role = A11yRole::Button;
    sub.name = tr("shared.actions.clear");
    sub.local_bounds = btn_clear_rect_;
    sub.can_invoke = true;
  } else if (idx == 4) {
    sub.role = A11yRole::ComboBox;
    sub.name = utf::utf8_to_utf16(level_filter_);
    sub.local_bounds = seg_level_rect_;
    sub.can_invoke = true;
    sub.expanded = (lvl_menu_ && lvl_menu_->visible());
    sub.value = utf::utf8_to_utf16(level_filter_);
  } else {
    sub.role = A11yRole::Edit;
    sub.name = tr("shared.placeholders.filter");
    sub.local_bounds = search_box_rect_;
    sub.focusable = true;
    sub.can_set_value = true;
    sub.value = utf::utf8_to_utf16(search_query_);
  }
  return sub;
}

bool LogViewerModal::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  switch (idx) {
    case 0: close(); return true;
    case 1: set_paused(!paused_); return true;
    case 2: set_desc(!desc_); return true;
    case 3: clear_logs(); return true;
    case 4:
      if (lvl_menu_) {
        lvl_menu_->set_visible(!lvl_menu_->visible());
        invalidate();
      }
      return true;
    default: return false;
  }
}

bool LogViewerModal::a11y_set_virtual_child_value(size_t idx, const std::wstring& v) {
  if (!open_ || !enabled() || idx != 5) return false;
  set_search_query(utf::utf16_to_utf8(v));
  return true;
}

// ==========================================
// 7. DeleteProfileConfirmModal (BUG-390)
//   src: src/pages/_layout/components/layout-dialogs.tsx:643-698
// ==========================================
DeleteProfileConfirmModal::DeleteProfileConfirmModal(Widget* parent)
    : Widget(parent) {
  set_z_order(980);
}

void DeleteProfileConfirmModal::open(const std::string& uid,
                                     const std::string& profile_name) {
  uid_ = uid;
  profile_name_ = profile_name;
  open_ = true;
  fade_.show();
  hover_cancel_ = false;
  hover_delete_ = false;
  pressed_cancel_ = false;
  pressed_delete_ = false;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

void DeleteProfileConfirmModal::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();
  invalidate();
  if (on_dismiss) on_dismiss();
}

void DeleteProfileConfirmModal::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: layout-dialogs.tsx:647-698 + popup-styles.ts:120-246 窄窗 271px 左锚定
  const float cw = std::min(theme::kPopupPaneWidth, area.w);
  const float pad = kPopupPadX;
  const float inner_w = std::max(80.0f, cw - pad * 2.0f);
  const float title_h = kPopupTitleBlockH;
  float name_h = 0.0f;
  float msg_h = 22.0f;
  if (ITextEngine* te = text_engine()) {
    if (!profile_name_.empty()) {
      TextStyle nts;
      nts.size = 13.0f;
      nts.weight = static_cast<int>(Weight::Bold);
      nts.wrap = true;
      name_h = std::max(20.0f,
                        te->measure_text(utf::utf8_to_utf16(profile_name_), nts, inner_w).h + 4.0f);
    }
    TextStyle mts;
    mts.size = 13.0f;
    mts.wrap = true;
    msg_h = std::max(20.0f,
                     te->measure_text(tr("profiles.modals.confirmDelete.message"), mts, inner_w).h + 4.0f);
  } else if (!profile_name_.empty()) {
    name_h = 22.0f;
  }
  const float content_h =
      kPopupContentPadY * 2.0f + name_h + (name_h > 0.0f ? 8.0f : 0.0f) + msg_h;
  // src: layout-dialogs.tsx:676-695 Cancel(12.5px, py .7) 在上 / Delete(13px, py .85) 在下
  const float cancel_h = popup_btn_h("default", 12.5f, 5.6f);
  const float delete_h = popup_btn_h("error", 13.0f, 6.8f);
  const float acts_h =
      kPopupActionsPadY * 2.0f + cancel_h + kPopupActionsGap + delete_h;
  const float ch = std::min(title_h + content_h + acts_h, std::max(140.0f, area.h - 16.0f));
  const float cx = 0.0f;
  const float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  const float acts_top = cy + ch - acts_h;
  btn_cancel_rect_ = {cx + pad, acts_top + kPopupActionsPadY, inner_w, cancel_h};
  btn_delete_rect_ = {cx + pad, btn_cancel_rect_.bottom() + kPopupActionsGap, inner_w, delete_h};
}

void DeleteProfileConfirmModal::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();
  set_opacity(fade_.value());
  if (fade_.hidden()) return;

  if (card_rect_.w <= 0.0f && bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }

  const auto& svc = ThemeService::instance();
  const auto tokens = svc.popup_tokens();

  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  rt.fill_rect({0.0f, 0.0f, pane_w, bounds().h},
               Brush{Color4f(0.0f, 0.0f, 0.0f, 0.5f)});
  ::clashwin::ui::draw_card(rt, card_rect_, svc.popup_card_paint());

  const float pad = kPopupPadX;
  const float inner_w = std::max(80.0f, card_rect_.w - pad * 2.0f);
  // src: layout-dialogs.tsx:654 fontSize '15.5px'
  TextStyle title_ts;
  title_ts.size = 15.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = tokens.primary_text;
  title_ts.align_h = AlignH::Center;
  title_ts.align_v = AlignV::Center;
  rt.draw_text(tr("profiles.modals.confirmDelete.title"),
               {card_rect_.x + pad, card_rect_.y + kPopupTitlePadY, inner_w, kPopupTitleTextH},
               title_ts);
  rt.fill_rect({card_rect_.x, card_rect_.y + kPopupTitleBlockH - 1.0f, card_rect_.w, 1.0f},
               Brush{tokens.divider_color});

  float text_y = card_rect_.y + kPopupTitleBlockH + kPopupContentPadY;
  if (!profile_name_.empty()) {
    TextStyle nts;
    nts.size = 13.0f;
    nts.weight = static_cast<int>(Weight::Bold);
    nts.color = tokens.primary_text;
    nts.wrap = true;
    const float max_h =
        std::max(20.0f, btn_cancel_rect_.y - kPopupActionsPadY - text_y - 24.0f);
    rt.draw_text(utf::utf8_to_utf16(profile_name_),
                 {card_rect_.x + pad, text_y, inner_w, max_h}, nts);
    if (ITextEngine* te = text_engine()) {
      text_y += std::max(20.0f,
                         te->measure_text(utf::utf8_to_utf16(profile_name_), nts, inner_w).h + 4.0f) + 8.0f;
    } else {
      text_y += 30.0f;
    }
  }
  TextStyle mts;
  mts.size = 13.0f;
  mts.color = tokens.secondary_text;
  mts.wrap = true;
  rt.draw_text(tr("profiles.modals.confirmDelete.message"),
               {card_rect_.x + pad, text_y, inner_w,
                std::max(18.0f, btn_cancel_rect_.y - kPopupActionsPadY - kPopupContentPadY - text_y)},
               mts);

  rt.fill_rect({card_rect_.x, btn_cancel_rect_.y - kPopupActionsPadY, card_rect_.w, 1.0f},
               Brush{tokens.divider_color});

  auto paint_btn = [&](const RectF& orig_r, const char* ctype, bool outlined,
                       float fs, bool hover, bool pressed,
                       const std::wstring& label) {
    const auto bp = svc.button_paint(ctype, outlined);
    RectF r = orig_r;
    if (pressed) r.y += bp.translate_active;
    draw_button_skin(rt, r, bp, hover ? 1.0f : 0.0f, pressed);
    TextStyle bts;
    bts.size = fs;
    bts.weight = bp.weight;
    bts.color = bp.text;
    bts.align_h = AlignH::Center;
    bts.align_v = AlignV::Center;
    rt.draw_text(label, r, bts);
  };
  // src: layout-dialogs.tsx:676-695
  paint_btn(btn_cancel_rect_, "default", true, 12.5f, hover_cancel_, pressed_cancel_,
            tr("shared.actions.cancel"));
  paint_btn(btn_delete_rect_, "error", false, 13.0f, hover_delete_, pressed_delete_,
            tr("shared.actions.delete"));
}

bool DeleteProfileConfirmModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  const PointF pt = e.pos;

  if (e.type == MouseEventType::Leave) {
    hover_cancel_ = hover_delete_ = false;
    pressed_cancel_ = pressed_delete_ = false;
    invalidate();
    return true;
  }
  if (e.type == MouseEventType::Move) {
    const bool hc = btn_cancel_rect_.contains(pt.x, pt.y);
    const bool hd = btn_delete_rect_.contains(pt.x, pt.y);
    if (hc != hover_cancel_ || hd != hover_delete_) {
      hover_cancel_ = hc;
      hover_delete_ = hd;
      invalidate();
    }
    return true;
  }
  if (e.type == MouseEventType::Down) {
    pressed_cancel_ = btn_cancel_rect_.contains(pt.x, pt.y);
    pressed_delete_ = btn_delete_rect_.contains(pt.x, pt.y);
    invalidate();
    return true;
  }
  if (e.type == MouseEventType::Up) {
    const bool was_c = pressed_cancel_;
    const bool was_d = pressed_delete_;
    pressed_cancel_ = pressed_delete_ = false;
    invalidate();
    if (was_c && btn_cancel_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (was_d && btn_delete_rect_.contains(pt.x, pt.y)) {
      const std::string target_uid = uid_;
      close();
      if (on_confirm && !target_uid.empty()) on_confirm(target_uid);
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }
  return true;
}

bool DeleteProfileConfirmModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down) {
    if (e.key == VK_ESCAPE || e.key == 27) {
      close();
      return true;
    }
    if (e.key == VK_RETURN || e.key == 13) {
      const std::string target_uid = uid_;
      close();
      if (on_confirm && !target_uid.empty()) on_confirm(target_uid);
      return true;
    }
  }
  return false;
}

std::wstring DeleteProfileConfirmModal::a11y_default_name() const {
  return tr("profiles.modals.confirmDelete.title");
}

A11ySubItem DeleteProfileConfirmModal::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  if (idx == 0) {
    sub.role = A11yRole::Text;
    std::wstring msg = tr("profiles.modals.confirmDelete.message");
    if (!profile_name_.empty()) {
      msg = utf::utf8_to_utf16(profile_name_) + L": " + msg;
    }
    sub.name = msg;
    sub.local_bounds = card_rect_;
    return sub;
  }
  sub.role = A11yRole::Button;
  sub.can_invoke = enabled();
  if (idx == 1) {
    sub.name = tr("shared.actions.cancel");
    sub.local_bounds = btn_cancel_rect_;
  } else {
    sub.name = tr("shared.actions.delete");
    sub.local_bounds = btn_delete_rect_;
  }
  return sub;
}

bool DeleteProfileConfirmModal::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  if (idx == 1) {
    close();
    return true;
  }
  if (idx == 2) {
    const std::string target_uid = uid_;
    close();
    if (on_confirm && !target_uid.empty()) on_confirm(target_uid);
    return true;
  }
  return false;
}

}  // namespace clashwin::ui::scene
