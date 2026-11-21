// 首次运行向导两步布局、绘制与鼠标/键盘交互（拆分自 first_run_guide_dialog.cpp）
// src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:1-200
#include "first_run_guide_dialog.h"
#include "first_run_guide_internal.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>

#include "ui/i18n_text.h"
#include "ui/theme/theme.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

namespace d = first_run_detail;
using theme::ThemeService;

// src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:45-65
float FirstRunGuideDialog::layout_pills(float pills_y, float inner_x, float inner_w) {
  const std::wstring texts[3] = {
      tr("layout.components.firstRun.featureAutoSelect"),
      tr("layout.components.firstRun.featureDnsGuard"),
      tr("layout.components.firstRun.featureStealth")};
  float cw3[3];
  for (int i = 0; i < 3; ++i) {
    float tw = 0;
    if (ITextEngine* te = text_engine()) {
      TextStyle ts;
      ts.size = d::kPillFont;
      tw = te->measure_text(texts[i], ts, 0).w;
    } else {
      tw = static_cast<float>(texts[i].size()) * d::kPillFont;
    }
    cw3[i] = tw + 16.0f + 2.0f;
  }
  pill_rects_.clear();
  float py = pills_y;
  for (int i = 0; i < 3;) {
    int j = i;
    float sum = 0;
    while (j < 3) {
      const float add = cw3[j] + (j > i ? d::kPillGap : 0.0f);
      if (sum + add > inner_w && j > i) break;
      sum += add;
      ++j;
    }
    const int cnt = j - i;
    const float extra = std::max(0.0f, inner_w - sum) / static_cast<float>(cnt);
    float px = inner_x;
    for (int k = i; k < j; ++k) {
      const float w = cw3[k] + extra;
      pill_rects_.push_back({px, py, w, d::kPillH});
      px += w + d::kPillGap;
    }
    py += d::kPillH + d::kPillGap;
    i = j;
  }
  return pill_rects_.empty() ? d::kPillH : (py - d::kPillGap - pills_y);
}

