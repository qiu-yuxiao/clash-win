#include "utility_modals.h"

#include <windows.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

#include "ui/card_draw.h"
#include "ui/soft_shadow.h"
#include "ui/theme/theme.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01
#include "events/notice.h"

namespace clashwin::ui::scene {

using theme::ThemeService;

namespace {

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
  oss << std::put_time(&tm, "%H:%M:%S");
  return oss.str();
}

// ← style-helpers.tsx:221-225 formatCoreVersion：去前缀 v/V 后加 "Ver."
std::string format_core_version(const std::string& version) {
  if (version.empty()) return "";
  size_t i = 0;
  while (i < version.size() && (version[i] == 'v' || version[i] == 'V')) ++i;
  return "Ver." + version.substr(i);
}

}  // namespace

// ==========================================
// 1. EditProfileModal
// ==========================================
EditProfileModal::EditProfileModal(Widget* parent) : Widget(parent) {
  set_z_order(960);
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
  invalidate();
}

void EditProfileModal::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();  // 画面延后 195ms 消失，交互立刻失效
  invalidate();
  if (on_dismiss) on_dismiss();
}

void EditProfileModal::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: layout-dialogs.tsx:124-137 paper p3 / width calc(100%-32px) / maxWidth 450
  const float cw = std::min(450.0f, std::max(240.0f, area.w - 32.0f));
  const float ch = 360.0f;
  const float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  const float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  // p24；标题 h6 20 + mb2 16；字段 gap2 16 / label 20 + mb.5 4 / 输入 40
  // 按钮 h28 px16、右对齐、gap1.5=12，顺序 取消→保存（src: :221-244）
  const float pad = 24.0f;
  name_input_rect_ = {cx + pad, cy + 84.0f, cw - pad * 2, 40.0f};
  url_input_rect_ = {cx + pad, cy + 164.0f, cw - pad * 2, 40.0f};
  interval_input_rect_ = {cx + pad, cy + 244.0f, cw - pad * 2, 40.0f};

  const float bw = 56.0f, bh = 28.0f, gap = 12.0f, by = cy + 308.0f;
  btn_save_rect_ = {cx + cw - pad - bw, by, bw, bh};
  btn_cancel_rect_ = {cx + cw - pad - bw - gap - bw, by, bw, bh};
}

