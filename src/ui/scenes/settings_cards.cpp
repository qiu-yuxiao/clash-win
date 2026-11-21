// 设置抽屉四张设置卡片：TakeoverCard / RoutingCard / BasicCard / ThemeCard
// （拆分自 src/ui/scenes/settings_drawer.cpp）
#include "settings_drawer.h"
#include "settings_drawer_internal.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>

#include "../theme/theme.h"
#include "clashwin/config/constants.h"
#include "events/notice.h"
#include "ui/i18n_text.h"

namespace clashwin::ui::scene {

using theme::ThemeService;

namespace {

// src: src/pages/_layout/components/theme-settings-card.tsx:174
void style_gold_segmented(Segmented* s) {
  s->themed_ = true;
  s->gold_3d_ = (ThemeService::instance().skin() == theme::SkinId::Retro3d);
  // src: src/pages/_layout/components/theme-settings-card.tsx:174 容器圆角 4
  s->radius_ = 4.0f;
  // src: src/pages/_layout/components/takeover-mode-card.tsx:116
  s->style_.size = 13;
  s->style_.weight = static_cast<int>(Weight::Bold);
}

// src: src/pages/_layout/components/theme-settings-card.tsx:259
void style_gold_segmented_small(Segmented* s) {
  style_gold_segmented(s);
  s->style_.size = 11;  // src: src/pages/_layout/components/theme-settings-card.tsx:259
}

void style_slider(Slider* s) {
  // src: src/pages/_layout/utils/style-helpers.tsx:241-375 get3DSliderStyle
  s->apply_theme();
}

void style_input(TextField* f) {
  // src: src/utils/button-styles/input.ts get3DInputStyle
  f->apply_theme();
  f->style_.size = 13;
  f->style_.align_h = AlignH::Center;  // src: src/pages/_layout/components/basic-settings-card.tsx:238-251
  f->caret_color_ = f->style_.color;
}

}  // namespace

// ---------- TakeoverCard ----------
// src: src/pages/_layout/components/takeover-mode-card.tsx:53-76
TakeoverCard::TakeoverCard() : Widget(nullptr) {
  auto t = std::make_unique<Label>();
  title_ = t.get();
  title_->style.size = 13;  // src: src/pages/_layout/components/takeover-mode-card.tsx:60
  title_->style.weight = static_cast<int>(Weight::Bold);
  title_->style.align_v = AlignV::Center;
  title_->style.color = detail::card_text();
  title_->set_i18n_text("settings.mini.takeoverMode");
  add_child(std::move(t));
  auto seg = std::make_unique<TakeoverSegmented>();
  seg_ = seg.get();
  seg_->set_a11y_labelled_by(title_);
  add_child(std::move(seg));
  auto hint = std::make_unique<Label>();
  hint_ = hint.get();
  hint_->set_visible(false);
  add_child(std::move(hint));
}

void TakeoverCard::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: src/pages/_layout/components/takeover-mode-card.tsx:51-76
  title_->layout_tree({0, 0, area.w, 20.41f});
  seg_->layout_tree({0, 26.41f, area.w, 26.0f});
}

// ---------- RoutingCard ----------
// src: src/pages/_layout/components/routing-preference-card.tsx:53-75
RoutingCard::RoutingCard() : Widget(nullptr) {
  auto t = std::make_unique<Label>();
  title_ = t.get();
  title_->style.size = 13;
  title_->style.weight = static_cast<int>(Weight::Bold);
  title_->style.align_v = AlignV::Center;
  title_->style.color = detail::card_text();
  title_->set_i18n_text("settings.mini.proxyMethod");
  add_child(std::move(t));
  auto seg = std::make_unique<Segmented>();
  seg_ = seg.get();
  seg_->set_options_i18n({"settings.mini.globalProxy", "settings.mini.gfwlist",
                          "settings.mini.addUrl"}, 1);
  // src: locales/zh/settings.json:567-569
  seg_->set_tooltips_i18n({"settings.mini.globalProxyTooltip",
                           "settings.mini.gfwlistTooltip",
                           "settings.mini.addUrlTooltip"});
  style_gold_segmented(seg_);
  seg_->style_.size = detail::routing_font_for_lang();
  seg_->set_a11y_labelled_by(title_);
  add_child(std::move(seg));
  auto hint = std::make_unique<Label>();
  hint_ = hint.get();
  hint_->set_visible(false);
  add_child(std::move(hint));
  auto ab = std::make_unique<Button>(L"＋ 添加自定义分流规则");
  add_rule_btn_ = ab.get();
  ab->set_visible(false);
  add_child(std::move(ab));
}

