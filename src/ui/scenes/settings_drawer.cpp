// 设置抽屉容器布局、面板背景、开关/端口/语言/右键菜单回调与状态同步
// （卡与持久化/HelpDialog 已拆分至 settings_cards.cpp / settings_skin_prefs.cpp；
//   src: src/pages/_layout/components/settings-drawer.tsx:1-200）
#include "settings_drawer.h"
#include "settings_drawer_internal.h"

#include <windows.h>
#include <shellapi.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>

#include "../../i18n/i18n.h"
#include "../breakpoints.h"
#include "../button_draw.h"
#include "../card_draw.h"
#include "../help_menu.h"
#include "../soft_shadow.h"
#include "../theme/theme.h"
#include "clashwin/config/constants.h"
#include "cmd/command_service.h"
#include "events/notice.h"
#include "ui/i18n_text.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"
#include "utility_modals.h"

namespace clashwin::ui::scene {

namespace utf = clashwin::utf;
using theme::ThemeService;

namespace {

// ---------- 几何常量（逻辑 DIP，全部来自源码） ----------
constexpr float kPad = 12.0f;                           // src: src/pages/_layout/components/settings-drawer.tsx
constexpr float kGap = 12.0f;                           // src: src/pages/_layout/components/settings-drawer.tsx
constexpr float kLeftW = 240.0f, kLeftGap = 5.0f;       // src: src/pages/_layout/components/settings-drawer.tsx
constexpr float kLeftPr = 8.0f;                         // src: src/pages/_layout/components/settings-drawer.tsx
constexpr float kCardW = kLeftW - kLeftPr - 1.0f;       // 231
constexpr float kPanelR = 8.0f;                         // src: src/assets/styles/index.scss:76
constexpr float kPanelBorder = 4.0f;                    // src: src/assets/styles/index.scss:125
constexpr float kCardPad = detail::kCardPad;
constexpr float kCloseSize = 28.0f, kCloseTop = 3.0f, kCloseRight = 8.0f;  // src: src/pages/_layout/components/settings-drawer.tsx

// src: src/pages/_layout/components/takeover-mode-card.tsx:51-76
constexpr float kComboH = 128.8f;
constexpr float kComboTakeoverH = 60.4f;
constexpr float kComboRoutingH = 52.4f;
// src: src/pages/_layout/components/basic-settings-card.tsx:154-530
constexpr float kBasicH = 146.2f;
// src: src/pages/_layout/components/theme-settings-card.tsx:148-476
constexpr float kThemeH = 166.3f;

Color4f mix_hover(const Color4f& a, const Color4f& b, float k) {
  return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k,
          a.a + (b.a - a.a) * k};
}

// src: src/pages/_layout/components/settings-drawer.tsx
class CloseButton : public IconW {
 public:
  CloseButton() = default;
  std::function<void(bool down)> on_press_state;
  bool on_mouse(const MouseEvent& e) override {
    if (e.type == MouseEventType::Down && on_press_state) on_press_state(true);
    if ((e.type == MouseEventType::Up || e.type == MouseEventType::Leave) &&
        on_press_state)
      on_press_state(false);
    return IconW::on_mouse(e);
  }
  void on_paint(IRenderTarget& rt) override {
    // src: src/utils/button-styles/button.ts:35
    const auto p = ThemeService::instance().button_paint("primary");
    const float ty = pressed_ ? p.translate_active : (hover_ ? p.translate_hover : 0.0f);
    RectF r{0.0f, ty, bounds().w, bounds().h};
    draw_button_skin(rt, r, p, hover_ ? 1.0f : 0.0f, pressed_);
    color_ = p.text;
    const PointF prev_o = rt.origin();
    if (ty != 0.0f) rt.set_origin({prev_o.x, prev_o.y + ty});
    IconW::on_paint(rt);
    if (ty != 0.0f) rt.set_origin(prev_o);
  }
};

// src: src/pages/_layout/components/help-menu-button.tsx:122
class HelpIconButton : public Button {
 public:
  using Button::Button;
  void draw_button_text(IRenderTarget& rt, const RectF& r) override {
    const auto p = ThemeService::instance().button_paint(theme_color_type_, theme_outlined_);
    // src: src/pages/_layout/components/help-menu-button.tsx:122 fontSize 16px
    constexpr float kIconSz = 16.0f;
    IconW::paint_at(rt, "helpoutline",
                    {r.x + (r.w - kIconSz) * 0.5f, r.y + (r.h - kIconSz) * 0.5f, kIconSz, kIconSz},
                    enabled() ? p.text : p.text_disabled);
  }
};

// src: src/pages/_layout/components/settings-drawer.tsx
class LangSelectButton : public Button {
 public:
  using Button::Button;
  void draw_button_text(IRenderTarget& rt, const RectF& r) override {
    const auto p = ThemeService::instance().button_paint(theme_color_type_, theme_outlined_);
    const bool disabled = !enabled();
    const Color4f ink = disabled ? p.text_disabled : p.text;
    TextStyle ts = text_style;
    ts.color = ink;
    ts.weight = p.weight;
    ts.align_h = AlignH::Left;
    ts.align_v = AlignV::Center;
    const RectF tr{r.x + 10.0f, r.y, std::max(0.0f, r.w - 26.0f), r.h};
    if (!disabled && state() != State::Pressed && p.text_shadow.a > 0) {
      TextStyle ts2 = ts;
      ts2.color = p.text_shadow;
      rt.draw_text(text, {tr.x, tr.y + p.text_shadow_dy, tr.w, tr.h}, ts2);
    }
    rt.draw_text(text, tr, ts);
    constexpr float kChevronSz = 14.0f;
    IconW::paint_at(rt, "keyboardarrowdown",
                    {r.right() - 18.0f, r.y + (r.h - kChevronSz) * 0.5f, kChevronSz, kChevronSz},
                    ink);
  }
};

// src: src/pages/_layout/components/settings-drawer.tsx
class SkinCell : public Button {
 public:
  explicit SkinCell(std::wstring t, int idx = 0)
      : Button(std::move(t)), cell_index(idx) {}
  int cell_index = 0;
  bool selected = false;

 protected:
  void draw_button_bg(IRenderTarget& rt, const RectF& r) override {
    const State st = state();
    const float k = st == State::Pressed ? 1.0f : hover_mix();
    if (!selected) {
      // src: src/pages/_layout/components/settings-drawer.tsx
      Color4f bg = detail::is_light()
                       ? mix_hover(Color4f::hex(0xF3F3F3), Color4f::hex(0xE5E5E5), k)
                       : mix_hover(Color4f::hex(0x1E1E1E), Color4f::hex(0x2D2D2D), k);
      const Pen border{detail::is_light() ? Color4f::hex(0xD0D0D0) : Color4f::hex(0x404040), 1.0f};
      Brush fill{bg};
      rt.fill_rect(r, fill);
      if (cell_index == 0) rt.draw_line({r.x + 0.5f, r.y}, {r.x + 0.5f, r.bottom()}, border);
      rt.draw_line({r.x, r.y + 0.5f}, {r.right(), r.y + 0.5f}, border);
      rt.draw_line({r.right() - 0.5f, r.y}, {r.right() - 0.5f, r.bottom()}, border);
      rt.draw_line({r.x, r.bottom() - 0.5f}, {r.right(), r.bottom() - 0.5f}, border);
      return;
    }
    const auto p = ThemeService::instance().button_paint("primary");
    draw_button_skin(rt, r, p, k, st == State::Pressed);
  }