// src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:160-210
void FirstRunGuideDialog::on_arrange(const RectF& area) {
  set_bounds(area);
  if (url_menu_) url_menu_->set_bounds({0, 0, area.w, area.h});
  set_bounds(area);
  // src: src/utils/popup-styles.ts:43-50
  const float cw = std::min(theme::kPopupPaneWidth, area.w);
  const bool has_banner =
      step_ == FirstRunStep::Input && detected_clipboard_url_.empty() == false &&
      url_.empty();
  banner_h_ = d::kBannerH;
  if (has_banner) {
    if (ITextEngine* te = text_engine()) {
      TextStyle bts;
      bts.size = d::kBannerFont;
      bts.weight = static_cast<int>(Weight::Bold);
      const float avail_w = (cw - d::kTitlePadX * 2.0f) - d::kBannerPadX * 2.0f;
      const SizeF b_sz = te->measure_text(L"✨ " + tr("layout.components.firstRun.clipboardDetected"), bts, 0);
      if (b_sz.w > avail_w) {
        banner_h_ = 46.0f;
      }
    }
  }
  {
    const float inner_w = cw - d::kTitlePadX * 2.0f;
    float h = d::kIntroH;
    if (ITextEngine* te = text_engine()) {
      TextStyle ts;
      ts.size = d::kIntroSize;
      ts.wrap = true;
      const SizeF s = te->measure_text(tr("layout.components.firstRun.intro"), ts, inner_w);
      if (s.h > 0) h = std::max(d::kIntroH, s.h);
    }
    intro_h_ = h;
  }
  float pills_extra = 0.0f;
  if (step_ == FirstRunStep::Input) {
    pills_h_ = layout_pills(0.0f, 0.0f, cw - d::kTitlePadX * 2.0f);
    pills_extra = std::max(0.0f, pills_h_ - d::kPillH);
  }
  float node_h = d::kReadyNodeH;
  float star_h = d::kReadyStarH;
  status_h_ = d::kStatusH;
  star_title_h_ = d::kStarTitleH;
  star_body_h_ = d::kStarBodyH;
  // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:40
  const float card_text_w = std::max(60.0f, cw - d::kTitlePadX * 2.0f - d::kCardPad * 2.0f);
  if (step_ == FirstRunStep::Ready) {
    const bool ready = proxy_ready();
    const bool unconf = unconfirmed();
    if (ITextEngine* te = text_engine()) {
      const std::wstring st_txt =
          ready ? tr("layout.components.firstRun.readyOk")
                : (unconf ? tr("layout.components.firstRun.readyUnconfirmed")
                          : tr("layout.components.firstRun.checking"));
      TextStyle sts;
      sts.size = d::kStatusFont;
      sts.weight = static_cast<int>(Weight::Bold);
      sts.wrap = true;
      const float st_w = card_text_w - (ready || unconf ? 0.0f : 18.0f);  // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:65
      const SizeF ss = te->measure_text(st_txt, sts, st_w);
      if (ss.h > 0) status_h_ = std::max(d::kStatusH, ss.h + 2.0f);

      TextStyle tts;
      tts.size = d::kStarTitleFont;
      tts.weight = static_cast<int>(Weight::Bold);
      tts.wrap = true;
      const SizeF ts_sz = te->measure_text(
          tr("layout.components.firstRun.starTitle"), tts, card_text_w);
      if (ts_sz.h > 0) star_title_h_ = std::max(d::kStarTitleH, ts_sz.h + 2.0f);

      TextStyle bts;
      bts.size = d::kStarBodyFont;
      bts.wrap = true;
      const SizeF bs_sz = te->measure_text(
          tr("layout.components.firstRun.wikiHint"), bts, card_text_w);
      if (bs_sz.h > 0) star_body_h_ = std::max(d::kStarBodyH, bs_sz.h + 4.0f);
    }
    star_h = d::kCardPad + star_title_h_ + d::kImportedMb + star_body_h_ + d::kStarBtnMt +
             d::kStarBtnH + d::kCardPad;
  }
  if (step_ == FirstRunStep::Ready) {
    const bool show_node = !active_node_name_.empty() && (proxy_ready() || unconfirmed());
    const bool show_retest = !proxy_ready() && (unconfirmed() || retesting_);
    const bool show_warn = !takeover_active_;
    float ny = d::kCardPad;
    ny += d::kImportedH + d::kImportedMb;
    ny += d::kStatusMt + status_h_;
    if (show_node) ny += d::kNodeMt + d::kNodePt + 1.0f + d::kNodeLabelH + d::kNodeGap + d::kNodeNameH;
    if (show_retest) ny += d::kRetestMt + d::kRetestH;
    if (show_warn) ny += d::kWarnMt + d::kWarnH;
    node_h = ny + d::kCardPad;
  }
  const bool has_err = (step_ == FirstRunStep::Input && !error_message_.empty());
  float err_block_h = 0.0f;
  if (has_err) {
    float err_text_h = 16.8f;
    if (ITextEngine* te = text_engine()) {
      TextStyle ets;
      ets.size = d::kBannerFont;
      ets.weight = static_cast<int>(Weight::Bold);
      ets.wrap = true;
      err_text_h = std::max(
          err_text_h, te->measure_text(L"⚠️ " + utf::utf8_to_utf16(error_message_), ets,
                                       cw - d::kTitlePadX * 2.0f).h);
    }
    err_block_h = 4.0f + err_text_h;
  }
  const float ready_cards_h = node_h + d::kReadyGap + star_h;
  float ch = step_ == FirstRunStep::Input
                 ? d::kInputStepH + (intro_h_ - d::kIntroH) + pills_extra +
                       (has_banner ? banner_h_ + d::kBannerMb : 0.0f) + err_block_h
                 : d::kTitleBlockH + d::kContentPad + ready_cards_h + d::kContentPad + d::kActionsH;
  ch = std::min(ch, area.h - 8.0f);  // 豁免: Mini 无对应物 — 窄高视口防溢出安全留边 4px*2（出处: 无）

  const float cx = 0.0f;  // src: src/utils/popup-styles.ts:43-50 贴左停靠
  const float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  const float inner_x = cx + d::kTitlePadX;
  const float inner_w = cw - d::kTitlePadX * 2.0f;
  const float content_y = cy + d::kTitleBlockH + d::kContentPad;

  if (step_ == FirstRunStep::Input) {
    max_ready_scroll_ = 0.0f;
    ready_scroll_y_ = 0.0f;
    const float pills_y = content_y + intro_h_ + d::kIntroMb;
    pills_h_ = layout_pills(pills_y, inner_x, inner_w);

    const float field_y = pills_y + pills_h_ + d::kPillMb +
                          (has_banner ? banner_h_ + d::kBannerMb : 0.0f);
    banner_clip_rect_ = {inner_x, pills_y + pills_h_ + d::kPillMb, inner_w, banner_h_};
    input_rect_ = {inner_x, field_y, inner_w, d::kFieldH};
    if (url_field_) {
      url_field_->set_visible(open_);
      url_field_->set_bounds({input_rect_.x, field_y, inner_w, d::kFieldH});
    }
    btn_enter_rect_ = {input_rect_.right() - 26.0f, field_y + 12.0f, 16.0f, 16.0f};

    const float paste_y = field_y + d::kFieldH + err_block_h + d::kPasteMt;
    btn_paste_rect_ = {cx + (cw - d::kPasteW) / 2.0f, paste_y, d::kPasteW, d::kPasteH};
    const float actions_top = cy + ch - d::kActionsH;
    btn_submit_rect_ = {inner_x, actions_top + d::kActionPadY, inner_w, d::kPrimaryBtnH};
    btn_later_rect_ = {inner_x, btn_submit_rect_.bottom() + d::kActionGap, inner_w,
                       d::kSecondaryBtnH};
  } else {
    if (url_field_) {
      url_field_->set_visible(false);
      url_field_->blur();
    }
    const float pre_actions = cy + ch - d::kActionsH;
    const float viewport_h = std::max(40.0f, (pre_actions - content_y) - d::kContentPad);
    max_ready_scroll_ = std::max(0.0f, ready_cards_h - viewport_h);
    ready_scroll_y_ = std::clamp(ready_scroll_y_, 0.0f, max_ready_scroll_);
    const float scroll_off = ready_scroll_y_;

    // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:40-200
    const float node_y = content_y - scroll_off;
    card_node_rect_ = {inner_x, node_y, inner_w, node_h};
    const bool show_node = !active_node_name_.empty() && (proxy_ready() || unconfirmed());
    const bool show_retest = !proxy_ready() && (unconfirmed() || retesting_);
    const bool show_warn = !takeover_active_;
    float ny = node_y + d::kCardPad + d::kImportedH + d::kImportedMb + d::kStatusMt + status_h_;
    if (show_node) ny += d::kNodeMt + d::kNodePt + 1.0f + d::kNodeLabelH + d::kNodeGap + d::kNodeNameH;
    if (show_retest) {
      ny += d::kRetestMt;
      btn_retest_rect_ = {card_node_rect_.x + d::kCardPad, ny,
                          card_node_rect_.w - d::kCardPad * 2.0f, d::kRetestH};
      ny += d::kRetestH;
    }
    warn_y_ = show_warn ? (ny + d::kWarnMt) : ny;

    float star_y = node_y + node_h + d::kReadyGap;
    if (star_y + star_h > pre_actions - 4.0f) {
      const float avail = pre_actions - 4.0f - star_y;
      if (avail < 70.0f) {
        star_y = std::max(content_y + node_h + 2.0f, pre_actions - 4.0f - star_h);  // 豁免: Mini 无对应物 — 极矮视口下防底栏重叠安全间距（出处: 无）
      }
      star_h = std::max(40.0f, pre_actions - 4.0f - star_y);  // 豁免: Mini 无对应物 — 极矮视口受限时卡片最小保底高度（出处: 无）
    }
    card_star_rect_ = {inner_x, star_y, inner_w, star_h};
    float wiki_w = d::kStarBtnW;
    if (ITextEngine* te = text_engine()) {
      TextStyle wts;
      wts.size = d::kHintFont;
      wts.weight = static_cast<int>(Weight::Bold);
      const SizeF ws = te->measure_text(
          L"📖 " + tr("layout.components.firstRun.openWiki") + L" ↗", wts, 0);
      wiki_w = std::min(inner_w - d::kCardPad * 2.0f, std::max(d::kStarBtnW, ws.w + 12.0f));
    }
    const float wiki_y = card_star_rect_.bottom() - d::kCardPad - d::kStarBtnH;
    btn_wiki_rect_ = {card_star_rect_.right() - d::kCardPad - wiki_w, wiki_y,
                      wiki_w, d::kStarBtnH};

    const float actions_top = cy + ch - d::kActionsH;
    btn_finish_star_rect_ = {inner_x, actions_top + d::kActionPadY, inner_w, d::kPrimaryBtnH};
    btn_finish_rect_ = {inner_x, btn_finish_star_rect_.bottom() + d::kActionGap, inner_w,
                        d::kSecondaryBtnH};
  }
}