void RoutingCard::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: src/pages/_layout/components/routing-preference-card.tsx:53-75
  title_->layout_tree({0, 0, area.w, 20.41f});
  seg_->style_.size = detail::routing_font_for_lang();
  seg_->layout_tree({0, 26.41f, area.w, 26.0f});
}

// ---------- BasicCard ----------
// src: src/pages/_layout/components/basic-settings-card.tsx:89-255
BasicCard::BasicCard() : Widget(nullptr) {
  auto t = std::make_unique<Label>();
  title_ = t.get();
  title_->style.size = 13;  // src: src/pages/_layout/components/basic-settings-card.tsx:158
  title_->style.weight = static_cast<int>(Weight::Bold);
  title_->style.align_v = AlignV::Center;
  title_->style.color = detail::card_text();
  title_->set_i18n_text("settings.components.verge.basic.title");
  add_child(std::move(t));
  struct Def {
    const char* i18n_key;
    const wchar_t* literal;
    SwitchW** slot;
  } defs[4] = {{"settings.sections.system.fields.autoLaunch", nullptr, &sw_auto_launch_},
               {nullptr, L"Allow LAN", &sw_allow_lan_},
               {"settings.sections.system.fields.silentStart", nullptr, &sw_silent_start_},
               {nullptr, L"Allow IPv6", &sw_allow_ipv6_}};
  for (int i = 0; i < 4; ++i) {
    auto lb = std::make_unique<Label>();
    labels_[i] = lb.get();
    labels_[i]->style.size = 13;  // src: src/pages/_layout/components/basic-settings-card.tsx:240
    labels_[i]->style.weight = static_cast<int>(Weight::Bold);
    labels_[i]->style.align_v = AlignV::Center;  // src: src/pages/_layout/components/basic-settings-card.tsx:25
    labels_[i]->style.color = detail::card_text();
    labels_[i]->set_hit_self(true);
    if (defs[i].i18n_key) {
      labels_[i]->set_i18n_text(defs[i].i18n_key);
      labels_[i]->set_i18n_tooltip(defs[i].i18n_key);
    } else {
      labels_[i]->set_text(defs[i].literal);
      labels_[i]->set_tooltip_text(defs[i].literal);
    }
    add_child(std::move(lb));
    auto sw = std::make_unique<SwitchW>();
    *defs[i].slot = sw.get();
    sw->set_a11y_labelled_by(labels_[i]);
    add_child(std::move(sw));
  }
  auto sl = std::make_unique<Label>();
  sniffer_label_ = sl.get();
  sniffer_label_->style.size = 13.0f;  // src: src/pages/_layout/components/basic-settings-card.tsx:41
  sniffer_label_->style.weight = static_cast<int>(Weight::Bold);
  sniffer_label_->style.align_v = AlignV::Center;  // src: src/pages/_layout/components/basic-settings-card.tsx:33
  sniffer_label_->style.wrap = false;
  sniffer_label_->style.ellipsis = true;
  sniffer_label_->style.color = detail::card_text();
  sniffer_label_->set_hit_self(true);
  sniffer_label_->set_i18n_text("settings.mini.tunSniffer");
  add_child(std::move(sl));
  auto ss = std::make_unique<Segmented>();
  sniffer_seg_ = ss.get();
  sniffer_seg_->set_options_i18n({"settings.mini.tunSnifferAuto",
                                  "settings.mini.tunSnifferOn",
                                  "settings.mini.tunSnifferOff"},
                                 0);
  style_gold_segmented(sniffer_seg_);
  sniffer_seg_->radius_ = 4.0f;      // src: src/pages/_layout/components/basic-settings-card.tsx:327
  sniffer_seg_->style_.size = 11.0f;  // src: src/pages/_layout/components/basic-settings-card.tsx:372
  sniffer_seg_->set_a11y_labelled_by(sniffer_label_);
  add_child(std::move(ss));
  set_tun_enabled(false);
  auto pl = std::make_unique<Label>();
  port_label_ = pl.get();
  port_label_->style.size = 13;  // src: src/pages/_layout/components/basic-settings-card.tsx:240
  port_label_->style.weight = static_cast<int>(Weight::Bold);
  port_label_->style.align_v = AlignV::Center;  // src: src/pages/_layout/components/basic-settings-card.tsx:214
  port_label_->style.color = detail::card_text();
  port_label_->set_text(L"Mixed Port");
  add_child(std::move(pl));
  auto pf = std::make_unique<TextField>();
  port_field_ = pf.get();
  style_input(port_field_);
  port_field_->set_padding_x(4.0f);  // src: src/pages/_layout/components/basic-settings-card.tsx:511
  port_field_->set_text(std::to_wstring(static_cast<uint32_t>(config::consts::kDefaultMixed)));
  port_field_->set_a11y_labelled_by(port_label_);
  add_child(std::move(pf));
  auto btn = std::make_unique<Button>();
  btn->set_i18n_text("shared.actions.save");
  save_btn_ = btn.get();
  btn->set_visible(false);
  add_child(std::move(btn));
}