  void draw_button_text(IRenderTarget& rt, const RectF& r) override {
    TextStyle ts = text_style;
    // src: src/utils/button-styles/button.ts:35
    ts.color = selected
                   ? (ThemeService::instance().skin() == theme::SkinId::Retro3d
                          ? Color4f::hex(0x2C1F03)
                          : ThemeService::instance().button_paint("primary").text)
                   : (detail::is_light() ? Color4f::hex(0x555555) : Color4f::hex(0xAAAAAA));
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt.draw_text(text, r, ts);
  }
};

// src: src/utils/button-styles/card.ts:45-120
void draw_card_bg(IRenderTarget& rt, const RectF& r) {
  draw_card(rt, r, card_default());
}

// src: src/assets/styles/index.scss:74-84
void draw_panel_bg(IRenderTarget& rt, const RectF& r) {
  auto& svc = ThemeService::instance();
  const auto pp = svc.panel_paint();
  if (pp.has_shadow)
    draw_soft_shadow(rt, r, pp.radius, pp.shadow_color, pp.shadow_dy,
                     pp.shadow_blur);
  if (pp.has_glow)
    draw_glow(rt, r, pp.radius, pp.glow_color, pp.glow_blur);
  if (pp.grad_bg) {
    rt.fill_round_rect_gradient_angle(r, pp.radius, pp.grad, pp.grad_count,
                                      pp.grad_angle);
  } else if (pp.bg.a > 0.001f) {
    Brush bg{pp.bg};
    rt.round_rect(r, pp.radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &bg);
  }
  if (pp.has_inner_bevel) {
    const float ih = 3.0f;
    rt.draw_line({r.x + pp.radius, r.y + ih}, {r.right() - pp.radius, r.y + ih},
                 Pen{pp.inner_hi, 1.0f});
    rt.draw_line({r.x + ih, r.y + pp.radius}, {r.x + ih, r.bottom() - pp.radius},
                 Pen{pp.inner_hi, 1.0f});
    rt.draw_line({r.x + pp.radius, r.bottom() - ih},
                 {r.right() - pp.radius, r.bottom() - ih},
                 Pen{pp.inner_dark, 1.0f});
    rt.draw_line({r.right() - ih, r.y + pp.radius},
                 {r.right() - ih, r.bottom() - pp.radius},
                 Pen{pp.inner_dark, 1.0f});
  }
  if (pp.border == theme::PanelPaint::BorderKind::None) return;
  Color4f bc = pp.border_color;
  if (pp.pulse) {
    const float now_s = static_cast<float>(GetTickCount64()) / 1000.0f;
    bc = Color4f::lerp(pp.pulse_from, pp.pulse_to, svc.pulse_phase(now_s));
  }
  Pen border{bc, 1.34f};
  rt.round_rect(r, pp.radius, border, nullptr);
  const float inner_r = pp.radius > 3.0f ? pp.radius - 3.0f : 0.0f;
  rt.round_rect({r.x + 3, r.y + 3, r.w - 6, r.h - 6}, inner_r, border, nullptr);
}

bool copy_text_to_clipboard(const std::string& utf8) {
  if (!OpenClipboard(nullptr)) return false;
  EmptyClipboard();
  std::wstring w = utf::utf8_to_utf16(utf8);
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
  if (h) {
    void* p = GlobalLock(h);
    if (p) {
      std::memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
      GlobalUnlock(h);
      SetClipboardData(CF_UNICODETEXT, h);
    } else {
      GlobalFree(h);
    }
  }
  CloseClipboard();
  return true;
}

bool open_profile_file(const std::string& file) {
  if (file.empty()) return false;
  std::wstring path = util::dirs::app_profiles_dir() + L"\\" + utf::utf8_to_utf16(file);
  if (!std::filesystem::exists(path)) return false;
  return reinterpret_cast<INT_PTR>(
             ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

}  // namespace

// ---------- SettingsDrawer ----------
// src: src/pages/_layout/components/settings-drawer.tsx:1-200
SettingsDrawer::SettingsDrawer(Widget* parent) : Stack(parent) {
  set_clip_to_bounds(true);

  auto mask = std::make_unique<Widget>();
  mask_ = mask.get();
  mask_->set_visible(false);
  add_child(std::move(mask));

  auto panel = std::make_unique<Widget>();
  panel_ = panel.get();
  panel_->set_clip_to_bounds(true);
  panel_->set_visible(false);
  add_child(std::move(panel));

  auto profile = std::make_unique<ProfileImportCard>();
  profile_card_ = profile.get();
  panel_->add_child(std::move(profile));

  auto takeover = std::make_unique<TakeoverCard>();
  takeover_card_ = takeover.get();
  panel_->add_child(std::move(takeover));

  auto routing = std::make_unique<RoutingCard>();
  routing_card_ = routing.get();
  panel_->add_child(std::move(routing));

  auto basic = std::make_unique<BasicCard>();
  basic_card_ = basic.get();
  panel_->add_child(std::move(basic));

  auto theme = std::make_unique<ThemeCard>();
  theme_card_ = theme.get();
  panel_->add_child(std::move(theme));

  logs_btn_ = theme_card_->logs_btn_;
  logs_btn_->on_click = [this]() {
    if (on_open_logs) on_open_logs();
  };

  auto hb = std::make_unique<Button>(L"帮助与关于");
  help_btn_ = hb.get();
  hb->set_visible(false);
  panel_->add_child(std::move(hb));

  auto style_cell = [](Button* b, float font) {
    b->themed_ = true;
    b->theme_color_type_ = "primary";
    b->text_style.size = font;
    b->text_style.align_h = AlignH::Center;
    b->text_style.align_v = AlignV::Center;
    b->set_visible(false);
  };

  auto lb = std::make_unique<LangSelectButton>(L"Language");
  lang_btn_ = lb.get();
  style_cell(lang_btn_, 11.0f);  // src: src/pages/_layout/components/settings-drawer.tsx
  lang_btn_->text_style.weight = 700;
  lang_btn_->text_style.font_family.clear();
  lang_btn_->set_a11y_role(A11yRole::ComboBox);
  lang_btn_->set_i18n_a11y_name("layout.a11y.selectLanguage");
  lang_btn_->set_a11y_expanded_getter(
      [this]() { return lang_menu_ && lang_menu_->is_open(); });
  panel_->add_child(std::move(lb));

  auto lm = std::make_unique<ContextMenu>();
  lang_menu_ = lm.get();
  // src: src/utils/popup-styles.ts:277-282 MenuItem 32px / px 14 / 12.5px
  lang_menu_->style_.item_h = 32.0f;
  lang_menu_->style_.padding_h = 14.0f;
  lang_menu_->style_.font_size = 12.5f;
  lang_menu_->style_.hover_inset = 4.0f;
  panel_->add_child(std::move(lm));

  auto pm = std::make_unique<ContextMenu>();
  profile_menu_ = pm.get();
  pm->set_z_order(950);
  panel_->add_child(std::move(pm));

  auto um = std::make_unique<ContextMenu>();
  url_menu_ = um.get();
  um->set_z_order(950);
  panel_->add_child(std::move(um));

  auto em = std::make_unique<EditProfileModal>();
  edit_profile_modal_ = em.get();
  add_child(std::move(em));

  auto dm = std::make_unique<DeleteProfileConfirmModal>();
  delete_confirm_modal_ = dm.get();
  dm->set_z_order(995);
  add_child(std::move(dm));

  static const wchar_t* kSkinNames[6] = {L"Trump-3D", L"Original", L"Modern",
                                         L"Frosted", L"Cyberpunk", L"Monochrome"};
  const theme::SkinId kSkinIds[6] = {
      theme::SkinId::Retro3d,   theme::SkinId::Original,
      theme::SkinId::ModernFlat, theme::SkinId::FrostedGlass,
      theme::SkinId::Cyberpunk,  theme::SkinId::Monochrome};
  for (int i = 0; i < 6; ++i) {
    auto b = std::make_unique<SkinCell>(kSkinNames[i], i);
    SkinCell* raw = b.get();
    style_cell(raw, 11.0f);
    raw->text_style.weight = static_cast<int>(Weight::Bold);
    const auto& fc = theme::skin_font_candidates(kSkinIds[i]);
    if (!fc.empty()) raw->text_style.font_family = fc.front();
    raw->on_click = [this, i]() { select_skin(i); };
    raw->set_a11y_toggle_getter([raw]() { return raw->selected; });
    skin_row_.push_back(raw);
    panel_->add_child(std::move(b));
  }
  refresh_skin_cells();

  auto cp = std::make_unique<ConnectionsPanel>();
  conn_panel_ = cp.get();
  conn_panel_->set_visible(false);
  add_child(std::move(cp));

  auto bh = std::make_unique<HelpIconButton>(L"");
  bottom_help_ = bh.get();
  style_cell(bottom_help_, 16.0f);  // src: src/pages/_layout/components/help-menu-button.tsx:122
  bottom_help_->text_style.weight = static_cast<int>(Weight::Bold);
  bottom_help_->on_click = [this]() { open_help_menu(); };
  bottom_help_->set_tooltip_text(tr("layout.components.helpMenu.buttonAriaLabel"));
  bottom_help_->set_i18n_a11y_name("layout.components.helpMenu.buttonAriaLabel");
  bottom_help_->set_a11y_expanded_getter(
      [this]() { return help_dialog_ && help_dialog_->is_open(); });
  bottom_help_->set_z_order(9999);  // src: src/pages/_layout/components/help-menu-button.tsx:117
  add_child(std::move(bh));

  auto close = std::make_unique<CloseButton>();
  close_btn_ = close.get();
  close_btn_->set_icon("close");
  close_btn_->size_ = 20;
  close->on_press_state = [this](bool down) { close_btn_pressed_ = down; };
  close_btn_->on_click = [this]() { set_open(false); };
  close_btn_->set_i18n_a11y_name("layout.a11y.closeSettings");
  close_btn_->set_z_order(9999);
  close_btn_->set_visible(false);
  add_child(std::move(close));

  auto hd = std::make_unique<HelpDialog>();
  help_dialog_ = hd.get();
  hd->set_z_order(10000);  // src: src/pages/_layout/components/help-menu-button.tsx:117
  add_child(std::move(hd));

  set_open(false);
  set_visible(true);
  wire_callbacks();
}

SettingsDrawer::~SettingsDrawer() {
  flush_pending_skin_prefs();
}

void SettingsDrawer::flush_pending_skin_prefs() {
  if (on_cancel_skin_persist_timer) on_cancel_skin_persist_timer();
  if (!skin_prefs_dirty_) return;
  skin_prefs_dirty_ = false;
  const std::string skin = pending_skin_name_.empty()
                               ? std::string(detail::current_skin_name(ThemeService::instance().skin()))
                               : pending_skin_name_;
  if (has_last_persisted_ && last_persisted_skin_ == skin &&
      std::fabs(last_persisted_depth_ - pending_depth_) < 0.0001f &&  // src: src/pages/_layout/hooks/use-skin-controls.ts:65
      std::fabs(last_persisted_vibrancy_ - pending_vibrancy_) < 0.0001f) {  // src: src/pages/_layout/hooks/use-skin-controls.ts:73
    return;
  }
  SkinPrefs p = load_skin_prefs();
  set_skin_factors(p, skin, pending_depth_, pending_vibrancy_);
  save_skin_prefs(p);
  has_last_persisted_ = true;
  last_persisted_skin_ = skin;
  last_persisted_depth_ = pending_depth_;
  last_persisted_vibrancy_ = pending_vibrancy_;
}

void SettingsDrawer::set_titlebar_hidden(bool hidden) {
  if (titlebar_hidden_ == hidden) return;
  titlebar_hidden_ = hidden;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    layout_tree(bounds());
    invalidate();
  }
}

void SettingsDrawer::detach_modals_to(Widget* host_root) {
  if (!host_root) return;
  auto move_modal = [&](Widget* modal) {
    if (!modal || modal->parent() == host_root) return;
    std::unique_ptr<Widget> owned;
    if (modal->parent()) owned = modal->parent()->remove_child(modal);
    if (owned) host_root->add_child(std::move(owned));
  };
  move_modal(edit_profile_modal_);
  move_modal(delete_confirm_modal_);
  move_modal(help_dialog_);
}

void SettingsDrawer::set_open(bool open) {
  if (open_ == open) return;
  if (!open) {
    flush_pending_skin_prefs();
    if (help_dialog_ && help_dialog_->is_open()) help_dialog_->close();
    if (lang_menu_ && lang_menu_->is_open()) lang_menu_->close();
    if (delete_confirm_modal_ && delete_confirm_modal_->is_open()) {
      delete_confirm_modal_->close();
    }
  }
  open_ = open;
  if (!open) {
    if (basic_card_ && basic_card_->port_field_ &&
        basic_card_->port_field_->focused()) {
      skip_port_blur_save_ = true;
      basic_card_->port_field_->blur();
    }
    if (basic_card_ && basic_card_->port_field_ &&
        basic_card_->port_current_ > 0) {
      basic_card_->port_field_->set_text(
          std::to_wstring(basic_card_->port_current_));
    }
    close_btn_pressed_ = false;
  } else {
    close_btn_pressed_ = false;
    skip_port_blur_save_ = false;
  }
  mask_->set_visible(false);
  panel_->set_visible(open);
  if (close_btn_) close_btn_->set_visible(open);
  if (bottom_help_) bottom_help_->set_visible(open);
  if (conn_panel_) conn_panel_->set_visible(open && bounds().w > win::kMiniWidthThreshold);
  if (open) {
    if (profile_card_) profile_card_->reload_profiles();
    if (bounds().w > 0) layout_tree(bounds());
    invalidate();
  }
  if (on_open_changed) on_open_changed(open);
}

// src: src/pages/_layout/components/theme-settings-card.tsx:59-134
void SettingsDrawer::sync_skin_sliders(theme::SkinId skin, float depth,
                                       float vibrancy) {
  if (!theme_card_) return;
  const auto spec = theme::skin_slider_spec(skin);
  if (theme_card_->depth_slider_) {
    theme_card_->depth_slider_->max_ = spec.max1;
    theme_card_->depth_slider_->set_value(depth, false);
  }
  if (theme_card_->vibrancy_slider_) {
    theme_card_->vibrancy_slider_->max_ = spec.max2;
    theme_card_->vibrancy_slider_->set_value(vibrancy, false);
  }
  if (theme_card_->depth_label_) theme_card_->depth_label_->set_text(spec.label1);
  if (theme_card_->vibrancy_label_)
    theme_card_->vibrancy_label_->set_text(spec.label2);
  if (theme_card_->depth_value_)
    theme_card_->depth_value_->set_text(fmt_factor(depth));
  if (theme_card_->vibrancy_value_)
    theme_card_->vibrancy_value_->set_text(fmt_factor(vibrancy));
}

void SettingsDrawer::select_skin(int i) {
  static const char* names[] = {"retro-3d", "original", "modern-flat",
                                "frosted-glass", "cyberpunk", "monochrome"};
  if (i < 0 || i >= 6) return;
  flush_pending_skin_prefs();
  if (auto skin = theme::skin_from_name(names[i])) {
    auto& svc = ThemeService::instance();
    svc.set_skin(*skin);
    SkinPrefs p = load_skin_prefs();
    // src: src/pages/_layout/hooks/use-skin-controls.ts:37-46
    auto [d, v] = skin_factors(p, names[i]);
    svc.set_factors(d, v);
    sync_skin_sliders(*skin, d, v);
    p.skin = names[i];
    save_skin_prefs(p);
    has_last_persisted_ = true;
    last_persisted_skin_ = names[i];
    last_persisted_depth_ = d;
    last_persisted_vibrancy_ = v;
    if (on_patch_verge) on_patch_verge("clash_mini_control_skin", names[i]);
    restyle_cards();
  }
}

void SettingsDrawer::apply_skin_prefs() {
  SkinPrefs p = load_skin_prefs();
  auto& svc = ThemeService::instance();
  theme::SkinId sid = theme::SkinId::Retro3d;
  if (auto skin = theme::skin_from_name(p.skin)) {
    svc.set_skin(*skin);
    sid = *skin;
  }
  bool dark = p.theme_mode == "dark" ||
              (p.theme_mode != "light" && detail::system_is_dark());
  svc.set_mode(dark ? theme::ThemeMode::Dark : theme::ThemeMode::Light);
  auto [d, v] = skin_factors(p, p.skin);
  svc.set_factors(d, v);
  if (theme_card_) {
    int idx = p.theme_mode == "light" ? 1 : (p.theme_mode == "dark" ? 2 : 0);
    theme_card_->mode_seg_->select_silent(idx);
  }
  sync_skin_sliders(sid, d, v);
  restyle_cards();
}

void SettingsDrawer::sync_theme_mode(const std::wstring& mode) {
  std::string m = utf::utf16_to_utf8(mode);
  if (m.empty()) m = "dark";
  int idx = m == "light" ? 1 : (m == "dark" ? 2 : 0);
  bool dark = m == "dark" || (m != "light" && detail::system_is_dark());
  auto& svc = ThemeService::instance();
  svc.set_mode(dark ? theme::ThemeMode::Dark : theme::ThemeMode::Light);
  if (theme_card_) theme_card_->mode_seg_->select_silent(idx);
  restyle_cards();
}

// src: src/pages/_layout/components/settings-drawer.tsx
void SettingsDrawer::restyle_cards() {
  const Color4f txt = detail::card_text();
  auto set = [&](Label* l) {
    if (l) l->style.color = txt;
  };
  const bool retro = ThemeService::instance().skin() == theme::SkinId::Retro3d;
  auto restyle_seg = [&](Segmented* s) {
    if (!s) return;
    s->gold_3d_ = retro;
    s->invalidate();
  };
  if (profile_card_) {
    profile_card_->set_title_color(txt);
    if (auto* f = profile_card_->url_field()) {
      f->apply_theme();
      f->style_.size = 13;
    }
    profile_card_->invalidate();
  }
  if (takeover_card_) {
    set(takeover_card_->title_);
    set(takeover_card_->hint_);
    if (takeover_card_->seg_) takeover_card_->seg_->invalidate();
  }
  if (routing_card_) {
    set(routing_card_->title_);
    set(routing_card_->hint_);
    if (routing_card_->seg_) {
      routing_card_->seg_->style_.size = detail::routing_font_for_lang();
    }
    restyle_seg(routing_card_->seg_);
  }
  if (basic_card_) {
    set(basic_card_->title_);
    set(basic_card_->sniffer_label_);
    restyle_seg(basic_card_->sniffer_seg_);
    set(basic_card_->port_label_);
    for (auto* l : basic_card_->labels_) set(l);
    if (basic_card_->port_field_) {
      basic_card_->port_field_->apply_theme();
      basic_card_->port_field_->style_.size = 13;
      basic_card_->port_field_->style_.align_h = AlignH::Center;
      basic_card_->port_field_->set_padding_x(4.0f);  // src: src/pages/_layout/components/basic-settings-card.tsx:511
      basic_card_->port_field_->caret_color_ =
          basic_card_->port_field_->style_.color;
    }
  }
  if (theme_card_) {
    set(theme_card_->title_);
    set(theme_card_->depth_label_);
    set(theme_card_->vibrancy_label_);
    const Color4f num = detail::slider_value_color();
    theme_card_->depth_value_->style.color = num;
    theme_card_->vibrancy_value_->style.color = num;
    if (theme_card_->copyright_) {
      // src: src/pages/_layout/components/theme-settings-card.tsx:459-476
      theme_card_->copyright_->style.color =
          detail::is_light() ? Color4f::hex(0x3C3C43, 0.45f)
                             : Color4f::hex(0xEBEBF5, 0.45f);
    }
    if (theme_card_->email_) {
      Color4f ec = theme::ThemeService::instance().primary();
      ec.a = 0.75f;
      theme_card_->email_->style.color = ec;
    }
    restyle_seg(theme_card_->mode_seg_);
    if (theme_card_->depth_slider_) theme_card_->depth_slider_->apply_theme();
    if (theme_card_->vibrancy_slider_)
      theme_card_->vibrancy_slider_->apply_theme();
    theme_card_->invalidate();
  }
  refresh_skin_cells();
  if (lang_btn_) lang_btn_->invalidate();
  if (bottom_help_) bottom_help_->invalidate();
  invalidate();
}

void SettingsDrawer::refresh_skin_cells() {
  const int idx = static_cast<int>(ThemeService::instance().skin());
  for (size_t i = 0; i < skin_row_.size(); ++i) {
    auto* cell = dynamic_cast<SkinCell*>(skin_row_[i]);
    if (!cell) continue;
    cell->selected = static_cast<int>(i) == idx;
    cell->invalidate();
  }
}

void SettingsDrawer::sync_takeover(bool sys_proxy, bool tun) {
  if (takeover_card_ && takeover_card_->seg_) {
    takeover_card_->seg_->set_mode(tun ? "TUN" : (sys_proxy ? "SYS" : "MAN"));
  }
  if (basic_card_) {
    basic_card_->set_tun_enabled(tun);
  }
}

void SettingsDrawer::sync_routing(const std::string& rule_fallback) {
  if (!routing_card_ || !routing_card_->seg_) return;
  const int idx = (rule_fallback == "proxy")
                      ? 0
                      : ((rule_fallback == "addurl") ? 2 : 1);
  routing_card_->seg_->select_silent(idx);
}

void SettingsDrawer::sync_basic(bool auto_launch, bool silent, bool allow_lan,
                                bool ipv6, int mixed_port,
                                std::optional<bool> tun_sniffer) {
  if (!basic_card_) return;
  basic_card_->sw_auto_launch_->set_checked(auto_launch, false);
  basic_card_->sw_silent_start_->set_checked(silent, false);
  basic_card_->sw_allow_lan_->set_checked(allow_lan, false);
  basic_card_->sw_allow_ipv6_->set_checked(ipv6, false);
  if (basic_card_->sniffer_seg_) {
    // src: src/pages/_layout/components/basic-settings-card.tsx:307-318
    const int sniffer_idx = !tun_sniffer.has_value() ? 0 : (*tun_sniffer ? 1 : 2);
    basic_card_->sniffer_seg_->select_silent(sniffer_idx);
  }
  if (mixed_port > 0) {
    basic_card_->port_current_ = mixed_port;
    basic_card_->port_field_->set_text(std::to_wstring(mixed_port));
  }
  basic_card_->invalidate();
}

// src: src/pages/_layout/components/settings-drawer.tsx:1-200
void SettingsDrawer::on_arrange(const RectF& area) {
  set_bounds(area);
  mask_->layout_tree({0, 0, area.w, area.h});
  panel_->layout_tree({0, 0, area.w, area.h});
  if (help_dialog_ && help_dialog_->parent() == this) {
    help_dialog_->layout_tree({0, 0, area.w, area.h});
  }
  close_btn_->layout_tree({area.w - kCloseRight - kCloseSize, kCloseTop,
                           kCloseSize, kCloseSize});
  if (edit_profile_modal_ && edit_profile_modal_->parent() == this) {
    edit_profile_modal_->layout_tree({0, 0, area.w, area.h});
  }
  if (delete_confirm_modal_ && delete_confirm_modal_->parent() == this) {
    delete_confirm_modal_->layout_tree({0, 0, area.w, area.h});
  }

  const float bottom_inset = area.w <= win::kMiniWidthThreshold ? 100.0f : 135.0f;
  const float top_h = titlebar_hidden_ ? 0.0f : 30.0f;
  const bool tall = (area.h + top_h + bottom_inset) > 830.0f;
  bottom_row_visible_ = tall && open_;
  const bool side = area.w > win::kMiniWidthThreshold;
  const float pb = (ThemeService::instance().skin() == theme::SkinId::Original)
                       ? 0.0f
                       : kPanelBorder;  // src: src/assets/styles/index.scss:125
  const float col_x = kPad + pb;
  const float col_y = kPad + pb;
  const float col_h = area.h - pb * 2.0f - kPad * 2.0f - (tall ? 30.0f : 0.0f);
  left_rect_ = {col_x, col_y, kLeftW, col_h};
  float y = col_y;
  float profile_h = profile_card_ ? profile_card_->preferred_height() : 124.4f;
  const float col_bottom = col_y + col_h;
  profile_rect_ = {col_x, y, kCardW, profile_h};
  y += profile_h + kLeftGap;

  combo_rect_ = {col_x, y, kCardW, kComboH};
  takeover_card_->layout_tree({combo_rect_.x + kCardPad,
                               combo_rect_.y + kCardPad,
                               combo_rect_.w - kCardPad * 2,
                               kComboTakeoverH});
  routing_card_->layout_tree({combo_rect_.x + kCardPad,
                              combo_rect_.y + kCardPad + kComboTakeoverH,
                              combo_rect_.w - kCardPad * 2, kComboRoutingH});
  y += kComboH + kLeftGap;

  basic_rect_ = {col_x, y, kCardW, kBasicH};
  y += kBasicH + kLeftGap;
  float theme_h = kThemeH;
  // theme_rect_ 固定使用 kThemeH（避免高度压缩遮挡滑块）
  theme_rect_ = {col_x, y, kCardW, theme_h};

  profile_card_->layout_tree(profile_rect_);
  basic_card_->layout_tree(basic_rect_);
  theme_card_->layout_tree(theme_rect_);

  const float row_y = area.h - 24.0f;
  if (bottom_help_) {
    if (tall) {
      bottom_help_->layout_tree({0.0f, row_y, 80.0f, 24.0f});  // src: src/pages/_layout/components/help-menu-button.tsx:98
    } else {
      bottom_help_->layout_tree({area.w - 68.0f, 3.0f, 28.0f, 28.0f});  // src: src/pages/_layout/components/help-menu-button.tsx:113
    }
    lang_btn_->layout_tree({80, row_y, 97.5f, 24});
    float sx = 177.5f;
    for (size_t i = 0; i < skin_row_.size(); ++i) {
      const float w = i == 0 ? 92.5f : 74.0f;
      skin_row_[i]->layout_tree({sx, row_y, w, 24});
      sx += w;
    }
    const bool show_bottom_row = tall && open_;
    bottom_help_->set_visible(open_);
    lang_btn_->set_visible(show_bottom_row);
    for (auto* b : skin_row_) b->set_visible(show_bottom_row);
    if (!show_bottom_row && lang_menu_ && lang_menu_->is_open()) lang_menu_->close();
    if (!open_ && help_dialog_ && help_dialog_->is_open()) help_dialog_->close();
  }

  CWLOG_INFO(std::format(
      "[DRAWERLAYOUT] area={:.1f}x{:.1f} tall={} col_h={:.1f} profile_h={:.1f} "
      "tops p={:.1f} c={:.1f} b={:.1f} t={:.1f}",
      area.w, area.h, tall, col_h, profile_h, profile_rect_.y, combo_rect_.y,
      basic_rect_.y, theme_rect_.y));

  if (conn_panel_) {
    conn_panel_->set_visible(open_ && side);
    if (side) {
      const float conn_x = col_x + kLeftW + kGap;
      const float conn_w = std::max(0.0f, area.w - conn_x - (kPad + pb));
      const float conn_h = std::max(
          0.0f, area.h - pb * 2.0f - kPad * 2.0f - (tall ? 24.0f : 0.0f));
      conn_panel_->layout_tree({conn_x, col_y, conn_w, conn_h});
    }
  }
}

bool SettingsDrawer::on_mouse(const MouseEvent& e) {
  (void)e;
  return Stack::on_mouse(e);
}

void SettingsDrawer::open_help_menu() {
  if (!help_dialog_) return;
  auto format_core_version = [](std::string s) -> std::string {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == 'v' || s[i] == 'V')) ++i;
    return "Ver." + s.substr(i);
  };
  auto core_version = [&]() -> std::string {
    if (clashwin::cmd::mihomo().is_err()) return {};
    auto v = clashwin::cmd::mihomo().value()->get_version(300);
    if (v.is_err()) return {};
    std::string s = v.value().version;
    if (v.value().meta && s.find("Mihomo") == std::string::npos) s += " Mihomo";
    return s;
  }();
  const std::wstring app_suffix =
      L" (v" + utf::utf8_to_utf16(config::consts::kAppVersion) + L")";
  const std::wstring core_suffix =
      core_version.empty()
          ? std::wstring{}
          : L" (" + utf::utf8_to_utf16(format_core_version(core_version)) + L")";
  std::vector<MenuItem> its;
  // src: src/pages/_layout/components/help-menu-button.tsx:161-257
  its.push_back({L"⭐ " + tr("layout.components.helpMenu.github"), false, false, L"", false});
  its.push_back({L"💡 " + tr("layout.components.helpMenu.wiki"), false, false, L"", false});
  its.push_back({L"🧭 " + tr("layout.components.helpMenu.firstRunGuide"), false, false, L"", false});
  its.push_back({L"🛡️ " + tr("layout.components.antivirus.menuLabel"), false, false, L"", false});
  its.push_back({L"", false, true, L"", false});
  its.push_back({L"📦 " + tr("layout.components.helpMenu.exportDiagnostics"), false, false, L"", false});
  its.push_back({L"", false, true, L"", false});
  its.push_back({L"🚀 " + tr("layout.components.helpMenu.checkAppUpdate") + app_suffix,
                 app_checking_, false, L"", false});
  its.push_back({L"⚙️ " + tr("layout.components.helpMenu.checkCoreUpdate") + core_suffix,
                 core_checking_, false, L"", false});
  help_dialog_->on_dismiss = [this] { invalidate(); };
  help_dialog_->open(its, [this](int idx) {
    HelpAction a;
    switch (idx) {
      case 0: a = HelpAction::StarGithub; break;
      case 1: a = HelpAction::OpenWiki; break;
      case 2: a = HelpAction::ReviewGuide; break;
      case 3: a = HelpAction::AntivirusGuide; break;
      case 5: a = HelpAction::ExportDiagnostics; break;
      case 7: a = HelpAction::CheckAppUpdate; break;
      case 8: a = HelpAction::CheckCoreUpdate; break;
      default: return;
    }
    if (a == HelpAction::CheckAppUpdate && on_check_app_update) {
      on_check_app_update();
      return;
    }
    if (a == HelpAction::CheckCoreUpdate && on_check_core_update) {
      on_check_core_update();
      return;
    }
    HelpMenu::execute_action(a, nullptr);
  });
}

