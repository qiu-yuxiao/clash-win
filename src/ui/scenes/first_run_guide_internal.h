#pragma once
// 首次运行向导共享几何常量、配色与绘制辅助（拆分自 first_run_guide_dialog.cpp）
// src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:1-120
#include <windows.h>

#include <algorithm>
#include <string>

#include "ui/button_draw.h"
#include "ui/card_draw.h"
#include "ui/render_target.h"
#include "ui/theme/theme.h"
#include "util/dirs.h"
#include "util/node.h"
#include "util/str_utils.h"
#include "util/utf.h"

namespace clashwin::ui::scene::first_run_detail {

// ============================================================================
// 全部几何/字号常量逐项对照
// src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:1-120
// ============================================================================
inline constexpr float kCardMaxW = 520.0f;      // src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:180
inline constexpr float kCardRadius = 8.0f;      // src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:180
inline constexpr float kTitlePadX = 14.0f;      // src: src/utils/popup-styles.ts:198 px: 1.75
inline constexpr float kTitlePadY = 12.0f;      // src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:180 py: 1.5
inline constexpr float kTitleSize = 15.5f;      // src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:180 fontSize '15.5px'
inline constexpr float kTitleGap = 4.8f;        // src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:180 gap: 0.6
inline constexpr float kTitleLineH = 20.9f;     // src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:180 lineHeight 1.35 * 15.5
inline constexpr float kBadgeFont = 10.5f;      // src: src/pages/_layout/components/first-run-guide-dialog/step-badge.tsx:25 fontSize '10.5px'
inline constexpr float kBadgePadX = 8.0f;       // src: src/pages/_layout/components/first-run-guide-dialog/step-badge.tsx:23 px: 1
inline constexpr float kBadgePadY = 1.6f;       // src: src/pages/_layout/components/first-run-guide-dialog/step-badge.tsx:24 py: 0.2
inline constexpr float kBadgeW = 86.0f;         // 豁免: Mini 无对应物 — 徽标宽按内容自适应，无 width 字面量（出处: src/pages/_layout/components/first-run-guide-dialog/step-badge.tsx:20）
inline constexpr float kBadgeH = 17.8f;         // src: src/pages/_layout/components/first-run-guide-dialog/step-badge.tsx:20
inline constexpr float kTitleBlockH = 68.5f;    // src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:180
inline constexpr float kContentPad = 14.0f;     // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:30
inline constexpr float kIntroSize = 13.0f;      // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:35 fontSize '13px'
inline constexpr float kIntroH = 41.6f;         // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:35
inline constexpr float kIntroMb = 12.0f;        // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:35 mb: 1.5
inline constexpr float kPillFont = 11.5f;       // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:55
inline constexpr float kPillPadY = 3.2f;        // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:55
inline constexpr float kPillH = 22.2f;          // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:55
inline constexpr float kPillGap = 6.0f;         // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:45
inline constexpr float kPillMb = 14.0f;         // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:45
inline constexpr float kBannerPadX = 10.0f;     // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:75
inline constexpr float kBannerPadY = 6.0f;      // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:75
inline constexpr float kBannerFont = 12.0f;     // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:90
inline constexpr float kBannerH = 31.4f;        // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:75
inline constexpr float kBannerMb = 10.0f;       // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:75
inline constexpr float kFieldH = 40.0f;         // src: node_modules/@mui/material/OutlinedInput/OutlinedInput.js:158-163
inline constexpr float kFieldMb = 8.0f;         // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:100
inline constexpr float kPasteFont = 12.0f;      // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:150
inline constexpr float kPasteH = 18.4f;         // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:150
inline constexpr float kPasteW = 150.0f;        // 豁免: Mini 无对应物 — 按钮宽按文字 + px 8*2 自适应（出处: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:140）
inline constexpr float kPasteMt = 8.0f;         // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:140
inline constexpr float kHintFont = 11.5f;       // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:160
inline constexpr float kHintH = 16.1f;          // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:160
inline constexpr float kHintGap = 4.0f;         // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:140
inline constexpr float kActionPadY = 12.0f;     // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:175
inline constexpr float kActionGap = 8.0f;       // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:175
inline constexpr float kPrimaryBtnH = 34.0f;    // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:180
inline constexpr float kSecondaryBtnH = 30.0f;  // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:195
inline constexpr float kActionsH = 96.4f;       // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:175
inline constexpr float kRadiusBtn = 6.0f;       // src: src/utils/button-styles/button.ts:35
inline constexpr float kInputStepH = 357.0f;    // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:1-200
inline constexpr float kReadyNodeH = 206.0f;    // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:40
inline constexpr float kReadyStarH = 134.0f;    // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:160
inline constexpr float kReadyGap = 12.0f;       // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:45

// 绿色卡内部（src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx）
inline constexpr float kCardPad = 12.0f;        // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:40
inline constexpr float kImportedFont = 13.5f;   // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:55
inline constexpr float kImportedH = 20.25f;     // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:55
inline constexpr float kImportedMb = 4.0f;      // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:55
inline constexpr float kStatusMt = 4.0f;        // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:65
inline constexpr float kStatusFont = 12.5f;     // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:75
inline constexpr float kStatusH = 37.5f;        // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:75
inline constexpr float kNodeMt = 8.0f;          // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:90
inline constexpr float kNodePt = 6.8f;          // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:90
inline constexpr float kNodeLabelH = 16.0f;     // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:100
inline constexpr float kNodeGap = 3.2f;         // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:95
inline constexpr float kNodeNameH = 18.2f;      // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:125
inline constexpr float kRetestMt = 8.8f;        // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:140
inline constexpr float kRetestH = 29.4f;        // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:140
inline constexpr float kWarnMt = 6.0f;          // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:155
inline constexpr float kWarnH = 18.0f;          // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:155
inline constexpr float kStarTitleFont = 13.5f;  // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:175
inline constexpr float kStarTitleH = 19.6f;     // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:175
inline constexpr float kStarBodyFont = 12.5f;   // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:185
inline constexpr float kStarBodyH = 58.2f;      // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:185
inline constexpr float kStarBtnMt = 6.0f;       // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:195
inline constexpr float kStarBtnH = 22.0f;       // src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:200
inline constexpr float kStarBtnW = 130.0f;      // 豁免: Mini 无对应物 — 按钮宽按文字自适应，无 width 字面量（出处: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:195）

inline bool is_retro() {
  return theme::ThemeService::instance().skin() == theme::SkinId::Retro3d;
}
inline bool is_light() {
  return theme::ThemeService::instance().is_light();
}

// src: src/utils/popup-styles.ts:50-84 getPopupSkinTokens
inline Color4f secondary_text() {
  return theme::ThemeService::instance().popup_tokens().secondary_text;
}
inline Color4f disabled_text() {
  return theme::ThemeService::instance().popup_tokens().disabled_text;
}
inline Color4f ready_color() {
  return theme::ThemeService::instance().popup_tokens().ready_color;
}
inline Color4f warning_color() {
  return theme::ThemeService::instance().popup_tokens().warning_color;
}
// src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:120
inline Color4f star_accent_color() {
  if (is_retro()) return Color4f::hex(0x7A3E00);
  return is_light() ? Color4f::hex(0xB26A00) : Color4f::hex(0xFFD54F);
}
inline Color4f card_text_color() {
  return theme::ThemeService::instance().popup_tokens().primary_text;
}
inline Color4f divider_color() {
  return theme::ThemeService::instance().popup_tokens().divider_color;
}

inline void draw_text(IRenderTarget& rt, const std::wstring& text, const RectF& r,
                      float size, int weight, const Color4f& color,
                      AlignH ah = AlignH::Left, AlignV av = AlignV::Center,
                      bool wrap = false, bool ellipsis = false) {
  TextStyle ts;
  ts.size = size;
  ts.weight = weight;
  ts.color = color;
  ts.align_h = ah;
  ts.align_v = av;
  ts.wrap = wrap;
  ts.ellipsis = ellipsis;
  rt.draw_text(text, r, ts);
}

// src: src/utils/popup-styles.ts:90-117 getPopupCardStyle
inline void paint_card(IRenderTarget& rt, const RectF& r) {
  ::clashwin::ui::draw_card(
      rt, r, theme::ThemeService::instance().popup_card_paint());
}

enum class BtnStyle { Primary, OutlinedDefault, OutlinedPrimary };

// src: src/utils/button-styles/button.ts:1-120
inline void paint_button(IRenderTarget& rt, const RectF& orig_r, BtnStyle style,
                         const std::wstring& label, float font_size,
                         bool hover = false, bool pressed = false) {
  auto& svc = theme::ThemeService::instance();
  const char* ctype = style == BtnStyle::Primary ? "primary" : "default";
  const bool outlined = style != BtnStyle::Primary;
  const auto bp = svc.button_paint(ctype, outlined);

  float ty = 0.0f;
  if (pressed) ty = bp.translate_active;
  else if (hover) ty = bp.translate_hover;

  RectF r = {orig_r.x, orig_r.y + ty, orig_r.w, orig_r.h};
  draw_button_skin(rt, r, bp, hover ? 1.0f : 0.0f, pressed);

  if (bp.text_shadow.a > 0 && !pressed) {
    TextStyle ts_sh;
    ts_sh.size = font_size;
    ts_sh.weight = bp.weight;
    ts_sh.color = bp.text_shadow;
    ts_sh.align_h = AlignH::Center;
    ts_sh.align_v = AlignV::Center;
    RectF sr{r.x, r.y + bp.text_shadow_dy, r.w, r.h};
    rt.draw_text(label, sr, ts_sh);
  }
  draw_text(rt, label, r, font_size, bp.weight, bp.text, AlignH::Center, AlignV::Center);
}

// src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:65
inline void dashed_rect(IRenderTarget& rt, const RectF& r, const Color4f& c) {
  const float dash = 4.0f;  // 豁免: Mini 无对应物 — CSS dashed 无引擎图元，段长 4（出处: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:65）
  const float gap = 3.0f;   // 豁免: Mini 无对应物 — CSS dashed 无引擎图元，段距 3（出处: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:65）
  Brush b{c};
  for (float x = r.x; x < r.right(); x += dash + gap)
    rt.fill_rect({x, r.y, std::min(dash, r.right() - x), 1.0f}, b);
  for (float x = r.x; x < r.right(); x += dash + gap)
    rt.fill_rect({x, r.y + r.h - 1.0f, std::min(dash, r.right() - x), 1.0f}, b);
  for (float y = r.y; y < r.bottom(); y += dash + gap) {
    rt.fill_rect({r.x, y, 1.0f, std::min(dash, r.bottom() - y)}, b);
    rt.fill_rect({r.x + r.w - 1.0f, y, 1.0f, std::min(dash, r.bottom() - y)}, b);
  }
}

// src: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:90
inline void dashed_line(IRenderTarget& rt, float x, float y, float w, const Color4f& c) {
  const float dash = 4.0f;  // 豁免: Mini 无对应物 — CSS dashed 无引擎图元，段长 4（出处: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:90）
  const float gap = 3.0f;   // 豁免: Mini 无对应物 — CSS dashed 无引擎图元，段距 3（出处: src/pages/_layout/components/first-run-guide-dialog/ready-step.tsx:90）
  Brush b{c};
  for (float dx = x; dx < x + w; dx += dash + gap)
    rt.fill_rect({dx, y, std::min(dash, x + w - dx), 1.0f}, b);
}

inline std::string read_clipboard_url() {
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

  std::string trimmed = util::trim(res);
  if (!trimmed.empty() &&
      clashwin::util::is_supported_subscription_or_node_input(trimmed)) {
    return trimmed;
  }
  return "";
}

inline std::wstring guide_storage_path() {
  return util::dirs::app_home_dir() + L"\\clash-win-guide.json";
}

}  // namespace clashwin::ui::scene::first_run_detail