void BasicCard::set_tun_enabled(bool tun) {
  tun_enabled_ = tun;
  const float op = tun ? 1.0f : 0.48f;  // src: src/pages/_layout/components/basic-settings-card.tsx:304
  const char* tip_key = tun ? "settings.mini.tunSnifferTooltip"
                            : "settings.mini.tunSnifferNeedTunTooltip";  // src: src/pages/_layout/components/basic-settings-card.tsx:287-295
  if (sniffer_label_) {
    sniffer_label_->set_opacity(op);
    sniffer_label_->set_i18n_tooltip(tip_key);
  }
  if (sniffer_seg_) {
    sniffer_seg_->set_enabled(tun);
    sniffer_seg_->set_opacity(op);
    sniffer_seg_->set_i18n_tooltip(tip_key);
  }
  invalidate();
}

HCURSOR BasicCard::cursor(const PointF& local) const {
  if (!tun_enabled_ && sniffer_seg_ && sniffer_seg_->bounds().contains(local)) {
    // src: src/pages/_layout/components/basic-settings-card.tsx:392
    return LoadCursorW(nullptr, IDC_NO);
  }
  return Widget::cursor(local);
}

bool BasicCard::on_mouse(const MouseEvent& e) {
  if (!tun_enabled_ && sniffer_seg_ && sniffer_seg_->bounds().contains(e.pos)) {
    if (e.type == MouseEventType::Down && (e.buttons & kMouseLeft)) {
      return true;
    }
    if (e.type == MouseEventType::Up &&
        (e.buttons == 0 || (e.buttons & kMouseLeft))) {
      // src: src/pages/_layout/components/basic-settings-card.tsx:416-420
      events::NoticeService::instance().push(
          "warning", tr8("settings.mini.tunSnifferNeedTunTooltip"));
      return true;
    }
  }
  return Widget::on_mouse(e);
}