void EditProfileModal::on_paint(IRenderTarget& rt) {
  // src: layout-dialogs.tsx:119-245
  //   paper p3(24) / width calc(100%-32px) maxWidth 450 / radius 8 /
  //   border 1px rgba(255,255,255,.12) / theme-panel
  //   标题 h6(20) bold mb2(16)；字段 label body2(14) bold mb.5(4)、gap2(16)；
  //   输入 get3DInputStyle outline 2px；mb3(24)；按钮 h28 px16 fs12，
  //   右对齐 gap1.5(12)，顺序 取消→保存
  if (fade_.advance()) invalidate();  // 自续帧，直到过渡结束
  set_opacity(fade_.value());        // 每帧同步整棵子树的不透明度
  if (fade_.hidden()) return;

  const bool light = ThemeService::instance().is_light();
  const auto& pal = ThemeService::instance().palette();
  const auto& vars = ThemeService::instance().vars();

  // 1. MUI 缺省遮罩 rgba(0,0,0,.5)
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  // 2. 卡片底座（theme-panel：6 套皮肤查表）
  // src: layout-dialogs.tsx:119-137 paper className='theme-panel' + radius 8
  //   + border 1px rgba(255,255,255,.12)
  //   → 但 index.scss:108-115 对 .MuiDialog-paper.theme-panel 强制纯色底
  {
    const auto pp = ThemeService::instance().panel_paint();
    if (pp.has_shadow)
      draw_soft_shadow(rt, card_rect_, pp.radius, pp.shadow_color,
                       pp.shadow_dy, pp.shadow_blur);
    if (pp.has_glow)
      draw_glow(rt, card_rect_, pp.radius, pp.glow_color, pp.glow_blur);
    if (pp.grad_bg) {
      rt.fill_round_rect_gradient_angle(card_rect_, pp.radius, pp.grad,
                                        pp.grad_count, pp.grad_angle);
    } else if (pp.bg.a > 0.001f) {
      Brush b{pp.bg};
      rt.round_rect(card_rect_, pp.radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &b);
    }
    if (pp.border != theme::PanelPaint::BorderKind::None) {
      Color4f bc = pp.border_color;
      if (pp.pulse) {
        const float now_s = static_cast<float>(GetTickCount64()) / 1000.0f;
        bc = Color4f::lerp(pp.pulse_from, pp.pulse_to,
                           ThemeService::instance().pulse_phase(now_s));
      }
      Pen border{bc, 1.34f};
      rt.round_rect(card_rect_, pp.radius, border, nullptr);
      const float inner_r = pp.radius > 3.0f ? pp.radius - 3.0f : 0.0f;
      rt.round_rect({card_rect_.x + 3, card_rect_.y + 3, card_rect_.w - 6,
                     card_rect_.h - 6},
                    inner_r, border, nullptr);
    }
  }

  // 3. 标题（h6 20 bold）
  TextStyle title_ts;
  title_ts.size = 20.0f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = pal.primary_text;
  title_ts.align_v = AlignV::Center;
  rt.draw_text(L"⚙️ " + tr("layout.components.dialogs.editProfile.title"),
               {card_rect_.x + 24.0f, card_rect_.y + 24.0f, card_rect_.w - 48.0f, 20.0f},
               title_ts);

  // 4. 字段 label（body2 14 bold mb.5）
  TextStyle label_ts;
  label_ts.size = 14.0f;
  label_ts.weight = static_cast<int>(Weight::Bold);
  label_ts.color = pal.primary_text;
  label_ts.align_v = AlignV::Center;
  rt.draw_text(tr("layout.components.dialogs.editProfile.nameLabel"),
               {card_rect_.x + 24.0f, card_rect_.y + 60.0f, card_rect_.w - 48.0f, 20.0f},
               label_ts);
  rt.draw_text(tr("layout.components.dialogs.editProfile.urlLabel"),
               {card_rect_.x + 24.0f, card_rect_.y + 140.0f, card_rect_.w - 48.0f, 20.0f},
               label_ts);
  rt.draw_text(tr("layout.components.dialogs.editProfile.intervalLabel"),
               {card_rect_.x + 24.0f, card_rect_.y + 220.0f, card_rect_.w - 48.0f, 20.0f},
               label_ts);

  // 5. 输入框（get3DInputStyle：6 套皮肤查表）
  TextStyle val_ts;
  val_ts.size = 14.0f;
  val_ts.align_v = AlignV::Center;
  val_ts.ellipsis = true;
  // src: layout-dialogs.tsx:164-216 各 TextField sx={get3DInputStyle(theme)}
  const theme::InputPaint ip = ThemeService::instance().input_paint();
  // src: MUI .Mui-disabled { opacity: 0.38 } —— 整控件（底/描边/文字）统一叠乘
  const float kDisabledOpacity = 0.38f;

  auto fade = [](Color4f c, float k) {
    c.a *= k;
    return c;
  };
  auto draw_field = [&](const RectF& r, const std::wstring& text, int field_idx, bool off) {
    Brush bg{off ? fade(ip.bg, kDisabledOpacity) : ip.bg};
    const Pen pen{off ? fade(ip.border, kDisabledOpacity)
                      : (focused_field_ == field_idx ? ip.border_focused : ip.border),
                  off ? ip.border_w : (focused_field_ == field_idx
                                           ? ip.border_w_focused
                                           : ip.border_w)};
    rt.round_rect(r, ip.radius, pen, &bg);
    TextStyle ts = val_ts;
    ts.color = off ? fade(ip.text, kDisabledOpacity) : ip.text;
    rt.draw_text(text, {r.x + 10.0f, r.y, r.w - 20.0f, r.h}, ts);
  };

  std::wstring nw = utf::utf8_to_utf16(name_);
  if (focused_field_ == 1) nw += L"|";
  draw_field(name_input_rect_, nw, 1, false);

  std::wstring uw = utf::utf8_to_utf16(url_);
  if (focused_field_ == 2) uw += L"|";
  draw_field(url_input_rect_, uw, 2, is_local_);

  std::wstring iw = std::to_wstring(interval_mins_);
  if (focused_field_ == 3) iw += L"|";
  draw_field(interval_input_rect_, iw, 3, is_local_);

  // 6. 按钮（h28 / fs12 / 取消→保存）
  // src: layout-dialogs.tsx:220-243 取消 defaultBtn3DStyle、保存 primaryBtn3DStyle，
  //   两者都是 get3DButtonStyle(theme,'contained', ...) → 6 套皮肤查表
  TextStyle btn_ts;
  btn_ts.size = 12.0f;
  btn_ts.align_h = AlignH::Center;
  btn_ts.align_v = AlignV::Center;

  auto paint_btn = [&](const RectF& r, const char* ctype, bool hover,
                       const std::wstring& label) {
    const auto bp = ThemeService::instance().button_paint(ctype);
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
    btn_ts.weight = bp.weight;
    btn_ts.color = bp.text;
    rt.draw_text(label, r, btn_ts);
  };
  paint_btn(btn_cancel_rect_, "default", hover_cancel_,
            tr("shared.actions.cancel"));
  paint_btn(btn_save_rect_, "primary", hover_save_,
            tr("shared.actions.save"));
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
      if (on_save) on_save(name_, url_, interval_mins_);
      close();
      return true;
    }
    if (name_input_rect_.contains(pt.x, pt.y)) {
      focused_field_ = 1;
      invalidate();
      return true;
    }
    if (!is_local_ && url_input_rect_.contains(pt.x, pt.y)) {
      focused_field_ = 2;
      invalidate();
      return true;
    }
    if (!is_local_ && interval_input_rect_.contains(pt.x, pt.y)) {
      focused_field_ = 3;
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
      if (on_save) on_save(name_, url_, interval_mins_);
      close();
      return true;
    }
    if (e.key == VK_BACK) {
      if (focused_field_ == 1 && !name_.empty()) {
        name_.pop_back();
        invalidate();
        return true;
      }
      if (focused_field_ == 2 && !url_.empty()) {
        url_.pop_back();
        invalidate();
        return true;
      }
      if (focused_field_ == 3) {
        interval_mins_ /= 10;
        invalidate();
        return true;
      }
    }
  } else if (e.type == KeyEventType::Char) {
    if (focused_field_ == 1 && e.ch >= 32) {
      name_ += utf::utf16_to_utf8(std::wstring(1, e.ch));
      invalidate();
      return true;
    }
    if (focused_field_ == 2 && e.ch >= 32 && e.ch < 127) {
      url_.push_back(static_cast<char>(e.ch));
      invalidate();
      return true;
    }
    if (focused_field_ == 3 && e.ch >= '0' && e.ch <= '9') {
      interval_mins_ = interval_mins_ * 10 + (e.ch - '0');
      if (interval_mins_ > 100000) interval_mins_ = 100000;
      invalidate();
      return true;
    }
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
  progress_ = 0;
  status_ = "idle";
  is_downloading_ = false;
  status_msg_.clear();
  hover_close_x_ = false;
  hover_close_ = false;
  hover_update_ = false;
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
  const float cw = 420.0f;
  const float ch = 340.0f;
  float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};  // 保留成员（Mini 无 ✕，不绘制）
  changelog_box_rect_ = {cx + 24.0f, cy + 124.0f, cw - 48.0f, 120.0f};
  // ← layout-dialogs.tsx:343 bar 高 8px（radius 4）
  progress_bar_rect_ = {cx + 24.0f, cy + 252.0f, cw - 48.0f, 8.0f};

  btn_close_rect_ = {cx + cw - 180.0f, cy + ch - 44.0f, 76.0f, 30.0f};
  btn_update_rect_ = {cx + cw - 96.0f, cy + ch - 44.0f, 82.0f, 30.0f};
}