void FirstRunGuideDialog::set_ready_scroll_y(float y) {
  const float clamped = std::clamp(y, 0.0f, max_ready_scroll_);
  if (std::abs(clamped - ready_scroll_y_) > 0.1f) {
    ready_scroll_y_ = clamped;
    if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
    invalidate();
  }
}

// src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:160-210
void FirstRunGuideDialog::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();
  set_opacity(fade_.value());
  if (fade_.hidden()) return;

  // src: src/utils/popup-styles.ts:120-168
  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  rt.fill_rect({0, 0, pane_w, bounds().h},
               Brush{Color4f(0, 0, 0, 0.5f)});

  d::paint_card(rt, card_rect_);
  const Color4f text_color = d::card_text_color();

  const std::wstring title =
      step_ == FirstRunStep::Input ? L"🎉 " + tr("layout.components.firstRun.title") : L"🚀 " + tr("layout.components.firstRun.readyTitle");
  d::draw_text(rt, title,
               {card_rect_.x, card_rect_.y + d::kTitlePadY, card_rect_.w, d::kTitleLineH},
               d::kTitleSize, static_cast<int>(Weight::Bold),
               step_ == FirstRunStep::Input ? text_color : d::ready_color(), AlignH::Center);

  // src: src/pages/_layout/components/first-run-guide-dialog/step-badge.tsx:20-48
  const RectF badge_r = {card_rect_.x + (card_rect_.w - d::kBadgeW) / 2.0f,
                         card_rect_.y + d::kTitlePadY + d::kTitleLineH + d::kTitleGap,
                         d::kBadgeW, d::kBadgeH};
  const Color4f badge_bg = d::is_retro() ? Color4f::hex(0x2C1F03, 0.12f)
                                         : Color4f::hex(0x808080, 0.14f);
  const Color4f badge_bd = d::is_retro() ? Color4f::hex(0x2C1F03, 0.22f)
                                         : Color4f::hex(0x808080, 0.24f);
  Brush badge_b{badge_bg};
  rt.round_rect(badge_r, badge_r.h / 2.0f, Pen{badge_bd, 1.0f}, &badge_b);
  Brush dot_b{step_ == FirstRunStep::Input ? ThemeService::instance().primary()
                                           : d::ready_color()};
  rt.round_rect({badge_r.x + d::kBadgePadX, badge_r.y + (badge_r.h - 6.0f) / 2.0f, 6.0f,
                 6.0f},
                3.0f, {}, &dot_b);
  d::draw_text(rt, step_ == FirstRunStep::Input ? L"STEP 1 / 2" : L"STEP 2 / 2",
               {badge_r.x + d::kBadgePadX + 6.0f + d::kPillGap, badge_r.y,
                badge_r.w - d::kBadgePadX - 6.0f - d::kPillGap, badge_r.h},
               d::kBadgeFont, static_cast<int>(Weight::Bold), d::secondary_text(), AlignH::Left);

  rt.fill_rect({card_rect_.x, card_rect_.y + d::kTitleBlockH - 1.0f, card_rect_.w, 1.0f},
               Brush{d::divider_color()});

  const float content_y = card_rect_.y + d::kTitleBlockH + d::kContentPad;
  const float inner_x = card_rect_.x + d::kTitlePadX;
  const float inner_w = card_rect_.w - d::kTitlePadX * 2.0f;

  if (step_ == FirstRunStep::Input) {
    // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:30-200
    d::draw_text(rt,
                 tr("layout.components.firstRun.intro"),
                 {inner_x, content_y, inner_w, intro_h_}, d::kIntroSize,
                 static_cast<int>(Weight::Normal), d::secondary_text(), AlignH::Left,
                 AlignV::Top, true);

    const float pills_y = content_y + intro_h_ + d::kIntroMb;
    if (pill_rects_.size() != 3) layout_pills(pills_y, inner_x, inner_w);
    const Color4f pill_bg = d::is_retro() ? Color4f::hex(0x2C1F03, 0.10f)
                                          : (d::is_light() ? Color4f::hex(0x000000, 0.04f)
                                                           : Color4f::hex(0xFFFFFF, 0.06f));
    const Color4f pill_bd = d::is_retro() ? Color4f::hex(0x2C1F03, 0.20f)
                                          : d::divider_color();
    const std::wstring pills[3] = {
        tr("layout.components.firstRun.featureAutoSelect"),
        tr("layout.components.firstRun.featureDnsGuard"),
        tr("layout.components.firstRun.featureStealth")};
    for (int i = 0; i < 3 && i < static_cast<int>(pill_rects_.size()); ++i) {
      const RectF pr = pill_rects_[static_cast<size_t>(i)];
      Brush pb{pill_bg};
      rt.round_rect(pr, 6.0f, Pen{pill_bd, 1.0f}, &pb);
      d::draw_text(rt, pills[i], pr, d::kPillFont, static_cast<int>(Weight::Bold), text_color,
                   AlignH::Center);
    }

    if (!detected_clipboard_url_.empty() && url_.empty()) {
      const float banner_y = pills_y + pills_h_ + d::kPillMb + (hover_banner_ ? -1.0f : 0.0f);
      const RectF br = {inner_x, banner_y, inner_w, banner_h_};
      banner_clip_rect_ = br;
      // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:65
      const Color4f b_bg = d::is_retro()
                               ? Color4f::hex(0x1B5E20, hover_banner_ ? 0.18f : 0.12f)
                               : (d::is_light() ? Color4f::hex(0x2E7D32, hover_banner_ ? 0.15f : 0.10f)
                                                : Color4f::hex(0x2E7D32, hover_banner_ ? 0.24f : 0.18f));
      const Color4f b_bd = d::is_retro() ? Color4f::hex(0x1B5E20, 0.45f) : d::ready_color();
      Brush bbg{b_bg};
      rt.round_rect(br, 6.0f, {}, &bbg);
      d::dashed_rect(rt, br, b_bd);
      d::draw_text(rt, L"✨ " + tr("layout.components.firstRun.clipboardDetected"),
                   {br.x + d::kBannerPadX, br.y + 4.0f, br.w - d::kBannerPadX * 2.0f, br.h - 8.0f},
                   d::kBannerFont, static_cast<int>(Weight::Bold), d::ready_color(),
                   AlignH::Center, AlignV::Center, true);
    }

    d::draw_text(rt, L"⏎", btn_enter_rect_, 16.0f,
                 static_cast<int>(Weight::Normal),
                 hover_enter_ ? ThemeService::instance().primary() : d::disabled_text(),
                 AlignH::Center);

    d::draw_text(rt, L"📋 " + tr("layout.components.firstRun.paste"),
                 {btn_paste_rect_.x, btn_paste_rect_.y + (pressed_paste_ ? 1.0f : 0.0f),
                  btn_paste_rect_.w, btn_paste_rect_.h}, d::kPasteFont,
                 static_cast<int>(Weight::Bold),
                 hover_paste_ ? d::warning_color() : ThemeService::instance().primary(),
                 AlignH::Center);

    if (!error_message_.empty()) {
      const float err_y = input_rect_.bottom() + 4.0f;
      const float err_h = std::max(16.8f, btn_paste_rect_.y - d::kPasteMt - err_y);
      d::draw_text(rt, L"⚠️ " + utf::utf8_to_utf16(error_message_),
                   {inner_x, err_y, inner_w, err_h},
                   d::kBannerFont, static_cast<int>(Weight::Bold),
                   ThemeService::instance().palette().error_color,
                   AlignH::Left, AlignV::Top, true);
    }
    d::draw_text(rt, tr("layout.components.firstRun.enterHint"),
                 {inner_x, btn_paste_rect_.bottom() + d::kHintGap, inner_w, d::kHintH},
                 d::kHintFont, static_cast<int>(Weight::Normal), d::disabled_text(),
                 AlignH::Center);

    rt.fill_rect({card_rect_.x, card_rect_.y + card_rect_.h - d::kActionsH,
                  card_rect_.w, 1.0f},
                 Brush{d::divider_color()});
    // src: locales/zh/layout.json:8
    d::paint_button(rt, btn_submit_rect_, d::BtnStyle::Primary,
                    tr("layout.components.firstRun.importAndStart"), 13.0f,
                    hover_submit_, pressed_submit_);
    d::paint_button(rt, btn_later_rect_, d::BtnStyle::OutlinedDefault,
                    tr("layout.components.firstRun.later"), 12.5f,
                    hover_later_, pressed_later_);

  } else {
    // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:30-220
    const float vp_top = card_rect_.y + d::kTitleBlockH;
    const float vp_h = std::max(0.0f, card_rect_.bottom() - d::kActionsH - vp_top);
    rt.push_clip({card_rect_.x, vp_top, card_rect_.w, vp_h});
    const RectF& nc = card_node_rect_;
    const Color4f node_bg = Color4f::hex(0x4CAF50, d::is_light() ? 0.08f : 0.12f);
    const Color4f node_bd = Color4f::hex(0x4CAF50, d::is_light() ? 0.28f : 0.32f);
    Brush nb{node_bg};
    rt.round_rect(nc, 8.0f, Pen{node_bd, 1.0f}, &nb);

    const float imported_y = nc.y + d::kCardPad;
    d::draw_text(rt, L"✅ " + tr("layout.components.firstRun.imported"),
                 {nc.x + d::kCardPad, imported_y, nc.w - d::kCardPad * 2.0f, d::kImportedH},
                 d::kImportedFont, static_cast<int>(Weight::Bold), d::ready_color());

    const bool ready = proxy_ready();
    const bool unconfirmed = this->unconfirmed();
    const float status_y = imported_y + d::kImportedH + d::kImportedMb + d::kStatusMt;
    if (!ready && !unconfirmed) {
      rt.round_rect({nc.x + d::kCardPad, status_y + 2.0f, 12.0f, 12.0f}, 6.0f,
                    Pen{d::secondary_text(), 1.0f}, nullptr);
      d::draw_text(rt, L"◌", {nc.x + d::kCardPad, status_y, 14.0f, 18.0f}, d::kStatusFont,
                   static_cast<int>(Weight::Normal), d::secondary_text(), AlignH::Center);
    }
    const Color4f status_color =
        ready ? d::ready_color() : (unconfirmed ? d::warning_color() : d::secondary_text());
    const std::wstring status_text =
        ready ? tr("layout.components.firstRun.readyOk")
              : (unconfirmed
                     ? tr("layout.components.firstRun.readyUnconfirmed")
                     : tr("layout.components.firstRun.checking"));
    d::draw_text(rt, status_text,
                 {nc.x + d::kCardPad + (ready || unconfirmed ? 0.0f : 18.0f), status_y,
                  nc.w - d::kCardPad * 2.0f - (ready || unconfirmed ? 0.0f : 18.0f), status_h_},
                 d::kStatusFont, static_cast<int>(Weight::Bold), status_color, AlignH::Left,
                 AlignV::Top, true);

    const float node_top = status_y + status_h_ + d::kNodeMt;
    if (!active_node_name_.empty() && (ready || unconfirmed)) {
      d::dashed_line(rt, nc.x + d::kCardPad, node_top + d::kNodePt, nc.w - d::kCardPad * 2.0f,
                     Color4f::hex(0x808080, 0.25f));
      const float label_y = node_top + d::kNodePt + 1.0f;
      d::draw_text(rt, L"🌐 " + tr("layout.components.firstRun.activeNodeLabel"),
                   {nc.x + d::kCardPad, label_y, 140.0f, d::kNodeLabelH},
                   d::kHintFont, static_cast<int>(Weight::Bold), d::secondary_text());
      if (ready) {
        const RectF db = {nc.right() - d::kCardPad - 72.0f, label_y - 1.0f, 72.0f, 18.0f};
        Brush dbb{Color4f::hex(0x4CAF50, 0.18f)};
        rt.round_rect(db, db.h / 2.0f, {}, &dbb);
        d::draw_text(rt, L"⚡ " + std::to_wstring(active_delay_ms_) + L" ms", db, 11.0f,
                     static_cast<int>(Weight::Bold), d::ready_color(), AlignH::Center);
      }
      d::draw_text(rt, utf::utf8_to_utf16(active_node_name_),
                   {nc.x + d::kCardPad, label_y + d::kNodeLabelH + d::kNodeGap,
                    nc.w - d::kCardPad * 2.0f, d::kNodeNameH},
                   13.0f, static_cast<int>(Weight::Bold),
                   text_color, AlignH::Left, AlignV::Center,
                   false, true);
    }

    if (!ready && (unconfirmed || retesting_)) {
      d::paint_button(rt, btn_retest_rect_, d::BtnStyle::OutlinedPrimary,
                      L"🔄 " + tr("layout.components.firstRun.retestNodes"), 12.5f,
                      hover_retest_, pressed_retest_);
    }

    if (!takeover_active_) {
      d::draw_text(rt, L"⚠️ " + tr("layout.components.firstRun.sysProxyOff"),
                   {nc.x + d::kCardPad, warn_y_, nc.w - d::kCardPad * 2.0f, d::kWarnH}, 12.0f,
                   static_cast<int>(Weight::Bold), d::warning_color());
    }

    const RectF& sc = card_star_rect_;
    const Color4f star_bg = d::is_retro()
                                ? Color4f::hex(0xFFFFFF, 0.22f)
                                : Color4f::hex(0xFFC107, d::is_light() ? 0.10f : 0.09f);
    const Color4f star_bd = d::is_retro() ? Color4f::hex(0x7A3E00, 0.35f)
                                          : Color4f::hex(0xFFC107, 0.45f);
    Brush sb{star_bg};
    rt.round_rect(sc, 8.0f, Pen{star_bd, 1.0f}, &sb);

    d::draw_text(rt, tr("layout.components.firstRun.starTitle"),
                 {sc.x + d::kCardPad, sc.y + d::kCardPad, sc.w - d::kCardPad * 2.0f, star_title_h_},
                 d::kStarTitleFont, static_cast<int>(Weight::Bold), d::star_accent_color(), AlignH::Left, AlignV::Top, true);
    const float body_y = sc.y + d::kCardPad + star_title_h_ + d::kImportedMb;
    const float body_max_h = std::max(12.0f, btn_wiki_rect_.y - 2.0f - body_y);
    d::draw_text(rt,
                 tr("layout.components.firstRun.wikiHint"),
                 {sc.x + d::kCardPad, body_y,
                  sc.w - d::kCardPad * 2.0f, body_max_h},
                 d::kStarBodyFont, static_cast<int>(Weight::Normal), d::secondary_text(),
                 AlignH::Left, AlignV::Top, true, true);
    d::draw_text(rt, L"📖 " + tr("layout.components.firstRun.openWiki") + L" ↗",
                 {btn_wiki_rect_.x, btn_wiki_rect_.y + (pressed_wiki_ ? 1.0f : 0.0f),
                  btn_wiki_rect_.w, btn_wiki_rect_.h}, d::kHintFont,
                 static_cast<int>(Weight::Bold),
                 hover_wiki_ ? d::warning_color() : ThemeService::instance().primary(),
                 AlignH::Right);

    rt.pop_clip();

    rt.fill_rect({card_rect_.x, card_rect_.y + card_rect_.h - d::kActionsH,
                  card_rect_.w, 1.0f},
                 Brush{d::divider_color()});
    d::paint_button(rt, btn_finish_star_rect_, d::BtnStyle::Primary,
                    tr("layout.components.firstRun.openGithubStar"), 13.0f,
                    hover_finish_star_, pressed_finish_star_);
    d::paint_button(rt, btn_finish_rect_, d::BtnStyle::OutlinedDefault,
                    tr("layout.components.firstRun.finish"), 12.5f,
                    hover_finish_, pressed_finish_);
  }
}