// src: src/pages/_layout/components/basic-settings-card.tsx:89-255
void BasicCard::on_arrange(const RectF& area) {
  set_bounds(area);
  const float pad = detail::kCardPad;
  // src: src/pages/_layout/components/basic-settings-card.tsx:101-113
  title_->layout_tree({pad, pad, area.w - pad * 2, 20.4f});
  const float grid_y = pad + 20.4f + 4.0f;
  // src: src/pages/_layout/components/basic-settings-card.tsx:129
  const float gap_x = 12.0f;
  const float col_w = (area.w - pad * 2.0f - gap_x) / 2.0f;
  const float row_h = 23.98f;
  SwitchW* sws[4] = {sw_auto_launch_, sw_allow_lan_, sw_silent_start_,
                     sw_allow_ipv6_};
  // src: src/pages/_layout/components/basic-settings-card.tsx:147-150
  const auto sp = theme::ThemeService::instance().switch_paint();
  const float sw_scale = 0.85f;
  const float sw_w = sp.track_w * sw_scale;
  const float sw_h = sp.track_h * sw_scale;
  const float row_pad_y = 1.2f;
  const float row_content_h = 21.58f;
  for (int i = 0; i < 4; ++i) {
    const bool is_col1 = (i % 2) != 0;
    const float cx = pad + (is_col1 ? (col_w + gap_x) : 0.0f);
    const float cy = grid_y + (i / 2) * row_h;
    labels_[i]->style.size = 13.0f;   // src: src/pages/_layout/components/basic-settings-card.tsx:31-36
    labels_[i]->style.weight = static_cast<int>(Weight::Bold);
    labels_[i]->style.align_v = AlignV::Center;
    labels_[i]->style.wrap = false;
    labels_[i]->style.ellipsis = true;
    const float lb_w = std::max(0.0f, col_w - sw_w - 13.0f);  // src: src/pages/_layout/components/basic-settings-card.tsx:29
    labels_[i]->layout_tree({cx + 4.0f, cy, lb_w, row_h});
    const float sw_cy = cy + row_pad_y + row_content_h * 0.5f;
    sws[i]->layout_tree({cx + col_w - 5.0f - sw_w, sw_cy - sw_h * 0.5f, sw_w, sw_h});
  }
  // src: src/pages/_layout/components/basic-settings-card.tsx:284-392
  const float sniffer_row_h = 26.4f;
  const float sniffer_y = grid_y + row_h * 2.0f;
  const float seg_w = 122.0f;
  const float seg_h = 24.0f;
  if (sniffer_label_) {
    sniffer_label_->layout_tree({pad + 4.0f, sniffer_y,
                                 std::max(0.0f, area.w - pad * 2.0f - 134.0f),
                                 sniffer_row_h});
  }
  if (sniffer_seg_) {
    sniffer_seg_->layout_tree(  // src: src/pages/_layout/components/basic-settings-card.tsx:324-329
        {area.w - pad - 4.0f - seg_w, sniffer_y + (sniffer_row_h - seg_h) / 2.0f,
         seg_w, seg_h});
  }
  // src: src/pages/_layout/components/basic-settings-card.tsx:398
  const float port_fh = 27.0f;
  const float port_fw = 88.0f;
  const float mixed_h = port_fh + 2.4f;
  const float mixed_y = sniffer_y + sniffer_row_h + 2.0f;
  port_label_->layout_tree({pad + 4.0f, mixed_y,
                            std::max(0.0f, area.w - pad * 2.0f - port_fw - 12.0f),
                            mixed_h});
  // src: src/pages/_layout/components/basic-settings-card.tsx:508-522
  port_field_->layout_tree({area.w - pad - 4.0f - port_fw,
                            mixed_y + (mixed_h - port_fh) / 2.0f, port_fw, port_fh});
}