void ClientUpdateModal::on_paint(IRenderTarget& rt) {
  // 1:1 对齐 layout-dialogs.tsx:247-410 Client Update Dialog：
  //   MUI Dialog paper（theme-panel 类、radius 8）+ h6 标题 + body2 版本两行 +
  //   changelog 框（13px pre-wrap）+ 进度条（8px，状态≠idle 时显示）+
  //   关闭（outlined）/立即更新（contained primary）按钮（downloading 时禁用）。
  if (fade_.advance()) invalidate();  // 自续帧，直到过渡结束
  set_opacity(fade_.value());        // 每帧同步整棵子树的不透明度
  if (fade_.hidden()) return;

  // src: node_modules/@mui/material/Backdrop/Backdrop.js:48
  //   MUI v5 Dialog Backdrop 缺省 rgba(0,0,0,.5)；Mini 业务源码未覆写 → 取框架缺省
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  // 豁免: Mini 无对应物 — D2D 无 box-shadow 原语；MUI Dialog paper elevation:24
  //   （node_modules/@mui/material/Dialog/Dialog.js:343）阴影由 CSS 运行时生成，
  //   此处以 0.22 alpha 软阴影等效（出处: 无）
  Brush shadow_b{Color4f(0, 0, 0, 0.22f)};
  rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5}, 9.0f,
                {}, &shadow_b);

  // theme-panel 卡底（← _theme.tsx paper.background）
  const auto& vars = ThemeService::instance().vars();
  Brush card_bg{vars.theme_bg};
  rt.round_rect(card_rect_, 8.0f, {Color4f(1, 1, 1, 0.12f), 1.0f}, &card_bg);

  // 标题（MUI DialogContentText h6 = 1.25rem = 20px bold）
  TextStyle title_ts;
  title_ts.size = 20.0f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = ThemeService::instance().text_primary();
  title_ts.align_v = AlignV::Center;
  // src: locales/zh/layout.json:58 「"title": "软件本体更新"」（layout-dialogs.tsx:280 🚀）
  rt.draw_text(L"🚀 " + tr("layout.components.dialogs.client.title"),
               {card_rect_.x + 24.0f, card_rect_.y + 14.0f, card_rect_.w - 48.0f, 28.0f},
               title_ts);

  // 版本信息两行（body2 = 14px；最新版本行 bold；v 前缀 ← layout-dialogs.tsx:288-306）
  const std::wstring unknown = tr("layout.components.dialogs.update.unknown");
  TextStyle ver_ts;
  ver_ts.size = 14.0f;
  ver_ts.color = ThemeService::instance().text_primary();
  ver_ts.align_v = AlignV::Center;
  rt.draw_text(tr("layout.components.dialogs.update.currentVersion",
                  {{"version", current_version_.empty() ? utf::utf16_to_utf8(unknown)
                                                       : current_version_}}),
               {card_rect_.x + 24.0f, card_rect_.y + 48.0f, card_rect_.w - 48.0f, 20.0f},
               ver_ts);
  TextStyle ver_bold = ver_ts;
  ver_bold.weight = static_cast<int>(Weight::Bold);
  rt.draw_text(tr("layout.components.dialogs.update.latestVersion",
                  {{"version", latest_version_.empty() ? utf::utf16_to_utf8(unknown)
                                                       : latest_version_}}),
               {card_rect_.x + 24.0f, card_rect_.y + 72.0f, card_rect_.w - 48.0f, 20.0f},
               ver_bold);

  TextStyle chg_label = ver_bold;
  // src: locales/zh/layout.json:59 「"更新日志:"」（layout-dialogs.tsx:318）
  rt.draw_text(tr("layout.components.dialogs.client.changelog"),
               {card_rect_.x + 24.0f, card_rect_.y + 100.0f, card_rect_.w - 48.0f, 20.0f},
               chg_label);

  // 更新日志框（13px pre-wrap；← layout-dialogs.tsx:320-336）
  const bool light = ThemeService::instance().is_light();
  Brush cl_bg{light ? Color4f::hex(0x000000, 0.04f) : Color4f::hex(0xFFFFFF, 0.06f)};
  rt.round_rect(changelog_box_rect_, 4.0f, {vars.theme_border, 1.0f}, &cl_bg);

  TextStyle cl_ts;
  cl_ts.size = 13.0f;
  cl_ts.color = ThemeService::instance().text_primary();
  cl_ts.wrap = true;
  // src: locales/zh/layout.json:60 「"暂无详细更新日志"」（layout-dialogs.tsx:335）
  std::wstring chg_w = changelog_.empty() ? tr("layout.components.dialogs.client.changelogEmpty") : utf::utf8_to_utf16(changelog_);
  rt.push_clip(changelog_box_rect_);
  rt.draw_text(chg_w, {changelog_box_rect_.x + 16.0f, changelog_box_rect_.y + 16.0f,
                       changelog_box_rect_.w - 32.0f, changelog_box_rect_.h - 32.0f}, cl_ts);
  rt.pop_clip();

  // 进度条与状态（仅 status ≠ idle 显示；← layout-dialogs.tsx:338-373）
  if (status_ != "idle") {
    // bar：8px 高、radius 4、底 10% 黑/白、填充 var(--primary-main)
    Brush bar_bg{light ? Color4f::hex(0x000000, 0.1f) : Color4f::hex(0xFFFFFF, 0.1f)};
    rt.round_rect(progress_bar_rect_, 4.0f, {}, &bar_bg);
    float fill_w = progress_bar_rect_.w * (static_cast<float>(progress_) / 100.0f);
    Brush fill_b{ThemeService::instance().primary()};
    rt.round_rect({progress_bar_rect_.x, progress_bar_rect_.y, fill_w, progress_bar_rect_.h},
                  4.0f, {}, &fill_b);

    TextStyle stat_ts;
    stat_ts.size = 12.0f;
    stat_ts.color = ThemeService::instance().text_primary();
    stat_ts.color.a *= 0.8f;  // opacity 0.8
    // 空消息兜底 = clientDownloading（点击后的初始文案）
    std::wstring stat_str = status_msg_.empty()
                                ? tr("layout.components.dialogs.progress.clientDownloading")
                                : utf::utf8_to_utf16(status_msg_);
    rt.draw_text(stat_str, {progress_bar_rect_.x, progress_bar_rect_.y + 16.0f,
                            progress_bar_rect_.w, 18.0f}, stat_ts);
  }

  // 底部按钮（关闭 outlined 禁用于 downloading；更新 contained primary）
  const bool busy = is_downloading_;
  auto paint_themed_btn = [&](IRenderTarget& rt2, const RectF& r, bool hovered,
                              bool disabled, const char* color_type,
                              const std::wstring& label) {
    auto p = ThemeService::instance().button_paint(color_type);
    Color4f fill = disabled ? p.fill_disabled
                            : (hovered ? p.fill_hover : p.fill);
    Brush b{fill};
    rt2.round_rect(r, p.radius, {p.border, p.border_w}, &b);
    TextStyle ts;
    ts.size = 12.0f;  // MUI Button body2
    ts.weight = p.weight;
    ts.color = disabled ? p.text_disabled : p.text;
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt2.draw_text(label, r, ts);
  };
  paint_themed_btn(rt, btn_close_rect_, hover_close_, busy, "default",
                   tr("shared.actions.close"));

  // 标签逻辑（← layout-dialogs.tsx:400-407）：done→准备重启；downloading→更新中...；
  // 同版本→已是最新；否则→立即更新
  std::wstring upd_label;
  if (status_ == "done")
    upd_label = tr("layout.components.dialogs.client.prepareRestart");
  else if (busy)
    upd_label = tr("layout.components.dialogs.update.updating");
  else if (!latest_version_.empty() && latest_version_ == current_version_)
    upd_label = tr("layout.components.dialogs.update.upToDate");
  else
    upd_label = tr("layout.components.dialogs.update.updateNow");
  paint_themed_btn(rt, btn_update_rect_, hover_update_, busy, "primary", upd_label);
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

  if (e.type == MouseEventType::Up) {
    if (btn_close_rect_.contains(pt.x, pt.y)) {
      // src: layout-dialogs.tsx:250-254 仅非 downloading 时 onClose 生效
      if (!is_downloading_) close();
      return true;
    }
    if (btn_update_rect_.contains(pt.x, pt.y)) {
      // src: use-client-update.ts:56-62 无更新对象/已是新版 → 不发起下载
      if (download_url_.empty() || is_downloading_ || progress_ >= 100) return true;
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

void CoreUpdateModal::open(const std::string& current_ver, const std::string& latest_ver) {
  open_ = true;
  fade_.show();
  current_version_ = current_ver;
  latest_version_ = latest_ver;
  progress_ = 0;
  status_ = "idle";
  message_.clear();
  hover_close_x_ = false;
  hover_close_ = false;
  hover_upgrade_ = false;
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
  const float cw = 400.0f;
  const float ch = 280.0f;
  float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};  // 保留成员（Mini 无 ✕，不绘制）
  // ← layout-dialogs.tsx:484 bar 高 8px
  progress_bar_rect_ = {cx + 24.0f, cy + 148.0f, cw - 48.0f, 8.0f};

  btn_close_rect_ = {cx + cw - 180.0f, cy + ch - 44.0f, 76.0f, 30.0f};
  btn_upgrade_rect_ = {cx + cw - 96.0f, cy + ch - 44.0f, 82.0f, 30.0f};
}

void CoreUpdateModal::on_paint(IRenderTarget& rt) {
  // 1:1 对齐 layout-dialogs.tsx:412-553 Core Update Dialog（「Mini 无对应物」
  // 的旧注释有误：Mini 具备本对话框）。MUI Dialog paper + h6 标题 + body2
  // 版本两行 + 进度条（状态≠idle）+ 关闭/升级按钮（busy 时禁用）。
  if (fade_.advance()) invalidate();  // 自续帧，直到过渡结束
  set_opacity(fade_.value());        // 每帧同步整棵子树的不透明度
  if (fade_.hidden()) return;

  const auto& vars = ThemeService::instance().vars();
  // src: node_modules/@mui/material/Backdrop/Backdrop.js:48
  //   MUI v5 Dialog Backdrop 缺省 rgba(0,0,0,.5)；Mini 业务源码未覆写 → 取框架缺省
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  // 豁免: Mini 无对应物 — D2D 无 box-shadow 原语；MUI Dialog paper elevation:24
  //   （node_modules/@mui/material/Dialog/Dialog.js:343）阴影由 CSS 运行时生成，
  //   此处以 0.22 alpha 软阴影等效（出处: 无）
  Brush shadow_b{Color4f(0, 0, 0, 0.22f)};
  rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5}, 9.0f,
                {}, &shadow_b);

  Brush card_bg{vars.theme_bg};
  // src: pages/_layout/components/layout-dialogs.tsx:131-132（paper borderRadius 8px
  //      + border 1px solid rgba(255,255,255,.12)；同名定义见 :264-265 / :433-434）
  rt.round_rect(card_rect_, 8.0f, {Color4f(1, 1, 1, 0.12f), 1.0f}, &card_bg);

  // 标题（h6 = 20px bold）
  TextStyle title_ts;
  title_ts.size = 20.0f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = ThemeService::instance().text_primary();
  title_ts.align_v = AlignV::Center;
  // src: locales/zh/layout.json:64 「"title": "Mihomo 内核更新"」（layout-dialogs.tsx:449 ⚙️）
  rt.draw_text(L"⚙️ " + tr("layout.components.dialogs.core.title"),
               {card_rect_.x + 24.0f, card_rect_.y + 14.0f, card_rect_.w - 48.0f, 28.0f},
               title_ts);

  // 版本信息两行（body2 = 14px；最新版本行 bold；formatCoreVersion ← style-helpers.tsx:221）
  TextStyle ver_ts;
  ver_ts.size = 14.0f;
  ver_ts.color = ThemeService::instance().text_primary();
  ver_ts.align_v = AlignV::Center;
  rt.draw_text(tr("layout.components.dialogs.update.currentVersion",
                  {{"version", format_core_version(current_version_)}}),
               {card_rect_.x + 24.0f, card_rect_.y + 48.0f, card_rect_.w - 48.0f, 20.0f},
               ver_ts);
  // src: layout.json:51/65 「最新版本: {{version}} / 获取中...」（layout-dialogs.tsx:471-474）
  TextStyle ver_bold = ver_ts;
  ver_bold.weight = static_cast<int>(Weight::Bold);
  rt.draw_text(tr("layout.components.dialogs.update.latestVersion",
                  {{"version", latest_version_.empty()
                                   ? utf::utf16_to_utf8(
                                         tr("layout.components.dialogs.core.fetching"))
                                   : format_core_version(latest_version_)}}),
               {card_rect_.x + 24.0f, card_rect_.y + 72.0f, card_rect_.w - 48.0f, 20.0f},
               ver_bold);

  // 升级进度条（仅 status ≠ idle；8px/radius4/底 10% 黑白/填充 primary ← :479-514）
  const bool light = ThemeService::instance().is_light();
  if (status_ != "idle") {
    Brush bar_bg{light ? Color4f::hex(0x000000, 0.1f) : Color4f::hex(0xFFFFFF, 0.1f)};
    rt.round_rect(progress_bar_rect_, 4.0f, {}, &bar_bg);
    float fill_w = progress_bar_rect_.w * (static_cast<float>(progress_) / 100.0f);
    Brush fill_b{ThemeService::instance().primary()};
    rt.round_rect({progress_bar_rect_.x, progress_bar_rect_.y, fill_w, progress_bar_rect_.h},
                  4.0f, {}, &fill_b);

    // 状态说明（12px ×0.8；消息直接来自后端 payload，无自造兜底）
    TextStyle msg_ts;
    msg_ts.size = 12.0f;
    msg_ts.color = ThemeService::instance().text_primary();
    msg_ts.color.a *= 0.8f;
    std::wstring display_msg = utf::utf8_to_utf16(message_);
    rt.draw_text(display_msg, {progress_bar_rect_.x, progress_bar_rect_.y + 16.0f,
                               progress_bar_rect_.w, 20.0f}, msg_ts);
  }

  // 底部按钮（关闭 outlined 禁用于 busy；升级 contained primary）
  const bool busy = is_upgrading_ || status_ == "checking" || status_ == "downloading" ||
                    status_ == "extracting";
  auto paint_themed_btn = [&](IRenderTarget& rt2, const RectF& r, bool hovered,
                              bool disabled, const char* color_type,
                              const std::wstring& label) {
    auto p = ThemeService::instance().button_paint(color_type);
    Color4f fill = disabled ? p.fill_disabled
                            : (hovered ? p.fill_hover : p.fill);
    Brush b{fill};
    rt2.round_rect(r, p.radius, {p.border, p.border_w}, &b);
    TextStyle ts;
    ts.size = 12.0f;  // MUI Button body2
    ts.weight = p.weight;
    ts.color = disabled ? p.text_disabled : p.text;
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt2.draw_text(label, r, ts);
  };
  paint_themed_btn(rt, btn_close_rect_, hover_close_, busy, "default",
                   tr("shared.actions.close"));

  // 标签逻辑（← layout-dialogs.tsx:544-550）：done→更新完成；busy→更新中...；
  // 否则→立即更新（isSameVersion 分支在 dialog 打开前提下不可达）
  std::wstring upd_label;
  if (status_ == "done")
    upd_label = tr("layout.components.dialogs.core.updateDone");
  else if (busy)
    upd_label = tr("layout.components.dialogs.update.updating");
  else
    upd_label = tr("layout.components.dialogs.update.updateNow");
  paint_themed_btn(rt, btn_upgrade_rect_, hover_upgrade_, busy, "primary", upd_label);
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
      if (status_ != "done") {
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
  last_flush_time_ = std::chrono::steady_clock::now();
  // src: logs.tsx:179-191 BaseStyledSelect 5 项
  //   （shared.json:135-141 ALL/DEBUG/INFO/WARN/ERROR）
  auto menu = std::make_unique<MenuList>();
  lvl_menu_ = menu.get();
  {
    const wchar_t* labels[5] = {L"ALL", L"DEBUG", L"INFO", L"WARN", L"ERROR"};
    for (auto* l : labels) {
      MenuItem it;
      it.label = l;
      menu->items.push_back(it);
    }
    menu->item_height = 28;
    menu->min_width_ = 140;
    menu->set_visible(false);
    menu->on_select = [this](int i) {
      static const char* vals[5] = {"all", "debug", "info", "warn", "error"};
      level_filter_ = vals[i];
      if (lvl_menu_) {
        for (int k = 0; k < 5; ++k) lvl_menu_->items[k].selected = (k == i);
        lvl_menu_->set_visible(false);
      }
      invalidate();
    };
  }
  add_child(std::move(menu));
}