void SettingsDrawer::set_app_update_checking(bool checking) {
  app_checking_ = checking;
  if (help_dialog_ && help_dialog_->is_open()) open_help_menu();
}

void SettingsDrawer::set_core_update_checking(bool checking) {
  core_checking_ = checking;
  if (help_dialog_ && help_dialog_->is_open()) open_help_menu();
}

void SettingsDrawer::open_profile_menu(const std::string& uid, PointF anchor) {
  if (!profile_menu_ || !profile_card_) return;
  const config::PrfItem* item = profile_card_->find_item(uid);
  if (!item) return;
  const bool local = item->type() == config::PrfType::Local;
  const std::string url = item->url.value_or(std::string());

  // src: src/utils/popup-styles.ts:277-282 MenuItem minH 32 / px 14 / 12.5px
  MenuStyle st;
  st.item_h = 32;
  st.padding_h = 14;
  st.min_w = 180;
  st.radius = 6;
  st.font_size = 12.5f;
  st.pad_v = 4;
  st.hover_inset = 4;
  st.border = Color4f(1, 1, 1, 0.12f);
  st.text = ThemeService::instance().text_primary();
  st.disabled_text = Color4f(st.text.r, st.text.g, st.text.b, 0.38f);
  // src: src/pages/_theme.tsx:8
  st.danger_text =
      ThemeService::instance().is_light() ? Color4f::hex(0xFF3B30) : Color4f::hex(0xFF453A);
  apply_menu_skin(st);

  // src: src/pages/_layout/components/profile-context-menu.tsx
  std::vector<MenuItem> its;
  its.push_back({L"📝 " + tr("shared.actions.edit"), false, false, L"", false});
  its.push_back({L"📄 " + tr("profiles.components.menu.editFile"), false, false, L"", false});
  its.push_back({L"", false, true, L"", false});
  its.push_back({L"🔗 " + tr("profiles.components.menu.copyLink"), local || url.empty(), false, L"", false});
  its.push_back({L"🔄 " + tr("profiles.components.menu.update"), local, false, L"", false});
  its.push_back({L"", false, true, L"", false});
  its.push_back({L"❌ " + tr("shared.actions.delete"), false, false, L"", true});

  profile_menu_->style_ = st;
  profile_menu_->on_select = [this, uid, url](int idx) {
    if (idx == 0) {
      open_edit_profile(uid);
    } else if (idx == 1) {
      const config::PrfItem* it = profile_card_->find_item(uid);
      if (!it || !it->file || !open_profile_file(*it->file)) {
        auto vr = cmd::CommandService::instance().invoke(
            "view_profile", nlohmann::json::object({{"index", uid}}));
        if (!vr.is_ok()) {
          events::NoticeService::instance().push("error", vr.error().message);
        }
      }
    } else if (idx == 3) {
      if (copy_text_to_clipboard(url)) {
        events::NoticeService::instance().push(
            "success", tr8("layout.notices.profileLinkCopied"));
      } else {
        events::NoticeService::instance().push(
            "error", tr8("layout.notices.copyFailed"));
      }
    } else if (idx == 4) {
      if (on_update_profile) on_update_profile(uid);
    } else if (idx == 6) {
      request_delete_profile(uid);
    }
  };
  profile_menu_->open(its, panel_ ? panel_->to_local(anchor) : anchor);
}