// ---------- ThemeCard ----------
// src: src/pages/_layout/components/theme-settings-card.tsx:136-476
ThemeCard::ThemeCard() : Widget(nullptr) {
  auto t = std::make_unique<Label>();
  title_ = t.get();
  title_->style.size = 13;
  title_->style.weight = static_cast<int>(Weight::Bold);
  title_->style.align_v = AlignV::Center;
  title_->style.color = detail::card_text();
  title_->set_i18n_text("settings.components.verge.theme.title");
  title_->set_i18n_tooltip("settings.components.verge.theme.title");
  add_child(std::move(t));
  const wchar_t* skins[] = {L"3D", L"Org", L"Mod", L"Fro", L"Cyber", L"Mono"};
  for (int i = 0; i < 6; ++i) {
    auto b = std::make_unique<Button>(skins[i]);
    b->set_visible(false);
    skin_buttons_.push_back(b.get());
    add_child(std::move(b));
  }
  // src: src/pages/_layout/components/theme-settings-card.tsx:427-448
  auto lb = std::make_unique<Button>();
  lb->set_i18n_text("settings.mini.debugLogs");
  logs_btn_ = lb.get();
  // src: src/utils/button-styles/button.ts:35
  lb->themed_ = true;
  lb->theme_color_type_ = "primary";
  lb->text_style.size = 13;
  lb->text_style.weight = 700;
  add_child(std::move(lb));
  auto mode = std::make_unique<Segmented>();
  mode_seg_ = mode.get();
  mode_seg_->set_options_i18n({"settings.sections.appearance.system",
                               "settings.sections.appearance.light",
                               "settings.sections.appearance.dark"}, 0);
  style_gold_segmented_small(mode_seg_);
  mode_seg_->set_a11y_labelled_by(title_);
  add_child(std::move(mode));
  auto dl = std::make_unique<Label>();
  depth_label_ = dl.get();
  depth_label_->style.size = 13;
  depth_label_->style.font_family.clear();
  depth_label_->style.align_v = AlignV::Center;
  depth_label_->style.color = detail::card_text();
  depth_label_->set_text(
      theme::skin_slider_spec(ThemeService::instance().skin()).label1);
  add_child(std::move(dl));
  auto ds = std::make_unique<Slider>();
  depth_slider_ = ds.get();
  {
    const auto spec =
        theme::skin_slider_spec(ThemeService::instance().skin());
    depth_slider_->min_ = 0;
    depth_slider_->max_ = spec.max1;
    depth_slider_->step_ = 0.1f;
  }
  style_slider(depth_slider_);
  depth_slider_->set_a11y_labelled_by(depth_label_);
  add_child(std::move(ds));
  auto dv = std::make_unique<Label>();
  depth_value_ = dv.get();
  dv->style.size = 12;
  dv->style.weight = static_cast<int>(Weight::Bold);
  dv->style.font_family.clear();
  dv->style.align_v = AlignV::Center;
  dv->style.align_h = AlignH::Right;
  dv->style.color = detail::slider_value_color();
  dv->set_text(SettingsDrawer::fmt_factor(0.0f));
  add_child(std::move(dv));
  auto vl = std::make_unique<Label>();
  vibrancy_label_ = vl.get();
  vibrancy_label_->style.size = 13;
  vibrancy_label_->style.font_family.clear();
  vibrancy_label_->style.align_v = AlignV::Center;
  vibrancy_label_->style.color = detail::card_text();
  vibrancy_label_->set_text(
      theme::skin_slider_spec(ThemeService::instance().skin()).label2);
  add_child(std::move(vl));
  auto vs = std::make_unique<Slider>();
  vibrancy_slider_ = vs.get();
  {
    const auto spec =
        theme::skin_slider_spec(ThemeService::instance().skin());
    vibrancy_slider_->min_ = 0;
    vibrancy_slider_->max_ = spec.max2;
    vibrancy_slider_->step_ = 0.1f;
  }
  style_slider(vibrancy_slider_);
  vibrancy_slider_->set_a11y_labelled_by(vibrancy_label_);
  add_child(std::move(vs));
  auto vv = std::make_unique<Label>();
  vibrancy_value_ = vv.get();
  vv->style.size = 12;
  vv->style.weight = static_cast<int>(Weight::Bold);
  vv->style.font_family.clear();
  vv->style.align_v = AlignV::Center;
  vv->style.align_h = AlignH::Right;
  vv->style.color = detail::slider_value_color();
  vv->set_text(SettingsDrawer::fmt_factor(0.0f));
  add_child(std::move(vv));
  // src: src/pages/_layout/components/theme-settings-card.tsx:450-476
  auto cp = std::make_unique<Label>();
  copyright_ = cp.get();
  copyright_->style.size = 11;
  copyright_->style.align_h = AlignH::Center;
  copyright_->style.align_v = AlignV::Center;
  copyright_->style.color =
      detail::is_light() ? Color4f::hex(0x3C3C43, 0.45f)
                         : Color4f::hex(0xEBEBF5, 0.45f);
  // src: locales/zh/settings.json:203
  copyright_->set_i18n_text("settings.components.verge.theme.copyright");
  add_child(std::move(cp));
  auto em = std::make_unique<Label>();
  email_ = em.get();
  email_->style.size = 10;
  email_->style.align_h = AlignH::Center;
  email_->style.align_v = AlignV::Center;
  Color4f pc = theme::ThemeService::instance().primary();
  pc.a = 0.75f;
  email_->style.color = pc;
  email_->set_text(L"qiuyuxiao@gmail.com");
  add_child(std::move(em));
}

// src: src/pages/_layout/components/theme-settings-card.tsx:148-476
void ThemeCard::on_arrange(const RectF& area) {
  set_bounds(area);
  const float pad = detail::kCardPad;
  const float seg_w = 122.0f, seg_h = 24.0f;
  const float header_h = 24.0f;
  mode_seg_->layout_tree({area.w - pad - 4.0f - seg_w, pad, seg_w, seg_h});
  title_->layout_tree(
      {pad, pad, std::max(40.0f, area.w - pad * 2 - seg_w - 8.0f), header_h});
  const float row1_y = pad + header_h + 4.0f;
  const float row_h = 23.2f;
  const float slider_w = 100.0f;
  const float wx = pad + 4.0f;
  const float content_w = area.w - wx * 2 - slider_w - 8.0f;
  const float val_w = 40.0f;
  depth_label_->layout_tree({wx, row1_y, content_w - val_w, row_h});
  depth_value_->layout_tree({wx + content_w - val_w, row1_y, val_w, row_h});
  depth_slider_->layout_tree({area.w - wx - slider_w, row1_y, slider_w, row_h});
  vibrancy_label_->layout_tree({wx, row1_y + row_h, content_w - val_w, row_h});
  vibrancy_value_->layout_tree({wx + content_w - val_w, row1_y + row_h, val_w, row_h});
  vibrancy_slider_->layout_tree({area.w - wx - slider_w, row1_y + row_h,
                                 slider_w, row_h});
  SizeF lb_sz = logs_btn_->on_measure(area.w, 24.0f);
  const float logs_w = lb_sz.w + 8.0f;
  logs_btn_->layout_tree({(area.w - logs_w) / 2.0f, row1_y + row_h * 2 + 8,
                          logs_w, 24.0f});
  const float foot_top = logs_btn_->bounds().bottom();
  const float foot_y = foot_top + 1.0f + 8.0f;
  copyright_->layout_tree({pad, foot_y, area.w - pad * 2, 18.26f});
  email_->layout_tree({pad, foot_y + 18.26f, area.w - pad * 2, 16.6f});
}