void LogViewerModal::open() {
  open_ = true;
  fade_.show();
  flush_incoming();
  hover_close_x_ = false;
  hover_pause_ = false;
  hover_order_ = false;
  hover_clear_ = false;
  invalidate();
}

void LogViewerModal::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();  // 画面延后 195ms 消失，交互立刻失效
  invalidate();
  if (on_dismiss) on_dismiss();
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
    for (auto& item : incoming_queue_) {
      logs_.push_back(std::move(item));
      // 1000 行环形截断 (FIFO clamp)
      if (logs_.size() > kMaxLogLines) {
        logs_.pop_front();
      }
    }
  }
  incoming_queue_.clear();
  invalidate();
}

void LogViewerModal::clear_logs() {
  logs_.clear();
  incoming_queue_.clear();
  scroll_y_ = 0;
  invalidate();
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
    if (entry.message.find(search_query_) == std::string::npos) {
      return false;
    }
  }
  return true;
}

void LogViewerModal::on_arrange(const RectF& area) {
  set_bounds(area);
  const float cw = std::min(600.0f, area.w - 20.0f);
  const float ch = std::min(440.0f, area.h - 20.0f);
  float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};

  toolbar_rect_ = {cx + 14.0f, cy + 42.0f, cw - 28.0f, 32.0f};
  btn_pause_rect_ = {toolbar_rect_.x, toolbar_rect_.y, 48.0f, 30.0f};
  btn_order_rect_ = {toolbar_rect_.x + 52.0f, toolbar_rect_.y, 48.0f, 30.0f};
  btn_clear_rect_ = {toolbar_rect_.x + 104.0f, toolbar_rect_.y, 54.0f, 30.0f};
  seg_level_rect_ = {toolbar_rect_.x + 164.0f, toolbar_rect_.y, 90.0f, 30.0f};
  search_box_rect_ = {seg_level_rect_.x + seg_level_rect_.w + 8.0f, toolbar_rect_.y,
                      std::max(60.0f, toolbar_rect_.x + toolbar_rect_.w - (seg_level_rect_.x + seg_level_rect_.w + 8.0f)), 30.0f};

  log_area_rect_ = {cx + 14.0f, cy + 82.0f, cw - 28.0f, ch - 96.0f};
}