void SettingsDrawer::open_url_menu(PointF anchor) {
  if (!url_menu_ || !profile_card_) return;
  TextField* f = profile_card_->url_field();
  if (!f) return;

  // src: src/utils/popup-styles.ts:277-282
  MenuStyle st;
  st.item_h = 32;
  st.padding_h = 14;
  st.min_w = 160;
  st.radius = 6;
  st.font_size = 12.5f;
  st.pad_v = 4;
  st.hover_inset = 4;
  st.border = Color4f(1, 1, 1, 0.12f);
  st.text = ThemeService::instance().text_primary();
  st.disabled_text = Color4f(st.text.r, st.text.g, st.text.b, 0.38f);
  // src: src/pages/_theme.tsx:8
  st.danger_text =
      ThemeService::instance().is_light() ? Color4f::hex(0xFF3B30) : Color4f::hex(0xFF453A);
  apply_menu_skin(st);

  // src: src/pages/_layout/components/input-context-menu.tsx
  std::vector<MenuItem> its;
  its.push_back({L"✂️ " + tr("shared.actions.cut"), !f->can_cut(), false, L"", false});
  its.push_back({L"📋 " + tr("shared.actions.copy"), !f->can_copy(), false, L"", false});
  its.push_back({L"📥 " + tr("shared.actions.paste"), !f->can_paste(), false, L"", false});
  its.push_back({L"🔍 " + tr("shared.actions.selectAll"), !f->can_select_all(), false, L"", false});
  its.push_back({L"", false, true, L"", false});
  its.push_back({L"🧹 " + tr("shared.actions.clearInput"), !f->can_clear(), false, L"", true});

  url_menu_->style_ = st;
  url_menu_->on_select = [f](int idx) {
    switch (idx) {
      case 0: f->cut(); break;
      case 1: f->copy(); break;
      case 2: f->paste(); break;
      case 3: f->select_all(); break;
      case 5: f->clear_text(); break;
      default: break;
    }
    f->request_focus();
  };
  url_menu_->open(its, panel_->to_local(anchor));
}