void ThemeCard::on_paint(IRenderTarget& rt) {
  // src: src/pages/_layout/components/theme-settings-card.tsx:459-476
  const float fa = footer_hover_ ? 1.0f : 0.75f;
  if (copyright_) {
    copyright_->style.color =
        detail::is_light() ? Color4f::hex(0x3C3C43, 0.6f * fa)
                           : Color4f::hex(0xEBEBF5, 0.6f * fa);
  }
  if (email_) {
    Color4f ec = theme::ThemeService::instance().primary();
    ec.a = fa;
    email_->style.color = ec;
  }
  Widget::on_paint(rt);
  if (!copyright_) return;
  const float y = copyright_->bounds().y - 9.0f;
  const float x0 = detail::kCardPad, x1 = bounds().w - detail::kCardPad;
  if (y > 0.0f && x1 > x0) {
    // src: node_modules/@mui/material/styles/createPalette.js:34,71
    Brush dash{detail::divider_color()};
    for (float x = x0; x + 3.0f < x1; x += 6.0f) {
      rt.fill_rect({x, y, 3.0f, 1.0f}, dash);
    }
  }
  if (email_ && email_hover_ && text_engine()) {
    const RectF eb = email_->bounds();
    const float tw =
        text_engine()->measure_text(email_->text, email_->style, 0.0f).w;
    const float ux = eb.x + std::max(0.0f, (eb.w - tw) * 0.5f);
    const float uy = eb.y + (eb.h + email_->style.size) * 0.5f + 1.0f;
    if (tw > 0.0f) {
      rt.fill_rect({ux, uy, std::min(tw, eb.w), 1.0f},
                   Brush{email_->style.color});
    }
  }
}

HCURSOR ThemeCard::cursor(const PointF& local) const {
  if (email_ && email_->bounds().contains(local)) {
    return LoadCursorW(nullptr, IDC_HAND);
  }
  return Widget::cursor(local);
}

bool ThemeCard::on_mouse(const MouseEvent& e) {
  const bool in_foot =
      copyright_ && email_ &&
      RectF{detail::kCardPad, copyright_->bounds().y - 9.0f,
            std::max(0.0f, bounds().w - detail::kCardPad * 2.0f),
            std::max(0.0f, email_->bounds().bottom() -
                               (copyright_->bounds().y - 9.0f))}
          .contains(e.pos);
  const bool in_email = email_ && email_->bounds().contains(e.pos);
  if (e.type == MouseEventType::Move || e.type == MouseEventType::Enter) {
    if (in_foot != footer_hover_ || in_email != email_hover_) {
      footer_hover_ = in_foot;
      email_hover_ = in_email;
      invalidate();
    }
  } else if (e.type == MouseEventType::Leave) {
    if (footer_hover_ || email_hover_) {
      footer_hover_ = false;
      email_hover_ = false;
      invalidate();
    }
  } else if (e.type == MouseEventType::Down && in_email &&
             (e.buttons & kMouseLeft)) {
    return true;
  } else if (e.type == MouseEventType::Up && in_email &&
             (e.buttons == 0 || (e.buttons & kMouseLeft))) {
    // src: src/pages/_layout/components/theme-settings-card.tsx:467-473
    ShellExecuteW(nullptr, L"open", L"mailto:qiuyuxiao@gmail.com", nullptr,
                  nullptr, SW_SHOWNORMAL);
    return true;
  }
  return Widget::on_mouse(e);
}

}  // namespace clashwin::ui::scene