void LogViewerModal::on_paint(IRenderTarget& rt) {
  // Mini 的日志为 MUI Dialog（src/pages/_layout/components/layout-dialogs.tsx:556），
  // 默认 Backdrop = rgba(0,0,0,0.5)（@mui/material/Backdrop/Backdrop.js:48）；
  // 点遮罩/按 Esc 均关闭（layout-dialogs.tsx:558 `onClose={() => setLogsOpen(false)}`）。
  if (fade_.advance()) invalidate();  // 自续帧，直到过渡结束
  set_opacity(fade_.value());        // 每帧同步整棵子树的不透明度
  if (fade_.hidden()) return;

  // 豁免: Mini 无对应物 — 本查看器为 ClashWin 自加的终端风日志查看器，
  //   Mini 日志页无终端配色字面量（出处: layout-dialogs.tsx:565-594 无终端配色）。
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  Brush shadow_b{Color4f(0, 0, 0, 0.22f)};
  rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5}, 9.0f,
                {}, &shadow_b);

  // ★ 卡片底座走主题（theme-panel 查表，与 EditProfileModal 同构）：
  //   src: layout-dialogs.tsx:556-594 Dialog paper className='theme-panel'
  //   + index.scss:108-115（theme-panel 被强制为纯色面板基色）
  //   此前写死 #FFFFFF 白底 + #1A1A1A 深字 → 暗色模式下是刺眼的白卡。
  {
    const auto& svc = ThemeService::instance();
    const auto& pal = svc.palette();
    const auto& vars = svc.vars();
    const auto pp = svc.panel_paint();
    if (pp.has_shadow)
      draw_soft_shadow(rt, card_rect_, pp.radius, pp.shadow_color, pp.shadow_dy,
                       pp.shadow_blur);
    if (pp.has_glow)
      draw_glow(rt, card_rect_, pp.radius, pp.glow_color, pp.glow_blur);
    if (pp.grad_bg) {
      rt.fill_round_rect_gradient_angle(card_rect_, pp.radius, pp.grad,
                                        pp.grad_count, pp.grad_angle);
    } else if (pp.bg.a > 0.001f) {
      Brush b{pp.bg};
      rt.round_rect(card_rect_, pp.radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &b);
    }
    if (pp.border != theme::PanelPaint::BorderKind::None) {
      Pen border{pp.border_color, 1.34f};
      rt.round_rect(card_rect_, pp.radius, border, nullptr);
    }

  // 标题
  TextStyle title_ts;
  title_ts.size = 13.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = pal.primary_text;
  title_ts.align_v = AlignV::Center;
  // src: locales/zh/logs.json:3 「"title": "日志"」（logs.tsx:106 BasePage title）
  rt.draw_text(tr("logs.page.title"), {card_rect_.x + 18.0f, card_rect_.y + 8.0f, 200.0f, 24.0f},
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

  rt.fill_rect({card_rect_.x, card_rect_.y + 36.0f, card_rect_.w, 1.0f},
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
    ts.size = 10.5f;
    ts.color = active ? warn_c : pal.primary_text;
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt.draw_text(label, r, ts);
  };

  // src: logs.tsx:126-131/:147-152 IconButton 仅图标，文案在 Tooltip
  draw_tool_btn(btn_pause_rect_, paused_ ? L"▶" : L"⏸", hover_pause_, paused_);
  draw_tool_btn(btn_order_rect_, desc_ ? L"▼" : L"▲", hover_order_, desc_);
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

  // 搜索框（走主题输入框变量 → 随皮肤/亮暗）
  Brush sb{vars.theme_input_bg};
  rt.round_rect(search_box_rect_, 4.0f, {vars.theme_input_border, 1.0f}, &sb);
  TextStyle search_ts;
  search_ts.size = 10.0f;
  search_ts.color =
      search_query_.empty() ? pal.secondary_text : pal.primary_text;
  search_ts.align_v = AlignV::Center;
  // src: locales/zh/shared.json:65 「"filter": "过滤条件"」（base-search-box.tsx:117）
  std::wstring s_text = search_query_.empty() ? tr("shared.placeholders.filter") : utf::utf8_to_utf16(search_query_);
  rt.draw_text(s_text, {search_box_rect_.x + 6.0f, search_box_rect_.y, search_box_rect_.w - 12.0f,
                        search_box_rect_.h}, search_ts);
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
  log_ts.wrap = false;
  log_ts.ellipsis = true;
  const auto& pal = theme::ThemeService::instance().palette();

  for (size_t i = 0; i < visible_entries.size(); ++i) {
    if (y + line_height_ >= log_area_rect_.y && y <= log_area_rect_.y + log_area_rect_.h) {
      const auto* e = visible_entries[i];

      // src: log-item.tsx:16-35 时间=text.secondary；级别 type=error/warning/info
      //   主色；正文 data=text.primary（逐段着色，无整行终端色）
      const std::wstring t_time = L"[" + utf::utf8_to_utf16(e->time) + L"] ";
      const std::wstring t_type = L"[" + utf::utf8_to_utf16(e->level) + L"] ";
      const std::wstring t_msg = utf::utf8_to_utf16(e->message);
      Color4f type_c = (e->level == "error" || e->level == "err")
                           ? pal.error_color
                           : ((e->level == "warn" || e->level == "warning")
                                  ? pal.warning_color
                                  : pal.info_color);
      float x = log_area_rect_.x + 8.0f;
      log_ts.color = pal.secondary_text;
      rt.draw_text(t_time, {x, y, log_area_rect_.w - 16.0f, line_height_}, log_ts);
      x += text_engine()->measure_text(t_time, log_ts, 0).w;
      log_ts.color = type_c;
      rt.draw_text(t_type, {x, y, log_area_rect_.w - 16.0f, line_height_}, log_ts);
      x += text_engine()->measure_text(t_type, log_ts, 0).w;
      log_ts.color = pal.primary_text;
      rt.draw_text(t_msg, {x, y, log_area_rect_.x + log_area_rect_.w - x - 8.0f,
                           line_height_}, log_ts);
    }
    y += line_height_;
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
    bool hc = btn_clear_rect_.contains(pt.x, pt.y);
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
    float delta = static_cast<float>(e.wheel_delta) / 120.0f * line_height_ * 3.0f;
    scroll_y_ = std::max(0.0f, scroll_y_ - delta);
    invalidate();
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
          const float menu_w = 100.0f;
          const float menu_h = 5.0f * lvl_menu_->item_height + 4.0f;
          float menu_x = std::max(card_rect_.x + 8.0f,
                                  std::min(seg_level_rect_.x, card_rect_.x + card_rect_.w - menu_w - 8.0f));
          float menu_y = (seg_level_rect_.bottom() + 2.0f + menu_h <= card_rect_.y + card_rect_.h - 8.0f)
                             ? (seg_level_rect_.bottom() + 2.0f)
                             : (seg_level_rect_.y - menu_h - 2.0f);
          lvl_menu_->layout_tree({menu_x, menu_y, menu_w, menu_h});
          lvl_menu_->min_width_ = menu_w;
          lvl_menu_->set_z_order(1000);
          MenuStyle st;
          apply_menu_skin(st);
          lvl_menu_->bg_ = st.bg;
          lvl_menu_->border_ = st.border;
          lvl_menu_->text_ = st.text;
          lvl_menu_->hover_bg_ = st.hover_bg;
          lvl_menu_->selected_bg_ = st.selected_bg;
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

}  // namespace clashwin::ui::scene