void SettingsDrawer::request_delete_profile(const std::string& uid) {
  if (!delete_confirm_modal_) {
    if (on_delete_profile) on_delete_profile(uid);
    return;
  }
  std::string pname = uid;
  if (profile_card_) {
    if (const config::PrfItem* item = profile_card_->find_item(uid)) {
      if (item->name && !item->name->empty()) pname = *item->name;
    }
  }
  delete_confirm_modal_->open(uid, pname);
  invalidate();
}

void SettingsDrawer::open_edit_profile(const std::string& uid) {
  if (!edit_profile_modal_ || !profile_card_) return;
  const config::PrfItem* item = profile_card_->find_item(uid);
  if (!item) return;
  const bool local = item->type() == config::PrfType::Local;
  int mins = 0;
  if (item->option && item->option->update_interval)
    mins = static_cast<int>(*item->option->update_interval);
  edit_profile_uid_ = uid;
  edit_profile_modal_->open(item->name.value_or(std::string()),
                            item->url.value_or(std::string()), mins, local);
}

// src: src/pages/_layout/components/settings-drawer.tsx:1-200
void SettingsDrawer::wire_callbacks() {
  profile_card_->on_height_changed = [this] {
    if (bounds().w > 0.0f && bounds().h > 0.0f) {
      layout_tree(bounds());
      invalidate();
    }
  };
  profile_card_->on_import = [this](const std::string& url,
                                    const std::string& name) {
    return on_import_profile ? on_import_profile(url, name) : false;
  };
  profile_card_->on_import_async =
      [this](const std::string& url, const std::string& name,
             std::function<void(bool ok)> done) {
        if (on_import_profile_async) {
          on_import_profile_async(
              url, name, [this, done = std::move(done)](bool ok) {
                if (done) done(ok);
                if (ok && bounds().w > 0) {
                  layout_tree(bounds());
                  invalidate();
                }
              });
        } else if (done) {
          done(on_import_profile ? on_import_profile(url, name) : false);
        }
      };
  profile_card_->on_select_profile = [this](const std::string& uid) {
    if (on_select_profile) on_select_profile(uid);
  };
  profile_card_->on_update_profile = [this](const std::string& uid) {
    if (on_update_profile) on_update_profile(uid);
  };
  profile_card_->on_delete_profile = [this](const std::string& uid) {
    request_delete_profile(uid);
  };
  profile_card_->on_context_menu = [this](const std::string& uid, PointF anchor) {
    open_profile_menu(uid, anchor);
  };
  profile_card_->on_url_context_menu = [this](PointF anchor) { open_url_menu(anchor); };
  if (edit_profile_modal_) {
    edit_profile_modal_->on_save = [this](const std::string& name,
                                         const std::string& url, int interval) {
      if (!edit_profile_uid_.empty() && on_patch_profile)
        on_patch_profile(edit_profile_uid_, name, url, interval);
      edit_profile_uid_.clear();
      profile_card_->reload_profiles();
      if (bounds().w > 0) layout_tree(bounds());
      invalidate();
    };
    edit_profile_modal_->on_dismiss = [this] { invalidate(); };
  }
  if (delete_confirm_modal_) {
    delete_confirm_modal_->on_confirm = [this](const std::string& uid) {
      if (on_delete_profile) on_delete_profile(uid);
      if (profile_card_) profile_card_->reload_profiles();
      if (bounds().w > 0) layout_tree(bounds());
      invalidate();
    };
    delete_confirm_modal_->on_dismiss = [this] { invalidate(); };
  }
  takeover_card_->seg_->apply = [this](const std::string& mode) {
    if (on_takeover_change) return on_takeover_change(mode);
    bool sys = mode == "SYS", tun = mode == "TUN";
    if (tun) {
      auto& svc = cmd::CommandService::instance();
      bool is_admin = false;
      auto ar = svc.invoke("app_is_admin");
      if (ar.is_ok() && ar.value().is_boolean())
        is_admin = ar.value().get<bool>();
      bool service_ok = svc.invoke("is_service_available").is_ok();
      if (!(is_admin || service_ok)) {
        auto inst = svc.invoke("install_service");
        if (!inst.is_ok()) {
          events::NoticeService::instance().push(
              "error", tr8("layout.notices.tunServiceConfigFailed"));
          return false;
        }
      }
    }
    CWLOG_INFO("[TAKEOVER] apply mode=" + mode + " sys=" + (sys ? "1" : "0") +
               " tun=" + (tun ? "1" : "0") +
               " patch_verge=" + (on_patch_verge ? "set" : "NULL"));
    if (on_patch_verge) {
      on_patch_verge("enable_system_proxy", sys);
      on_patch_verge("enable_tun_mode", tun);
    }
    return true;
  };
  routing_card_->seg_->on_change = [this](int i) {
    static const char* fb[] = {"proxy", "direct", "addurl"};
    if (i < 0 || i >= 3) return;
    if (on_routing_change_async) {
      on_routing_change_async(fb[i], i == 2);
      return;
    }
    if (on_patch_verge) on_patch_verge("rule_fallback", fb[i]);
    if (on_patch_clash_mode) on_patch_clash_mode("rule");
    if (i == 2 && on_add_rule) on_add_rule();
  };
  // src: src/pages/_layout/components/routing-preference-card.tsx:202
  routing_card_->seg_->on_reclick = [this](int i) {
    if (i == 2 && on_add_rule) on_add_rule();
  };
  basic_card_->sw_auto_launch_->on_change = [this](bool v) {
    if (on_patch_verge) on_patch_verge("enable_auto_launch", v);
  };
  basic_card_->sw_silent_start_->on_change = [this](bool v) {
    if (on_patch_verge) on_patch_verge("enable_silent_start", v);
  };
  basic_card_->sw_allow_lan_->on_change = [this](bool v) {
    if (on_patch_verge) on_patch_verge("clash_allow_lan", v);
  };
  basic_card_->sw_allow_ipv6_->on_change = [this](bool v) {
    if (on_patch_verge) on_patch_verge("clash_ipv6", v);
  };
  if (basic_card_->sniffer_seg_) {
    // src: src/pages/_layout/components/basic-settings-card.tsx:307-318
    basic_card_->sniffer_seg_->on_change = [this](int i) {
      if (!on_patch_verge) return;
      if (i == 0) {
        on_patch_verge("enable_tun_sniffer", nullptr);
      } else {
        on_patch_verge("enable_tun_sniffer", i == 1);
      }
    };
  }
  auto commit_port = [this] {
    std::wstring t = basic_card_->port_field_->text();
    while (!t.empty() && (t.front() == L' ' || t.front() == L'\t')) t.erase(t.begin());
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\t')) t.pop_back();
    bool digits = !t.empty();
    int port = 0;
    for (wchar_t ch : t) {
      if (ch < L'0' || ch > L'9') {
        digits = false;
        break;
      }
      if (port <= 65535) port = port * 10 + (ch - L'0');
    }
    if (!digits || port < 1 || port > 65535) {
      // src: src/pages/_layout/hooks/use-minimal-settings.ts:97-100
      events::NoticeService::instance().push(
          "error", tr8("settings.mini.invalidPort"));
      const int fallback =
          basic_card_->port_current_ > 0
              ? basic_card_->port_current_
              : static_cast<int>(config::consts::kDefaultMixed);
      basic_card_->port_field_->set_text(std::to_wstring(fallback));
      basic_card_->port_field_->invalidate();
      return;
    }
    bool ok = false;
    if (on_save_port) ok = on_save_port(port);
    if (!ok && basic_card_->port_current_ > 0) {
      basic_card_->port_field_->set_text(
          std::to_wstring(basic_card_->port_current_));
    }
  };
  basic_card_->port_field_->on_key_intercept = [this](const KeyEvent& e) {
    if (e.type == KeyEventType::Down && e.key == VK_ESCAPE) {
      skip_port_blur_save_ = true;
      if (basic_card_->port_current_ > 0) {
        basic_card_->port_field_->set_text(
            std::to_wstring(basic_card_->port_current_));
      }
      basic_card_->port_field_->blur();
      return true;
    }
    return false;
  };
  basic_card_->port_field_->on_submit = commit_port;
  basic_card_->port_field_->on_focus_lost = [this, commit_port]() {
    if (should_skip_mixed_port_blur_save(open_, skip_port_blur_save_,
                                         close_btn_pressed_)) {
      skip_port_blur_save_ = false;
      if (open_ && basic_card_->port_current_ > 0) {
        basic_card_->port_field_->set_text(
            std::to_wstring(basic_card_->port_current_));
      }
      return;
    }
    commit_port();
  };
  basic_card_->save_btn_->on_click = commit_port;
  static const char* skin_names[] = {"retro-3d", "original", "modern-flat",
                                     "frosted-glass", "cyberpunk",
                                     "monochrome"};
  (void)skin_names;
  for (size_t i = 0; i < theme_card_->skin_buttons_.size(); ++i) {
    theme_card_->skin_buttons_[i]->on_click = [this, i]() {
      select_skin(static_cast<int>(i));
    };
  }
  if (lang_btn_ && lang_menu_) {
    static const wchar_t* kLangLabels[13] = {
        L"简体中文", L"English", L"Русский",  L"فارسی",    L"Татарча",
        L"Bahasa Indonesia", L"العربية", L"한국어", L"Türkçe", L"Deutsch",
        L"Español",  L"日本語",   L"繁體中文"};
    lang_btn_->on_click = [this]() {
      const std::string cur = i18n::I18n::instance().language();
      static const char* codes[13] = {"zh",  "en", "ru",  "fa",   "tt",
                                      "id",  "ar", "ko",  "tr",   "de",
                                      "es",  "jp", "zhtw"};
      std::vector<MenuItem> items;
      for (int i = 0; i < 13; ++i) {
        MenuItem it;
        it.label = kLangLabels[i];
        it.selected = (cur == codes[i]);
        items.push_back(it);
      }
      // src: src/utils/popup-styles.ts:277-282 MenuItem 32px / px 14 / 12.5px
      apply_menu_skin(lang_menu_->style_);
      lang_menu_->style_.item_h = 32.0f;
      lang_menu_->style_.padding_h = 14.0f;
      lang_menu_->style_.font_size = 12.5f;
      lang_menu_->style_.hover_inset = 4.0f;
      RectF lb = lang_btn_->bounds();
      const float max_menu_h = std::max(120.0f, lb.y - 8.0f);
      const float want_h = std::min(440.0f, max_menu_h);
      lang_menu_->style_.max_h = want_h;
      lang_menu_->open(items, {lb.x, lb.y - want_h - 4.0f});
    };
    lang_menu_->on_select = [this](int i) {
      static const char* codes[13] = {"zh",  "en", "ru",  "fa",   "tt",
                                      "id",  "ar", "ko",  "tr",   "de",
                                      "es",  "jp", "zhtw"};
      if (i < 0 || i >= 13) return;
      i18n::I18n::instance().set_language(codes[i]);
      if (routing_card_ && routing_card_->seg_) {
        routing_card_->seg_->style_.size = detail::routing_font_for_lang();
        routing_card_->seg_->invalidate();
      }
      if (on_patch_verge) on_patch_verge("language", std::string(codes[i]));
    };
  }
  theme_card_->mode_seg_->on_change = [this](int i) {
    auto& svc = ThemeService::instance();
    bool dark = i == 2 || (i == 0 && detail::system_is_dark());
    svc.set_mode(dark ? theme::ThemeMode::Dark : theme::ThemeMode::Light);
    SkinPrefs p = load_skin_prefs();
    p.theme_mode = i == 1 ? "light" : (i == 2 ? "dark" : "sys");
    save_skin_prefs(p);
    restyle_cards();
    if (on_patch_verge)
      on_patch_verge("theme_mode", i == 1 ? "light" : (i == 2 ? "dark" : "system"));
  };
  // src: src/pages/_layout/hooks/use-skin-controls.ts:4
  theme_card_->depth_slider_->on_change = [this](float v) {
    auto& svc = ThemeService::instance();
    svc.set_factors(v, svc.vibrancy_factor());
    if (theme_card_->depth_value_)
      theme_card_->depth_value_->set_text(fmt_factor(v));
    pending_skin_name_ = detail::current_skin_name(svc.skin());
    pending_depth_ = v;
    pending_vibrancy_ = svc.vibrancy_factor();
    skin_prefs_dirty_ = true;
    restyle_cards();
    if (on_arm_skin_persist_timer) on_arm_skin_persist_timer(200);
  };
  theme_card_->vibrancy_slider_->on_change = [this](float v) {
    auto& svc = ThemeService::instance();
    svc.set_factors(svc.depth_factor(), v);
    if (theme_card_->vibrancy_value_)
      theme_card_->vibrancy_value_->set_text(fmt_factor(v));
    pending_skin_name_ = detail::current_skin_name(svc.skin());
    pending_depth_ = svc.depth_factor();
    pending_vibrancy_ = v;
    skin_prefs_dirty_ = true;
    restyle_cards();
    if (on_arm_skin_persist_timer) on_arm_skin_persist_timer(200);
  };
  theme_card_->depth_slider_->on_commit = [this](float) {
    flush_pending_skin_prefs();
  };
  theme_card_->vibrancy_slider_->on_commit = [this](float) {
    flush_pending_skin_prefs();
  };
}

std::wstring SettingsDrawer::fmt_factor(float v) {
  wchar_t buf[16];
  swprintf(buf, 16, L"%.1f", v);
  return buf;
}

// src: src/pages/_layout/components/settings-drawer.tsx:1-200
void SettingsDrawer::on_paint(IRenderTarget& rt) {
  if (!open_) return;
  const RectF area{0, 0, bounds().w, bounds().h};
  rt.push_clip(area);
  draw_panel_bg(rt, area);
  if (bounds().w > win::kMiniWidthThreshold) {
    rt.fill_rect({left_rect_.x + kLeftW - 1.0f, left_rect_.y, 1.0f, left_rect_.h},
                 Brush{detail::divider_color()});
  }
  if (combo_rect_.y < area.h) draw_card_bg(rt, combo_rect_);
  if (basic_rect_.y < area.h) draw_card_bg(rt, basic_rect_);
  if (theme_rect_.y < area.h) draw_card_bg(rt, theme_rect_);
  rt.pop_clip();
  if (ThemeService::instance().panel_paint().pulse) invalidate();
}

std::wstring SettingsDrawer::a11y_default_name() const {
  return tr("settings.components.verge.basic.title");
}

}  // namespace clashwin::ui::scene