// src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:280-360
bool FirstRunGuideDialog::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  const PointF pt = e.pos;
  const float vp_top = card_rect_.y + d::kTitleBlockH;
  const float vp_h = std::max(0.0f, card_rect_.bottom() - d::kActionsH - vp_top);
  const RectF clip_vp = {card_rect_.x, vp_top, card_rect_.w, vp_h};

  if (e.type == MouseEventType::Leave) {
    hover_banner_ = hover_paste_ = hover_submit_ = hover_later_ = hover_enter_ = false;
    hover_retest_ = hover_wiki_ = hover_finish_star_ = hover_finish_ = false;
    pressed_banner_ = pressed_paste_ = pressed_enter_ = pressed_submit_ = pressed_later_ = false;
    pressed_retest_ = pressed_wiki_ = pressed_finish_star_ = pressed_finish_ = false;
    invalidate();
    return true;
  }

  if (e.type == MouseEventType::Down) {
    if (step_ == FirstRunStep::Input) {
      if (!detected_clipboard_url_.empty() && url_.empty() &&
          banner_clip_rect_.contains(pt.x, pt.y)) {
        pressed_banner_ = true;
      }
      if (btn_paste_rect_.contains(pt.x, pt.y)) pressed_paste_ = true;
      if (btn_enter_rect_.contains(pt.x, pt.y)) pressed_enter_ = true;
      if (btn_submit_rect_.contains(pt.x, pt.y)) pressed_submit_ = true;
      if (btn_later_rect_.contains(pt.x, pt.y)) pressed_later_ = true;
    } else {
      if (clip_vp.contains(pt.x, pt.y)) {
        if (btn_retest_rect_.contains(pt.x, pt.y) &&
            !proxy_ready() && (unconfirmed() || retesting_)) {
          pressed_retest_ = true;
        }
        if (btn_wiki_rect_.contains(pt.x, pt.y)) pressed_wiki_ = true;
      }
      if (btn_finish_star_rect_.contains(pt.x, pt.y)) pressed_finish_star_ = true;
      if (btn_finish_rect_.contains(pt.x, pt.y)) pressed_finish_ = true;
    }
    invalidate();
    return true;
  }

  if (e.type == MouseEventType::Move) {
    if (step_ == FirstRunStep::Input) {
      const bool hb = !detected_clipboard_url_.empty() && url_.empty() &&
                      banner_clip_rect_.contains(pt.x, pt.y);
      const bool hp = btn_paste_rect_.contains(pt.x, pt.y);
      const bool hs = btn_submit_rect_.contains(pt.x, pt.y);
      const bool hl = btn_later_rect_.contains(pt.x, pt.y);
      const bool he = btn_enter_rect_.contains(pt.x, pt.y);
      if (hb != hover_banner_ || hp != hover_paste_ || hs != hover_submit_ ||
          hl != hover_later_ || he != hover_enter_) {
        hover_banner_ = hb;
        hover_paste_ = hp;
        hover_submit_ = hs;
        hover_later_ = hl;
        hover_enter_ = he;
        invalidate();
      }
    } else {
      const bool in_vp = clip_vp.contains(pt.x, pt.y);
      const bool hr = in_vp && btn_retest_rect_.contains(pt.x, pt.y) &&
                      !proxy_ready() && (unconfirmed() || retesting_);
      const bool hw = in_vp && btn_wiki_rect_.contains(pt.x, pt.y);
      const bool hfs = btn_finish_star_rect_.contains(pt.x, pt.y);
      const bool hf = btn_finish_rect_.contains(pt.x, pt.y);
      if (hr != hover_retest_ || hw != hover_wiki_ ||
          hfs != hover_finish_star_ || hf != hover_finish_) {
        hover_retest_ = hr;
        hover_wiki_ = hw;
        hover_finish_star_ = hfs;
        hover_finish_ = hf;
        invalidate();
      }
    }
    return true;
  }

  if (e.type == MouseEventType::Wheel) {
    if (step_ == FirstRunStep::Ready && max_ready_scroll_ > 0.0f &&
        card_rect_.contains(pt.x, pt.y)) {
      const float delta = static_cast<float>(e.wheel_delta) / 120.0f * 36.0f;  // 豁免: Mini 无对应物 — 桌面滚轮步长 36px（出处: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:30）
      set_ready_scroll_y(ready_scroll_y_ - delta);
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    pressed_banner_ = pressed_paste_ = pressed_enter_ = pressed_submit_ = pressed_later_ = false;
    pressed_retest_ = pressed_wiki_ = pressed_finish_star_ = pressed_finish_ = false;
    invalidate();

    if (is_importing_) return true;

    if (step_ == FirstRunStep::Input) {
      if (!detected_clipboard_url_.empty() && url_.empty() &&
          banner_clip_rect_.contains(pt.x, pt.y)) {
        set_url(detected_clipboard_url_);
        if (url_field_) url_field_->request_focus();
        return true;
      }
      if (btn_paste_rect_.contains(pt.x, pt.y)) {
        std::string clip = d::read_clipboard_url();
        if (!clip.empty()) {
          set_url(clip);
        } else if (url_field_) {
          url_field_->clear_text();
          url_field_->paste();
        }
        if (url_field_) url_field_->request_focus();
        return true;
      }
      if (btn_enter_rect_.contains(pt.x, pt.y) || btn_submit_rect_.contains(pt.x, pt.y)) {
        submit_import();
        return true;
      }
      if (btn_later_rect_.contains(pt.x, pt.y)) {
        finish();
        if (on_dismiss_later) on_dismiss_later();
        return true;
      }
    } else {
      if (clip_vp.contains(pt.x, pt.y) &&
          btn_retest_rect_.contains(pt.x, pt.y) &&
          !proxy_ready() && (unconfirmed() || retesting_)) {
        if (!retesting_) {
          retesting_ = true;
          gave_up_ = false;
          ready_started_ms_ = 0;
          last_poll_ms_ = 0;
          if (bounds().w > 0.0f && bounds().h > 0.0f) {
            on_arrange(bounds());
          }
          invalidate();
          if (on_retest_nodes) on_retest_nodes();
        }
        return true;
      }
      if (clip_vp.contains(pt.x, pt.y) && btn_wiki_rect_.contains(pt.x, pt.y)) {
        if (on_open_wiki) {
          on_open_wiki();
        } else {
          ShellExecuteW(nullptr, L"open",
                        L"https://github.com/qiu-yuxiao/clash-win/wiki", nullptr,
                        nullptr, SW_SHOWNORMAL);
        }
        return true;
      }
      if (btn_finish_star_rect_.contains(pt.x, pt.y)) {
        if (on_open_github) {
          on_open_github();
        } else {
          ShellExecuteW(nullptr, L"open", L"https://github.com/qiu-yuxiao/clash-win",
                        nullptr, nullptr, SW_SHOWNORMAL);
        }
        finish();
        return true;
      }
      if (btn_finish_rect_.contains(pt.x, pt.y)) {
        finish();
        return true;
      }
    }

    if (!card_rect_.contains(pt.x, pt.y)) {
      dismiss_via_close();
      return true;
    }
    return true;
  }

  if (e.type == MouseEventType::Down) return true;
  return true;
}

bool FirstRunGuideDialog::on_key(const KeyEvent& e) {
  if (!open_) return false;

  if (e.type == KeyEventType::Down) {
    if (e.key == VK_ESCAPE || e.key == 27) {
      dismiss_via_close();
      return true;
    }
    if (e.key == VK_RETURN || e.key == 13) {
      if (is_importing_) return true;
      if (step_ == FirstRunStep::Input) {
        submit_import();
      } else {
        finish();
      }
      return true;
    }
  }
  return false;
}

}  // namespace clashwin::ui::scene
