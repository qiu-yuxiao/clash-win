#include "first_run_guide_dialog.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

#include "cmd/command_service.h"
#include "config/config.h"
#include "config/profiles.h"
#include "ui/card_draw.h"
#include "ui/soft_shadow.h"
#include "ui/theme/theme.h"
#include "util/dirs.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01

namespace clashwin::ui::scene {

using theme::ThemeService;

namespace {

// ============================================================================
// 全部几何/字号常量逐项对照
// src/pages/_layout/components/first-run-guide-dialog.tsx
// ============================================================================
constexpr float kCardMaxW = 520.0f;      // src: :554 maxWidth '520px'
constexpr float kCardRadius = 8.0f;      // src: :555 borderRadius '8px'
constexpr float kTitlePadX = 14.0f;      // src: :566 px: 1.75
constexpr float kTitlePadY = 12.0f;      // src: :567 py: 1.5
constexpr float kTitleSize = 15.5f;      // src: :582 fontSize '15.5px'
constexpr float kTitleGap = 4.8f;        // src: :571 gap: 0.6
constexpr float kTitleLineH = 20.9f;     // src: :583 lineHeight 1.35 × 15.5
constexpr float kBadgeFont = 10.5f;      // src: :504 fontSize '10.5px'
constexpr float kBadgePadX = 8.0f;       // src: :502 px: 1
constexpr float kBadgePadY = 1.6f;       // src: :503 py: 0.2
constexpr float kBadgeW = 86.0f;         // 豁免: Mini 无对应物 — 徽标宽按内容自适应，无 width 字面量（出处: first-run-guide-dialog.tsx:503-514）
constexpr float kBadgeH = 17.8f;         // src: :503-514 py1.6×2 + 10.5 行高 + 1px 边框×2
constexpr float kTitleBlockH = 68.5f;    // src: :566-572 12+20.9+4.8+17.8+12+1
constexpr float kContentPad = 14.0f;     // src: :592 px/py 1.75
constexpr float kIntroSize = 13.0f;      // src: :599 fontSize '13px'
constexpr float kIntroH = 41.6f;         // src: :597 lineHeight 1.6 × 13 × 2 行（长句换行）
constexpr float kIntroMb = 12.0f;        // src: :598 mb: 1.5
constexpr float kPillFont = 11.5f;       // src: :629 fontSize '11.5px'
constexpr float kPillPadY = 3.2f;        // src: :627 py: 0.4
constexpr float kPillH = 22.2f;          // src: :627/:629 3.2×2 + 11.5 行高 + 1px 边框×2
constexpr float kPillGap = 6.0f;         // src: :611 gap: 0.75
constexpr float kPillMb = 14.0f;         // src: :612 mb: 1.75
constexpr float kBannerPadX = 10.0f;     // src: :660 px: 1.25
constexpr float kBannerPadY = 6.0f;      // src: :661 py: 0.75
constexpr float kBannerFont = 12.0f;     // src: :688 fontSize '12px'
constexpr float kBannerH = 31.4f;        // src: :661/:688 6×2 + 12×1.45 + 1px 边框×2
constexpr float kBannerMb = 10.0f;       // src: :659 mb: 1.25
constexpr float kFieldH = 40.0f;         // 豁免: MUI TextField size=small 默认输入框高 40
constexpr float kFieldMb = 8.0f;         // src: :697 mb: 1
// 输入框圆角/描边宽逐皮肤（get3DInputStyle）→ 运行时取 input_paint().radius /
//   .border_w，不再用固定常量。
constexpr float kPasteFont = 12.0f;      // src: :767 fontSize '12px'
constexpr float kPasteH = 18.4f;         // src: :771/:772 py: 0.25×2 + 12 行高
constexpr float kPasteW = 150.0f;        // 豁免: Mini 无对应物 — 按钮宽按文字 + px 8×2 自适应，无 width 字面量（出处: first-run-guide-dialog.tsx:750-760）
constexpr float kPasteMt = 8.0f;         // src: :757 mt: 1
constexpr float kHintFont = 11.5f;       // src: :782 fontSize '11.5px'
constexpr float kHintH = 16.1f;          // src: :783 lineHeight 1.4 × 11.5
constexpr float kHintGap = 4.0f;         // src: :756 gap: 0.5
constexpr float kActionPadY = 12.0f;     // src: :798 py: 1.5
constexpr float kActionGap = 8.0f;       // src: :799 gap: 1
constexpr float kPrimaryBtnH = 34.0f;    // src: :807 py: 0.85×2 + 13 行高 + 2px 边框×2
constexpr float kSecondaryBtnH = 30.0f;  // src: :821 py: 0.7×2 + 12.5 行高 + 2px 边框×2
constexpr float kActionsH = 96.4f;       // src: :798-800 12+34+8+30+12 + 1px 上边框
constexpr float kRadiusBtn = 6.0f;       // src: get3DButtonStyle :79 borderRadius '6px'
constexpr float kInputStepH = 357.0f;    // src: 68.5+14+(41.6+12+22.2+14+40+8+38.5)+14+96.4
constexpr float kReadyNodeH = 206.0f;    // src: :865-1016 绿色卡流式高（含重测/接管提示预留）
constexpr float kReadyStarH = 134.0f;    // src: :1019-1083 Star 卡流式高（正文 3 行）
constexpr float kReadyGap = 12.0f;       // src: :877 mb: 1.5

// 绿色卡内部（src: :862-1016）
constexpr float kCardPad = 12.0f;        // src: :866 p: 1.5
constexpr float kImportedFont = 13.5f;   // src: :886 fontSize '13.5px'
constexpr float kImportedH = 20.25f;     // src: :885 行高 1.5 × 13.5
constexpr float kImportedMb = 4.0f;      // src: :885 mb: 0.5
constexpr float kStatusMt = 4.0f;        // src: :898 mt: 0.5
constexpr float kStatusFont = 12.5f;     // src: :910 fontSize '12.5px'
constexpr float kStatusH = 37.5f;        // src: :909 lineHeight 1.5 × 12.5 × 2 行
constexpr float kNodeMt = 8.0f;          // src: :925 mt: 1
constexpr float kNodePt = 6.8f;          // src: :926 pt: 0.85
constexpr float kNodeLabelH = 16.0f;     // src: :945/:946 caption 11.5
constexpr float kNodeGap = 3.2f;         // src: :930 gap: 0.4
constexpr float kNodeNameH = 18.2f;      // src: :973 lineHeight 1.4 × 13
constexpr float kRetestMt = 8.8f;        // src: :989 mt: 1.1
constexpr float kRetestH = 29.4f;        // src: :990 py: 0.65×2 + 12.5 行高 + 2px 边框×2
constexpr float kWarnMt = 6.0f;          // src: :1008 mt: 0.75
constexpr float kWarnH = 18.0f;          // src: :1009 lineHeight 1.5 × 12
constexpr float kStarTitleFont = 13.5f;  // src: :1040 fontSize '13.5px'
constexpr float kStarTitleH = 19.6f;     // src: :1041 lineHeight 1.45 × 13.5
constexpr float kStarBodyFont = 12.5f;   // src: :1053 fontSize '12.5px'
constexpr float kStarBodyH = 58.2f;      // src: :1052 lineHeight 1.55 × 12.5 × 3 行
constexpr float kStarBtnMt = 6.0f;       // src: :1061 mt: 0.75
constexpr float kStarBtnH = 22.0f;       // src: :1072/:1073 py: 0.2×2 + 11.5 行高
constexpr float kStarBtnW = 130.0f;      // 豁免: Mini 无对应物 — 按钮宽按文字自适应，无 width 字面量（出处: first-run-guide-dialog.tsx:1065-1080）

// 前置声明：定义见本匿名命名空间后部（语义色 helper 依赖皮肤判断）
bool is_retro();
bool is_light();

// 语义色（src: :479-487）
Color4f secondary_text() {
  // src: :479 retro-3d 换 deep brown rgba(44,31,3,.78)，其余 text.secondary
  return is_retro() ? Color4f::hex(0x2C1F03, 0.78f)
                    : ThemeService::instance().palette().secondary_text;
}
Color4f disabled_text() {
  // src: :480 retro-3d rgba(44,31,3,.55)，其余 text.disabled
  return is_retro() ? Color4f::hex(0x2C1F03, 0.55f)
                    : ThemeService::instance().palette().secondary_text;
}
Color4f ready_color() {
  // src: :481 retro-3d #1B5E20，其余 success.main
  return is_retro() ? Color4f::hex(0x1B5E20)
                    : ThemeService::instance().palette().success_color;
}
Color4f warning_color() {
  // src: :482 retro-3d #8C4A03，其余 warning.main
  return is_retro() ? Color4f::hex(0x8C4A03)
                    : ThemeService::instance().palette().warning_color;
}
Color4f star_accent_color() {
  // src: :483-487 retro-3d #7A3E00；其余 dark #FFD54F / light #B26A00
  if (is_retro()) return Color4f::hex(0x7A3E00);
  return is_light() ? Color4f::hex(0xB26A00) : Color4f::hex(0xFFD54F);
}
Color4f card_text_color() {
  // src: utils/button-styles.ts:535/576 get3DCardStyle('default').color（6 套皮肤）
  return ThemeService::instance().card_paint("default").text_color;
}

bool is_retro() {
  return ThemeService::instance().skin() == theme::SkinId::Retro3d;
}
bool is_light() {
  return ThemeService::instance().is_light();
}

// src: MUI 默认 palette.divider light rgba(0,0,0,.12) / dark rgba(255,255,255,.12)
Color4f divider_color() {
  return is_light() ? Color4f::hex(0x000000, 0.12f) : Color4f::hex(0xFFFFFF, 0.12f);
}

void draw_text(IRenderTarget& rt, const std::wstring& text, const RectF& r,
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

// 3D 卡：6 套皮肤走 ThemeService::card_paint("default") 查表
//   （src: utils/button-styles.ts:492-731 get3DCardStyle）
void paint_card(IRenderTarget& rt, const RectF& r) {
  ::clashwin::ui::draw_card(rt, r,
                            ThemeService::instance().card_paint("default"));
}

enum class BtnStyle { Primary, OutlinedDefault, OutlinedPrimary };

// 按钮：6 套皮肤走 ThemeService::button_paint() 查表
//   （src: utils/button-styles.ts:22-490 get3DButtonStyle）
void paint_button(IRenderTarget& rt, const RectF& orig_r, BtnStyle style,
                  const std::wstring& label, float font_size,
                  bool hover = false, bool pressed = false) {
  auto& svc = ThemeService::instance();
  const char* ctype = style == BtnStyle::Primary ? "primary" : "default";
  const bool outlined = style != BtnStyle::Primary;
  const auto bp = svc.button_paint(ctype, outlined);
  const float d = svc.depth_factor();

  float ty = 0.0f;
  if (pressed) ty = bp.translate_active;
  else if (hover) ty = bp.translate_hover;

  RectF r = {orig_r.x, orig_r.y + ty, orig_r.w, orig_r.h};

  if (bp.has_shadow) {
    if (pressed) {
      Color4f ds = Color4f::hex(0x000000, svc.is_light() ? 0.45f : 0.65f);
      draw_soft_shadow(rt, r, bp.radius, ds, 1.0f * d, 1.5f * d);
    } else {
      float blur = hover ? (bp.shadow_blur * 1.3f) : bp.shadow_blur;
      draw_soft_shadow(rt, r, bp.radius, bp.shadow_color, bp.shadow_dy, blur);
    }
  }
  if (bp.glow.a > 0 && !pressed) {
    float glow_blur = hover ? (bp.glow_blur * 1.4f) : bp.glow_blur;
    draw_glow(rt, r, bp.radius, bp.glow, glow_blur);
  }

  if (bp.use_grad && bp.bevel_dark.a > 0 && !pressed) {
    Color4f ds = Color4f::hex(0x000000, svc.is_light() ? 0.35f : 0.60f);
    draw_soft_shadow(rt, r, bp.radius, ds, 3.0f * d, 2.0f * d);
    for (int i = 2; i >= 1; --i) {
      RectF br{r.x, r.y + i * d, r.w, r.h};
      Brush bb{bp.bevel_dark};
      rt.round_rect(br, bp.radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &bb);
    }
  }

  if (bp.use_grad) {
    const GradientStop* gs = (hover || pressed) ? bp.grad_hover : bp.grad;
    rt.fill_round_rect_gradient_angle(r, bp.radius, gs, bp.grad_count, bp.grad_angle);
  } else {
    Color4f fill = pressed ? bp.fill_pressed : (hover ? bp.fill_hover : bp.fill);
    Brush b{fill};
    rt.round_rect(r, bp.radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &b);
  }

  if (bp.border_w > 0.01f)
    rt.round_rect(r, bp.radius, Pen{bp.border, bp.border_w}, nullptr);

  if (bp.bevel_dark.a > 0) {
    if (pressed) {
      rt.draw_line({r.x + bp.radius, r.y + 1.0f},
                   {r.right() - bp.radius, r.y + 1.0f}, Pen{bp.bevel_dark, 1.0f});
      if (d > 1.0f) {
        rt.draw_line({r.x + bp.radius, r.y + 2.0f},
                     {r.right() - bp.radius, r.y + 2.0f}, Pen{bp.bevel_dark, 1.0f});
      }
    } else {
      rt.draw_line({r.x + bp.radius, r.y + 1.0f},
                   {r.right() - bp.radius, r.y + 1.0f}, Pen{bp.bevel_hi, 1.0f});
    }
  }

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

// src: :674-676 border 1px dashed（引擎无虚线图元，以离散段绘制）
void dashed_rect(IRenderTarget& rt, const RectF& r, const Color4f& c) {
  const float dash = 4.0f;  // 豁免: Mini 无对应物 — CSS dashed 无引擎图元，以 4/3 段模拟（出处: :674-676）
  const float gap = 3.0f;   // 豁免: Mini 无对应物 — 同上（dash/gap 4/3；出处: :674-676）
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

void dashed_line(IRenderTarget& rt, float x, float y, float w, const Color4f& c) {
  const float dash = 4.0f;  // 豁免: Mini 无对应物 — CSS dashed 无引擎图元，段长 4（出处: :674-676）
  const float gap = 3.0f;   // 豁免: Mini 无对应物 — 同上（gap 3；出处: :674-676）
  Brush b{c};
  for (float dx = x; dx < x + w; dx += dash + gap)
    rt.fill_rect({dx, y, std::min(dash, x + w - dx), 1.0f}, b);
}

std::string read_clipboard_url() {
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
  if (!trimmed.empty()) {
    std::string lower = util::to_lower(trimmed);
    if (lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0 ||
        lower.rfind("clash://", 0) == 0 || lower.rfind("ss://", 0) == 0 ||
        lower.rfind("ssr://", 0) == 0 || lower.rfind("vmess://", 0) == 0 ||
        lower.rfind("vless://", 0) == 0 || lower.rfind("trojan://", 0) == 0 ||
        lower.rfind("hysteria://", 0) == 0 || lower.rfind("hysteria2://", 0) == 0 ||
        lower.rfind("hy2://", 0) == 0 || lower.rfind("wireguard://", 0) == 0 ||
        lower.rfind("wg://", 0) == 0 || lower.rfind("tuic://", 0) == 0 ||
        lower.rfind("socks5://", 0) == 0 || lower.rfind("socks://", 0) == 0) {
      return trimmed;
    }
    if (lower.find("proxies:") != std::string::npos) {
      return trimmed;
    }
  }
  return "";
}

std::wstring guide_storage_path() {
  return util::dirs::app_home_dir() + L"\\clashwin-guide.json";
}

}  // namespace

bool FirstRunGuideDialog::is_first_run_completed() {
  std::ifstream f(guide_storage_path(), std::ios::binary);
  if (!f) return false;
  try {
    nlohmann::json j;
    f >> j;
    return j.value(kStorageKey, false);
  } catch (...) {
    return false;
  }
}

void FirstRunGuideDialog::set_first_run_completed(bool completed) {
  nlohmann::json j;
  j[kStorageKey] = completed;
  std::ofstream f(guide_storage_path(), std::ios::binary | std::ios::trunc);
  if (f) f << j.dump(2);
}

bool FirstRunGuideDialog::has_real_profile() {
  auto prf = config::profiles::load();
  if (!prf.items.has_value() || prf.items->empty()) return false;
  for (const auto& it : *prf.items) {
    auto t = it.type();
    if (t == config::PrfType::Remote || t == config::PrfType::Local) {
      return true;
    }
  }
  return false;
}

FirstRunGuideDialog::FirstRunGuideDialog(Widget* parent) : Widget(parent) {
  set_z_order(980);

  // 订阅链接 TextField（src: :698-732 真实 MUI TextField 可编辑组件）
  auto uf = std::make_unique<TextField>(this);
  url_field_ = uf.get();
  url_field_->set_placeholder(
      L"https://example.com/api/v1/client/subscribe?token=...");
  url_field_->apply_theme();  // ← get3DInputStyle 6 套皮肤查表
  url_field_->style_.size = 13;
  url_field_->height_ = kFieldH;  // src: size="small" 输入框高 40
  url_field_->right_pad_ = 28.0f;  // src: first-run-guide-dialog.tsx:714 endAdornment 预留
  url_field_->set_visible(false);  // 默认隐藏，open()/on_arrange 按步显隐
  url_field_->on_text_changed = [this](const std::wstring& t) {
    // src: :704-707 onChange：setUrl(e.target.value) + 清错误
    const bool was_empty = url_.empty();
    url_ = utf::utf16_to_utf8(t);
    error_message_.clear();
    if (was_empty != url_.empty() && bounds().w > 0.0f && bounds().h > 0.0f) {
      on_arrange(bounds());
    }
    invalidate();
  };
  url_field_->on_submit = [this]() {
    // src: :715-728 回车图标 endAdornment onClick → handleSubmitImport
    if (!is_importing_) submit_import();
  };
  url_field_->on_context_menu = [this](PointF anchor) {
    // src: :711 onContextMenu={handleContextMenu}
    open_url_menu(anchor);
  };
  add_child(std::move(uf));

  // 订阅链接右键编辑菜单（← profile_import_card 同款 open_url_menu）
  auto um = std::make_unique<ContextMenu>(this);
  url_menu_ = um.get();
  url_menu_->set_z_order(1050);  // src: MUI Menu zIndex 1300（高于 url_field_）
  add_child(std::move(um));
}

void FirstRunGuideDialog::open() {
  open_ = true;
  fade_.show();  // ← MUI Dialog 缺省 Fade（enter 225ms）
  // src: :286-298 resetForm：重新打开从导入步开始
  step_ = FirstRunStep::Input;
  is_importing_ = false;
  error_message_.clear();
  retesting_ = false;
  // 订阅框复位（src: :698-707 autoFocus + value 受控）
  if (url_field_) {
    url_field_->apply_theme();
    url_field_->style_.size = 13;  // src: first-run-guide-dialog.tsx:743 fontSize 13px
    url_field_->set_enabled(true);
    url_field_->set_text(L"");
    url_field_->set_visible(true);
    url_field_->request_focus();  // ← src: autoFocus
  }
  // src: :143-166/:209-244 就绪态全部复位（activeDelay=null、未超时、未自动重测）
  active_node_name_.clear();
  active_delay_ms_ = -1;
  gave_up_ = false;
  ready_started_ms_ = 0;
  last_poll_ms_ = 0;
  auto_retested_ = false;
  check_clipboard();
  if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
  invalidate();
}

void FirstRunGuideDialog::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();  // 画面延后 195ms 消失（MUI leave），交互立刻失效
  if (url_field_) { url_field_->blur(); url_field_->set_visible(false); }
  if (url_menu_ && url_menu_->is_open()) url_menu_->close();
  invalidate();
  if (on_dismiss) on_dismiss();
}

void FirstRunGuideDialog::set_step(FirstRunStep s) {
  step_ = s;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

void FirstRunGuideDialog::set_url(const std::string& u) {
  const bool was_empty = url_.empty();
  url_ = u;
  error_message_.clear();
  if (url_field_ && utf::utf16_to_utf8(url_field_->text()) != u) {
    url_field_->set_text(utf::utf8_to_utf16(u));
  }
  if (was_empty != url_.empty() && bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  error_message_.clear();
  // 反向同步到 TextField（剪贴板横幅/粘贴按钮改写 url_ 时同步显示）
  // 用「字段当前文本 != u」作递归护栏：避免 set_text→on_text_changed→set_url 死循环
  if (url_field_ && utf::utf16_to_utf8(url_field_->text()) != u) {
    url_field_->set_text(utf::utf8_to_utf16(u));
  }
  invalidate();
}

void FirstRunGuideDialog::check_clipboard() {
  std::string clip = read_clipboard_url();
  if (!clip.empty()) {
    detected_clipboard_url_ = clip;
  }
}

void FirstRunGuideDialog::set_active_delay_ms(int ms) {
  // -1 = null（未测出）；0 = 测速失败；>0 有效延迟（src: :143-166）
  active_delay_ms_ = ms;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

// src: :228-232 进入 ready 且未就绪、未重测：20s 后置 gaveUp
// src: :236-245 进入 ready 立即刷新一次，随后每 1.5s 轮询；
//            (gaveUp && !retesting) 时停止轮询
void FirstRunGuideDialog::tick(int64_t now_ms) {
  if (!open_ || step_ != FirstRunStep::Ready) return;
  if (proxy_ready()) return;
  if (retesting_) return;  // src: :229 retesting 短路（重测期间不计时/不轮询）
  if (gave_up_) return;    // src: :237 超时确认后停止轮询
  if (ready_started_ms_ == 0) ready_started_ms_ = now_ms;
  if (now_ms - ready_started_ms_ >= kReadyTimeoutMs) {
    gave_up_ = true;
    if (bounds().w > 0.0f && bounds().h > 0.0f) {
      on_arrange(bounds());
    }
    invalidate();
    return;
  }
  if (last_poll_ms_ == 0 || now_ms - last_poll_ms_ >= kReadyPollIntervalMs) {
    last_poll_ms_ = now_ms;
    if (on_refresh_proxy) on_refresh_proxy();
  }
}

void FirstRunGuideDialog::on_retest_finished() {
  if (!retesting_) return;
  retesting_ = false;
  // src: :228-245 重测结束后重新计时/恢复轮询
  ready_started_ms_ = 0;
  last_poll_ms_ = 0;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

// src: :173-174 isUnconfirmed = !proxyReady && (gaveUp || (!retesting && activeDelay === 0))
bool FirstRunGuideDialog::unconfirmed() const {
  return !proxy_ready() &&
         (gave_up_ || (!retesting_ && active_delay_ms_ == 0));
}

void FirstRunGuideDialog::submit_import() {
  if (is_importing_) return;

  std::string target_url = util::trim(url_);
  if (target_url.empty()) {
    target_url = util::trim(detected_clipboard_url_);
  }
  if (target_url.empty()) {
    std::string clip = read_clipboard_url();
    if (!clip.empty()) {
      target_url = clip;
      detected_clipboard_url_ = clip;
    }
  }

  // 仅要求非空：口径与设置页订阅导入框完全一致，同时支持 http(s) 订阅链接与直连节点（ss/vmess/vless/trojan/hy2/wg等十数种协议）
  if (target_url.empty()) {
    error_message_ = "请输入或粘贴订阅链接或节点配置";
    invalidate();
    return;
  }

  // src: :703 disabled={submitting} —— 校验通过后导入期间禁用订阅框
  if (url_field_) url_field_->set_enabled(false);

  is_importing_ = true;
  error_message_.clear();
  invalidate();

  bool success = false;
  std::string err_msg;
  if (on_import) {
    success = on_import(target_url);
  } else {
    // 默认通过 CommandService 导入
    nlohmann::json args = {{"url", target_url}};
    auto res = cmd::CommandService::instance().invoke("import_profile", args);
    success = res.is_ok();
    if (!success) {
      err_msg = res.error().message;
    }
  }

  if (success) {
    // src: :398 verge 已加载且 enable_system_proxy/enable_tun_mode 均未开启 → 自动接管
    bool checked = false;
    bool takeover_on = false;
    try {
      const auto& v = *config::Config::instance().verge().latest_arc();
      takeover_on = v.enable_system_proxy.value_or(false) ||
                    v.enable_tun_mode.value_or(false);
      checked = true;
    } catch (...) {
    }
    if (checked && !takeover_on) {
      if (on_patch_system_proxy) {
        on_patch_system_proxy(true);
      } else {
        try {
          cmd::CommandService::instance().invoke("patch_verge_config",
                                                 {{"enable_system_proxy", true}});
        } catch (...) {}
      }
      takeover_on = true;
    }
    // src: :181-186 isTakeoverOff = Boolean(verge && …)：verge 未加载时不误报
    takeover_active_ = !checked || takeover_on;

    // src: :401 setStep('ready')；BUG-311 守卫先释放，避免阻塞后续测速
    is_importing_ = false;
    if (url_field_) url_field_->set_enabled(true);  // 下一步隐藏，先复位可用态
    step_ = FirstRunStep::Ready;
    gave_up_ = false;
    ready_started_ms_ = 0;
    last_poll_ms_ = 0;
    if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
    invalidate();
    invalidate();

    // src: :207-225 首次进入 ready 且未就绪 → 后台全节点测速优选（仅一次）
    if (!auto_retested_) {
      auto_retested_ = true;
      if (on_retest_nodes) on_retest_nodes();
    }
  } else {
    // src: :403 errImport
    is_importing_ = false;
    if (url_field_) url_field_->set_enabled(true);  // 留在导入步，允许重新编辑
    if (!err_msg.empty()) {
      error_message_ = "导入失败: " + err_msg;
    } else {
      error_message_ = "订阅导入失败，请检查链接是否有效后重试";
    }
    invalidate();
  }
}

void FirstRunGuideDialog::finish() {
  // src: :301-303 markGuided：三个出口共用完成标记
  set_first_run_completed(true);
  close();
}

// 三大特性胶囊排版：flex 1 1 auto + flexWrap:wrap + gap 6（src: :607-644）
// 返回总高（多行时 > kPillH）；同时填充 pill_rects_ 供绘制使用。
float FirstRunGuideDialog::layout_pills(float pills_y, float inner_x, float inner_w) {
  const std::wstring texts[3] = {
      tr("layout.components.firstRun.featureAutoSelect"),
      tr("layout.components.firstRun.featureDnsGuard"),
      tr("layout.components.firstRun.featureStealth")};
  // 内容宽 = 文字 + px 8×2 + border 1×2（src: :623-625 px:1 / border 1px）
  float cw3[3];
  for (int i = 0; i < 3; ++i) {
    float tw = 0;
    if (ITextEngine* te = text_engine()) {
      TextStyle ts;
      ts.size = kPillFont;
      tw = te->measure_text(texts[i], ts, 0).w;
    } else {
      tw = static_cast<float>(texts[i].size()) * kPillFont;
    }
    cw3[i] = tw + 16.0f + 2.0f;
  }
  pill_rects_.clear();
  float py = pills_y;
  for (int i = 0; i < 3;) {
    int j = i;
    float sum = 0;
    while (j < 3) {
      const float add = cw3[j] + (j > i ? kPillGap : 0.0f);
      if (sum + add > inner_w && j > i) break;
      sum += add;
      ++j;
    }
    const int cnt = j - i;
    const float extra = std::max(0.0f, inner_w - sum) / static_cast<float>(cnt);
    float px = inner_x;
    for (int k = i; k < j; ++k) {
      const float w = cw3[k] + extra;
      pill_rects_.push_back({px, py, w, kPillH});
      px += w + kPillGap;
    }
    py += kPillH + kPillGap;
    i = j;
  }
  return pill_rects_.empty() ? kPillH : (py - kPillGap - pills_y);
}

void FirstRunGuideDialog::on_arrange(const RectF& area) {
  set_bounds(area);
  if (url_menu_) url_menu_->set_bounds({0, 0, area.w, area.h});
  set_bounds(area);
  // src: :545-557 paper width 100% / maxWidth 520（窗口侧留 32px 呼吸）
  const float cw = std::min(kCardMaxW, area.w - 32.0f);
  const bool has_banner =
      step_ == FirstRunStep::Input && detected_clipboard_url_.empty() == false &&
      url_.empty();
  banner_h_ = kBannerH;
  if (has_banner) {
    if (ITextEngine* te = text_engine()) {
      TextStyle bts;
      bts.size = kBannerFont;
      bts.weight = static_cast<int>(Weight::Bold);
      const float avail_w = (cw - kTitlePadX * 2.0f) - kBannerPadX * 2.0f;
      const SizeF b_sz = te->measure_text(L"✨ " + tr("layout.components.firstRun.clipboardDetected"), bts, 0);
      if (b_sz.w > avail_w) {
        banner_h_ = 46.0f;
      }
    }
  }
  // 引导文案实测高度（src: :593-603 是流式 Typography，行数随语言变化）：
  //   固定 2 行高度会让后续胶囊/输入框压在文字上（日/德文案实测 4~5 行）
  {
    const float inner_w = cw - kTitlePadX * 2.0f;
    float h = kIntroH;
    if (ITextEngine* te = text_engine()) {
      TextStyle ts;
      ts.size = kIntroSize;
      ts.wrap = true;
      const SizeF s = te->measure_text(tr("layout.components.firstRun.intro"), ts, inner_w);
      if (s.h > 0) h = std::max(kIntroH, s.h);
    }
    intro_h_ = h;
  }
  // src: :560-1120 内容随流式增减：输入步 + 横幅高度；就绪步两卡 + 动作
  //   胶囊折行（pills_h_ - kPillH）同样撑高卡片：Mini 是流式 Box，高度自然增长
  float pills_extra = 0.0f;
  if (step_ == FirstRunStep::Input) {
    pills_h_ = layout_pills(0.0f, 0.0f, cw - kTitlePadX * 2.0f);
    pills_extra = std::max(0.0f, pills_h_ - kPillH);
  }
  // 就绪步绿色卡高度随内部块增减（src: :864-1016 无固定 height，流式自增）。
  // 仅累计实际出现的块：导入行 + 状态行 +（可选）节点块 +（可选）重测按钮 +
  //（可选）接管提示。kReadyNodeH 仅作无块时的上界默认。
  float node_h = kReadyNodeH;
  float star_h = kReadyStarH;
  status_h_ = kStatusH;
  star_title_h_ = kStarTitleH;
  star_body_h_ = kStarBodyH;
  if (step_ == FirstRunStep::Ready) {
    const float card_text_w = std::max(60.0f, cw - kTitlePadX * 2.0f - kCardPad * 2.0f);  // src: first-run-guide-dialog.tsx:866,1022
    const bool ready = proxy_ready();
    const bool unconf = unconfirmed();
    if (ITextEngine* te = text_engine()) {
      const std::wstring st_txt =
          ready ? tr("layout.components.firstRun.readyOk")
                : (unconf ? tr("layout.components.firstRun.readyUnconfirmed")
                          : tr("layout.components.firstRun.checking"));
      TextStyle sts;
      sts.size = kStatusFont;
      sts.weight = static_cast<int>(Weight::Bold);
      sts.wrap = true;
      const float st_w = card_text_w - (ready || unconf ? 0.0f : 18.0f);  // src: :899 spinner 12+gap 6
      const SizeF ss = te->measure_text(st_txt, sts, st_w);
      if (ss.h > 0) status_h_ = std::max(kStatusH, ss.h + 2.0f);

      TextStyle tts;
      tts.size = kStarTitleFont;
      tts.weight = static_cast<int>(Weight::Bold);
      tts.wrap = true;
      const SizeF ts_sz = te->measure_text(
          L"⭐ " + tr("layout.components.firstRun.starTitle"), tts, card_text_w);
      if (ts_sz.h > 0) star_title_h_ = std::max(kStarTitleH, ts_sz.h + 2.0f);

      TextStyle bts;
      bts.size = kStarBodyFont;
      bts.wrap = true;
      const SizeF bs_sz = te->measure_text(
          tr("layout.components.firstRun.wikiHint"), bts, card_text_w);
      if (bs_sz.h > 0) star_body_h_ = std::max(kStarBodyH, bs_sz.h + 4.0f);  // src: :1052 lineHeight 1.55
    }
    star_h = kCardPad + star_title_h_ + kImportedMb + star_body_h_ + kStarBtnMt +
             kStarBtnH + kCardPad;
  }
  if (step_ == FirstRunStep::Ready) {
    const bool show_node = !active_node_name_.empty() && (proxy_ready() || unconfirmed());
    const bool show_retest = !proxy_ready() && (unconfirmed() || retesting_);
    const bool show_warn = !takeover_active_;
    float ny = kCardPad;
    ny += kImportedH + kImportedMb;
    ny += kStatusMt + status_h_;
    if (show_node) ny += kNodeMt + kNodePt + 1.0f + kNodeLabelH + kNodeGap + kNodeNameH;
    if (show_retest) ny += kRetestMt + kRetestH;
    if (show_warn) ny += kWarnMt + kWarnH;
    node_h = ny + kCardPad;  // 底部留白
  }
  float ch = step_ == FirstRunStep::Input
                 ? kInputStepH + (intro_h_ - kIntroH) + pills_extra +
                       (has_banner ? banner_h_ + kBannerMb : 0.0f)
                 : kTitleBlockH + kContentPad + node_h + kReadyGap +
                       star_h + kContentPad + kActionsH;
  ch = std::min(ch, area.h - 8.0f);  // 豁免: Mini 无对应物 — 窄高视口防溢出安全留边 4px×2

  const float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  const float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  const float inner_x = cx + kTitlePadX;
  const float inner_w = cw - kTitlePadX * 2.0f;
  const float content_y = cy + kTitleBlockH + kContentPad;

  if (step_ == FirstRunStep::Input) {
    // ---- 三大特性胶囊：flex 1 1 auto + flexWrap:wrap + gap 6 ----
    // （src: :607-644）窄窗 + 长译文放不下一行时必须折行，
    //   行内按 flex-grow 均摊剩余宽度；早期实现平均三等分 → 文字互相压盖
    const float pills_y = content_y + intro_h_ + kIntroMb;
    pills_h_ = layout_pills(pills_y, inner_x, inner_w);

    const float field_y = pills_y + pills_h_ + kPillMb +
                          (has_banner ? banner_h_ + kBannerMb : 0.0f);
    banner_clip_rect_ = {inner_x, pills_y + pills_h_ + kPillMb, inner_w, banner_h_};
    input_rect_ = {inner_x, field_y, inner_w, kFieldH};
    // 订阅链接 TextField：占满输入框宽，右留 26px 给回车图标（src: endAdornment）
      if (url_field_) {
      url_field_->set_visible(open_);
      url_field_->set_bounds({input_rect_.x, field_y, inner_w, kFieldH});
    }
      // src: :715-729 endAdornment KeyboardReturn 16px（点击即导入）
    btn_enter_rect_ = {input_rect_.right() - 26.0f, field_y + 12.0f, 16.0f, 16.0f};

    const float paste_y = field_y + kFieldH + kPasteMt;
    btn_paste_rect_ = {cx + (cw - kPasteW) / 2.0f, paste_y, kPasteW, kPasteH};
    const float actions_top = cy + ch - kActionsH;
    btn_submit_rect_ = {inner_x, actions_top + kActionPadY, inner_w, kPrimaryBtnH};
    btn_later_rect_ = {inner_x, btn_submit_rect_.bottom() + kActionGap, inner_w,
                       kSecondaryBtnH};
  } else {
    // 订阅链接 TextField 在就绪步隐藏并释放焦点（src: 仅 STEP 1 存在）
    if (url_field_) {
      url_field_->set_visible(false);
      url_field_->blur();
    }
    // src: :863-1083 绿色仪表卡 → Star 卡（卡间 mb 12）；绿卡高度流式自增
    card_node_rect_ = {inner_x, content_y, inner_w, node_h};
    // src: :984-999 重测按钮 fullWidth，仅 !ready && (unconfirmed||retesting) 出现
    const bool show_node = !active_node_name_.empty() && (proxy_ready() || unconfirmed());
    const bool show_retest = !proxy_ready() && (unconfirmed() || retesting_);
    const bool show_warn = !takeover_active_;  // 与 node_h 计算块同源，避免「检测中」态错位
    float ny = content_y + kCardPad + kImportedH + kImportedMb + kStatusMt + status_h_;
    if (show_node) ny += kNodeMt + kNodePt + 1.0f + kNodeLabelH + kNodeGap + kNodeNameH;
    if (show_retest) {
      ny += kRetestMt;
      btn_retest_rect_ = {card_node_rect_.x + kCardPad, ny,
                          card_node_rect_.w - kCardPad * 2.0f, kRetestH};
      ny += kRetestH;
    }
    // 接管提示 y：始终接在最后一个可见块之后（src: :1002-1015 mt 0.75），
    // 不依赖重测按钮是否出现，避免「检测中」态错位。
    warn_y_ = show_warn ? (ny + kWarnMt) : ny;

    float star_y = content_y + node_h + kReadyGap;
    const float pre_actions = cy + ch - kActionsH;
    if (star_y + star_h > pre_actions - 4.0f) {
      const float avail = pre_actions - 4.0f - star_y;
      if (avail < 70.0f) {
        star_y = std::max(content_y + node_h + 2.0f, pre_actions - 4.0f - star_h);  // 豁免: 极矮视口下防底栏重叠
      }
      star_h = std::max(40.0f, pre_actions - 4.0f - star_y);  // 豁免: 视口受限时压缩卡片高
    }
    card_star_rect_ = {inner_x, star_y, inner_w, star_h};
    // src: :1059-1082 Wiki 文本按钮右对齐
    float wiki_w = kStarBtnW;
    if (ITextEngine* te = text_engine()) {
      TextStyle wts;
      wts.size = kHintFont;
      wts.weight = static_cast<int>(Weight::Bold);
      const SizeF ws = te->measure_text(
          L"📖 " + tr("layout.components.firstRun.openWiki") + L" ↗", wts, 0);
      wiki_w = std::min(inner_w - kCardPad * 2.0f, std::max(kStarBtnW, ws.w + 12.0f));  // src: :1071 px: 0.75 (6px*2)
    }
    // 始终定位在 star 卡片内部底部右下角，与卡底留出 kCardPad，杜绝与上方正文或下方动作区重叠
    const float wiki_y = card_star_rect_.bottom() - kCardPad - kStarBtnH;
    btn_wiki_rect_ = {card_star_rect_.right() - kCardPad - wiki_w, wiki_y,
                      wiki_w, kStarBtnH};


    const float actions_top = cy + ch - kActionsH;
    btn_finish_star_rect_ = {inner_x, actions_top + kActionPadY, inner_w, kPrimaryBtnH};
    btn_finish_rect_ = {inner_x, btn_finish_star_rect_.bottom() + kActionGap, inner_w,
                        kSecondaryBtnH};
  }
}

void FirstRunGuideDialog::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();  // 自续帧，直到过渡结束
  set_opacity(fade_.value());        // 每帧同步整棵子树的不透明度
  if (fade_.hidden()) return;

  // src: MUI Dialog backdrop 默认 rgba(0,0,0,0.5)
  rt.fill_rect({0, 0, bounds().w, bounds().h},
               Brush{Color4f(0, 0, 0, 0.5f)});

  paint_card(rt, card_rect_);
  const Color4f text_color = card_text_color();

  // ---- 标题区（src: :562-590 / :832-860）----
  const std::wstring title =
      step_ == FirstRunStep::Input ? L"🎉 " + tr("layout.components.firstRun.title") : L"🚀 " + tr("layout.components.firstRun.readyTitle");
  draw_text(rt, title,
            {card_rect_.x, card_rect_.y + kTitlePadY, card_rect_.w, kTitleLineH},
            kTitleSize, static_cast<int>(Weight::Bold),
            step_ == FirstRunStep::Input ? text_color : ready_color(), AlignH::Center);

  // 步骤胶囊：src: :495-527
  const RectF badge_r = {card_rect_.x + (card_rect_.w - kBadgeW) / 2.0f,
                         card_rect_.y + kTitlePadY + kTitleLineH + kTitleGap,
                         kBadgeW, kBadgeH};
  // src: :507-514 bgcolor / borderColor（retro 深棕 / 其余中性灰）
  const Color4f badge_bg = is_retro() ? Color4f::hex(0x2C1F03, 0.12f)
                                      : Color4f::hex(0x808080, 0.14f);
  const Color4f badge_bd = is_retro() ? Color4f::hex(0x2C1F03, 0.22f)
                                      : Color4f::hex(0x808080, 0.24f);
  Brush badge_b{badge_bg};
  rt.round_rect(badge_r, badge_r.h / 2.0f, Pen{badge_bd, 1.0f}, &badge_b);
  // src: :517-524 6×6 圆点（STEP1 primary.main / STEP2 readyColor）
  Brush dot_b{step_ == FirstRunStep::Input ? ThemeService::instance().primary()
                                           : ready_color()};
  rt.round_rect({badge_r.x + kBadgePadX, badge_r.y + (badge_r.h - 6.0f) / 2.0f, 6.0f,
                 6.0f},
                3.0f, {}, &dot_b);
  draw_text(rt, step_ == FirstRunStep::Input ? L"STEP 1 / 2" : L"STEP 2 / 2",
            {badge_r.x + kBadgePadX + 6.0f + kPillGap, badge_r.y,
             badge_r.w - kBadgePadX - 6.0f - kPillGap, badge_r.h},
            kBadgeFont, static_cast<int>(Weight::Bold), secondary_text(), AlignH::Left);

  // src: :572 borderBottom 1px solid palette.divider
  rt.fill_rect({card_rect_.x, card_rect_.y + kTitleBlockH - 1.0f, card_rect_.w, 1.0f},
               Brush{divider_color()});

  const float content_y = card_rect_.y + kTitleBlockH + kContentPad;
  const float inner_x = card_rect_.x + kTitlePadX;
  const float inner_w = card_rect_.w - kTitlePadX * 2.0f;

  if (step_ == FirstRunStep::Input) {
    // ---- 引导文案（src: :593-603）----
    draw_text(rt,
              tr("layout.components.firstRun.intro"),
              {inner_x, content_y, inner_w, intro_h_}, kIntroSize,
              static_cast<int>(Weight::Normal), secondary_text(), AlignH::Left,
              AlignV::Top, true);

    // ---- 三大特性胶囊（src: :606-647）----
    //   排版在 on_arrange 里做过（flex 折行），这里只按 pill_rects_ 绘制
    const float pills_y = content_y + intro_h_ + kIntroMb;
    if (pill_rects_.size() != 3) layout_pills(pills_y, inner_x, inner_w);
    // src: :632-641 bgcolor / borderColor
    const Color4f pill_bg = is_retro() ? Color4f::hex(0x2C1F03, 0.10f)
                                      : (is_light() ? Color4f::hex(0x000000, 0.04f)
                                                    : Color4f::hex(0xFFFFFF, 0.06f));
    const Color4f pill_bd = is_retro() ? Color4f::hex(0x2C1F03, 0.20f)
                                      : divider_color();
    const std::wstring pills[3] = {
        tr("layout.components.firstRun.featureAutoSelect"),
        tr("layout.components.firstRun.featureDnsGuard"),
        tr("layout.components.firstRun.featureStealth")};
    for (int i = 0; i < 3 && i < static_cast<int>(pill_rects_.size()); ++i) {
      const RectF pr = pill_rects_[static_cast<size_t>(i)];
      Brush pb{pill_bg};
      rt.round_rect(pr, 6.0f, Pen{pill_bd, 1.0f}, &pb);
      draw_text(rt, pills[i], pr, kPillFont, static_cast<int>(Weight::Bold), text_color,
                AlignH::Center);
    }

    // ---- 剪贴板智能探测横幅（src: :650-695）----
    if (!detected_clipboard_url_.empty() && url_.empty()) {
      const float banner_y = pills_y + pills_h_ + kPillMb + (hover_banner_ ? -1.0f : 0.0f);
      const RectF br = {inner_x, banner_y, inner_w, banner_h_};
      banner_clip_rect_ = br;
      // src: :667-676 retro rgba(27,94,32,.12) / dark rgba(46,125,50,.18) /
      //      light rgba(46,125,50,.1)，边框 1px dashed
      const Color4f b_bg = is_retro()
                               ? Color4f::hex(0x1B5E20, hover_banner_ ? 0.18f : 0.12f)
                               : (is_light() ? Color4f::hex(0x2E7D32, hover_banner_ ? 0.15f : 0.10f)
                                             : Color4f::hex(0x2E7D32, hover_banner_ ? 0.24f : 0.18f));
      const Color4f b_bd = is_retro() ? Color4f::hex(0x1B5E20, 0.45f) : ready_color();
      Brush bbg{b_bg};
      rt.round_rect(br, 6.0f, {}, &bbg);
      dashed_rect(rt, br, b_bd);
      draw_text(rt, L"✨ " + tr("layout.components.firstRun.clipboardDetected"),
                {br.x + kBannerPadX, br.y + 4.0f, br.w - kBannerPadX * 2.0f, br.h - 8.0f}, kBannerFont,
                static_cast<int>(Weight::Bold), ready_color(), AlignH::Center, AlignV::Center, true);
    }

    // ---- 凹陷订阅输入框（src: :697-733 + get3DInputStyle :734-927）----
    // ★ 输入框本体为 Win 端 TextField 子控件（见构造），由引擎自绘（bg/描边/
    //   文本/占位符/光标），此处仅绘制右侧回车图标 endAdornment
    //   （src: :713-728 KeyboardReturn 16px，hover 变主色，点击即导入）。
    // src: :725-727 KeyboardReturn 16px color text.disabled
    draw_text(rt, L"⏎", btn_enter_rect_, 16.0f,
              static_cast<int>(Weight::Normal),
              hover_enter_ ? ThemeService::instance().primary() : disabled_text(),
              AlignH::Center);

    // ---- 粘贴按钮（src: :750-779）----
    draw_text(rt, L"📋 " + tr("layout.components.firstRun.paste"),
              {btn_paste_rect_.x, btn_paste_rect_.y + (pressed_paste_ ? 1.0f : 0.0f),
               btn_paste_rect_.w, btn_paste_rect_.h}, kPasteFont,
              static_cast<int>(Weight::Bold),
              hover_paste_ ? warning_color() : ThemeService::instance().primary(),
              AlignH::Center);

    // ---- 错误提示 / Enter 提示（src: :735-748 错误行；:781-789 提示行）----
    // 豁免: 本引擎布局不随错误动态改高，错误与提示共用输入框下方同一行
    const bool has_error = !error_message_.empty();
    const std::wstring hint_line =
        has_error ? L"⚠️ " + utf::utf8_to_utf16(error_message_)
                  : tr("layout.components.firstRun.enterHint");
    draw_text(rt, hint_line,
              {inner_x, btn_paste_rect_.bottom() + kHintGap, inner_w, kHintH},
              has_error ? kPasteFont : kHintFont,
              static_cast<int>(has_error ? Weight::Bold : Weight::Normal),
              has_error ? ThemeService::instance().palette().error_color
                        : disabled_text(),
              AlignH::Center);

    // ---- 动作区（src: :792-828）----
    rt.fill_rect({card_rect_.x, card_rect_.y + card_rect_.h - kActionsH,
                  card_rect_.w, 1.0f},
                 Brush{divider_color()});
    // src: :803-815 contained primary
    // src: locales/zh/layout.json:8 「"importAndStart": "立即导入并开启"」（Button loading 仅展示 spinner）
    paint_button(rt, btn_submit_rect_, BtnStyle::Primary,
                 tr("layout.components.firstRun.importAndStart"), 13.0f,
                 hover_submit_, pressed_submit_);
    // src: :816-827 outlined default
    paint_button(rt, btn_later_rect_, BtnStyle::OutlinedDefault,
                 tr("layout.components.firstRun.later"), 12.5f,
                 hover_later_, pressed_later_);

  } else {
    // ================= STEP 2：就绪仪表盘（src: :830-1119）=================
    // ---- 绿色仪表卡（src: :864-1016）----
    const RectF& nc = card_node_rect_;
    // src: :868-876 bgcolor rgba(76,175,80,.08/.12) + 1px 边框
    const Color4f node_bg = Color4f::hex(0x4CAF50, is_light() ? 0.08f : 0.12f);
    const Color4f node_bd = Color4f::hex(0x4CAF50, is_light() ? 0.28f : 0.32f);
    Brush nb{node_bg};
    rt.round_rect(nc, 8.0f, Pen{node_bd, 1.0f}, &nb);

    // src: :880-890 ✅ 导入成功
    const float imported_y = nc.y + kCardPad;
    draw_text(rt, L"✅ " + tr("layout.components.firstRun.imported"),
              {nc.x + kCardPad, imported_y, nc.w - kCardPad * 2.0f, kImportedH},
              kImportedFont, static_cast<int>(Weight::Bold), ready_color());

    // src: :893-919 就绪状态（未测出前如实提示，不做虚假宣称）
    const bool ready = proxy_ready();
    const bool unconfirmed = this->unconfirmed();
    const float status_y = imported_y + kImportedH + kImportedMb + kStatusMt;
    if (!ready && !unconfirmed) {
      // 豁免: CircularProgress 以 12px 空心圆近似（引擎无动画组件）
      rt.round_rect({nc.x + kCardPad, status_y + 2.0f, 12.0f, 12.0f}, 6.0f,
                    Pen{secondary_text(), 1.0f}, nullptr);
      draw_text(rt, L"◌", {nc.x + kCardPad, status_y, 14.0f, 18.0f}, kStatusFont,
                static_cast<int>(Weight::Normal), secondary_text(), AlignH::Center);
    }
    const Color4f status_color =
        ready ? ready_color() : (unconfirmed ? warning_color() : secondary_text());
    const std::wstring status_text =
        ready ? tr("layout.components.firstRun.readyOk")
              : (unconfirmed
                     ? tr("layout.components.firstRun.readyUnconfirmed")
                     : tr("layout.components.firstRun.checking"));
    draw_text(rt, status_text,
              {nc.x + kCardPad + (ready || unconfirmed ? 0.0f : 18.0f), status_y,
               nc.w - kCardPad * 2.0f - (ready || unconfirmed ? 0.0f : 18.0f), status_h_},
              kStatusFont, static_cast<int>(Weight::Bold), status_color, AlignH::Left,
              AlignV::Top, true);

    // src: :921-981 当前最优节点（仅在测速完成/超时确认后展示）
    const float node_top = status_y + status_h_ + kNodeMt;
    if (!active_node_name_.empty() && (ready || unconfirmed)) {
      // src: :926-930 borderTop 1px dashed rgba(128,128,128,.25)
      dashed_line(rt, nc.x + kCardPad, node_top + kNodePt, nc.w - kCardPad * 2.0f,
                  Color4f::hex(0x808080, 0.25f));
      const float label_y = node_top + kNodePt + 1.0f;
      draw_text(rt, L"🌐 " + tr("layout.components.firstRun.activeNodeLabel"),
                {nc.x + kCardPad, label_y, 140.0f, kNodeLabelH},
                kHintFont, static_cast<int>(Weight::Bold), secondary_text());
      if (ready) {
        // src: :952-965 延迟胶囊：⚡ xx ms
        const RectF db = {nc.right() - kCardPad - 72.0f, label_y - 1.0f, 72.0f, 18.0f};
        Brush dbb{Color4f::hex(0x4CAF50, 0.18f)};
        rt.round_rect(db, db.h / 2.0f, {}, &dbb);
        draw_text(rt, L"⚡ " + std::to_wstring(active_delay_ms_) + L" ms", db, 11.0f,
                  static_cast<int>(Weight::Bold), ready_color(), AlignH::Center);
      }
      // src: :968-979 节点名
      draw_text(rt, utf::utf8_to_utf16(active_node_name_),
                {nc.x + kCardPad, label_y + kNodeLabelH + kNodeGap,
                 nc.w - kCardPad * 2.0f, kNodeNameH},
                13.0f, static_cast<int>(Weight::Bold),
                ThemeService::instance().palette().primary_text, AlignH::Left, AlignV::Center,
                false, true);
    }

    // src: :983-1000 重测并优选（仅在未就绪/重测中显示）
    if (!ready && (unconfirmed || retesting_)) {
      paint_button(rt, btn_retest_rect_, BtnStyle::OutlinedPrimary,
                   L"🔄 " + tr("layout.components.firstRun.retestNodes"), 12.5f,
                   hover_retest_, pressed_retest_);
    }

    // src: :1002-1015 接管未开启提示
    if (!takeover_active_) {
      draw_text(rt, L"⚠️ " + tr("layout.components.firstRun.sysProxyOff"),
                {nc.x + kCardPad, warn_y_, nc.w - kCardPad * 2.0f, kWarnH}, 12.0f,
                static_cast<int>(Weight::Bold), warning_color());
    }

    // ---- 暖金 Star 卡（src: :1019-1083）----
    const RectF& sc = card_star_rect_;
    // src: :1023-1032 bgcolor / borderColor
    const Color4f star_bg = is_retro()
                                ? Color4f::hex(0xFFFFFF, 0.22f)
                                : Color4f::hex(0xFFC107, is_light() ? 0.10f : 0.09f);
    const Color4f star_bd = is_retro() ? Color4f::hex(0x7A3E00, 0.35f)
                                      : Color4f::hex(0xFFC107, 0.45f);
    Brush sb{star_bg};
    rt.round_rect(sc, 8.0f, Pen{star_bd, 1.0f}, &sb);

    // src: :1035-1046 ⭐ 标题
    draw_text(rt, L"⭐ " + tr("layout.components.firstRun.starTitle"),
              {sc.x + kCardPad, sc.y + kCardPad, sc.w - kCardPad * 2.0f, star_title_h_},
              kStarTitleFont, static_cast<int>(Weight::Bold), star_accent_color(), AlignH::Left, AlignV::Top, true);
    // src: :1048-1057 正文
    const float body_y = sc.y + kCardPad + star_title_h_ + kImportedMb;
    const float body_max_h = std::max(12.0f, btn_wiki_rect_.y - 2.0f - body_y);
    draw_text(rt,
              tr("layout.components.firstRun.wikiHint"),
              {sc.x + kCardPad, body_y,
               sc.w - kCardPad * 2.0f, body_max_h},
              kStarBodyFont, static_cast<int>(Weight::Normal), secondary_text(),
              AlignH::Left, AlignV::Top, true, true);
    // src: :1059-1082 Wiki 文本按钮
    draw_text(rt, L"📖 " + tr("layout.components.firstRun.openWiki") + L" ↗",
              {btn_wiki_rect_.x, btn_wiki_rect_.y + (pressed_wiki_ ? 1.0f : 0.0f),
               btn_wiki_rect_.w, btn_wiki_rect_.h}, kHintFont,
              static_cast<int>(Weight::Bold),
              hover_wiki_ ? warning_color() : ThemeService::instance().primary(),
              AlignH::Right);

    // ---- 动作区（src: :1086-1119）----
    rt.fill_rect({card_rect_.x, card_rect_.y + card_rect_.h - kActionsH,
                  card_rect_.w, 1.0f},
                 Brush{divider_color()});
    paint_button(rt, btn_finish_star_rect_, BtnStyle::Primary,
                 L"⭐ " + tr("layout.components.firstRun.openGithubStar"), 13.0f,
                 hover_finish_star_, pressed_finish_star_);
    paint_button(rt, btn_finish_rect_, BtnStyle::OutlinedDefault,
                 tr("layout.components.firstRun.finish"), 12.5f,
                 hover_finish_, pressed_finish_);
  }
}

bool FirstRunGuideDialog::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  const PointF pt = e.pos;

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
      if (btn_retest_rect_.contains(pt.x, pt.y) &&
          !proxy_ready() && (unconfirmed() || retesting_)) {
        pressed_retest_ = true;
      }
      if (btn_wiki_rect_.contains(pt.x, pt.y)) pressed_wiki_ = true;
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
      const bool hr = btn_retest_rect_.contains(pt.x, pt.y) &&
                      !proxy_ready() && (unconfirmed() || retesting_);
      const bool hw = btn_wiki_rect_.contains(pt.x, pt.y);
      const bool hfs = btn_finish_star_rect_.contains(pt.x, pt.y);
      const bool hf = btn_finish_rect_.contains(pt.x, pt.y);
      if (hr != hover_retest_ || hw != hover_wiki_ || hfs != hover_finish_star_ ||
          hf != hover_finish_) {
        hover_retest_ = hr;
        hover_wiki_ = hw;
        hover_finish_star_ = hfs;
        hover_finish_ = hf;
        invalidate();
      }
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    pressed_banner_ = pressed_paste_ = pressed_enter_ = pressed_submit_ = pressed_later_ = false;
    pressed_retest_ = pressed_wiki_ = pressed_finish_star_ = pressed_finish_ = false;
    invalidate();

    // src: :533-542 导入进行中点遮罩/Esc 均不关闭
    if (is_importing_) return true;

    if (step_ == FirstRunStep::Input) {
      if (!detected_clipboard_url_.empty() && url_.empty() &&
          banner_clip_rect_.contains(pt.x, pt.y)) {
        // src: :650-657 点击横幅填入
        set_url(detected_clipboard_url_);
        if (url_field_) url_field_->request_focus();
        return true;
      }
      if (btn_paste_rect_.contains(pt.x, pt.y)) {
        std::string clip = read_clipboard_url();
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
        // src: :469-473 handleDismissLater
        finish();
        return true;
      }
    } else {
      if (btn_retest_rect_.contains(pt.x, pt.y) &&
          !proxy_ready() && (unconfirmed() || retesting_)) {
        // src: :189-205 重测并优选：retesting=true、gaveUp=false
        if (!retesting_) {
          retesting_ = true;
          gave_up_ = false;
          ready_started_ms_ = 0;
          last_poll_ms_ = 0;
    if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
    invalidate();
          if (bounds().w > 0.0f && bounds().h > 0.0f) {
            on_arrange(bounds());
          }
          invalidate();
          if (on_retest_nodes) on_retest_nodes();
        }
        return true;
      }
      if (btn_wiki_rect_.contains(pt.x, pt.y)) {
        if (on_open_wiki) {
          on_open_wiki();
        } else {
          ShellExecuteW(nullptr, L"open",
                        // §5.1 品牌位：Clash Mini → Clash Win（发布后不得把用户导向 Mini 仓库）
                        L"https://github.com/qiu-yuxiao/clash-win/wiki", nullptr,
                        nullptr, SW_SHOWNORMAL);
        }
        return true;
      }
      if (btn_finish_star_rect_.contains(pt.x, pt.y)) {
        // src: :454-463 handleOpenGithubStarAndFinish
        if (on_open_github) {
          on_open_github();
        } else {
          // §5.1 品牌位：Clash Mini → Clash Win
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

    // src: :533-542 点击遮罩：Input→later，Ready→finish
    if (!card_rect_.contains(pt.x, pt.y)) {
      finish();
      return true;
    }
    return true;
  }

  if (e.type == MouseEventType::Down) return true;  // 拦截下层点击
  return true;
}

bool FirstRunGuideDialog::on_key(const KeyEvent& e) {
  if (!open_) return false;

  if (e.type == KeyEventType::Down) {
    // src: :533-542 Esc 走 onClose（导入中忽略）
    if (e.key == VK_ESCAPE || e.key == 27) {
      if (!is_importing_) finish();
      return true;
    }
    // src: :417-446 Enter 全窗口监听（任意焦点均可导入/完成）
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

  // 字符输入（打字/删除/IME/选区/剪贴板）已交由订阅框 TextField 子控件处理
  // （← Mini :698-732 真实 MUI TextField；wants_key 自动获焦），本对话框不再手绘吃键。
  // 此外窗口级 handler（main_window WM_KEYDOWN）也会在弹窗打开时统一接管 Esc/Enter，
  // 保证就绪步（订阅框已隐藏）键位仍可用。
  return false;
}

void FirstRunGuideDialog::open_url_menu(PointF anchor) {
  if (!url_menu_ || !url_field_) return;
  TextField* f = url_field_;

  // 外观：src: _layout.tsx:1872-1884（minWidth 160 / radius 6 / border 1px
  //   rgba(255,255,255,.12) / List padding 4px 0）+ style-helpers.tsx:126-145
  MenuStyle st;
  st.item_h = 48;
  st.padding_h = 16;
  st.min_w = 160;  // src: _layout.tsx:1876 minWidth '160px'
  st.radius = 6;
  st.font_size = 13.0f;
  st.pad_v = 4;
  st.hover_inset = 0;
  st.border = Color4f(1, 1, 1, 0.12f);
  st.text = ThemeService::instance().text_primary();
  st.disabled_text = Color4f(st.text.r, st.text.g, st.text.b, 0.38f);
  // src: _theme.tsx:8/:24 error_color '#FF3B30'(light) / '#FF453A'(dark)
  st.danger_text =
      ThemeService::instance().is_light() ? Color4f::hex(0xFF3B30) : Color4f::hex(0xFF453A);
  // 6 套皮肤：面板底/边框/圆角 + hover 全部走查表（← apply_menu_skin）
  apply_menu_skin(st);

  // 5 项 + 1 分隔线：src: _layout.tsx:1886-1931（文案 zh: shared.json:27-31）
  std::vector<MenuItem> its;
  its.push_back({L"✂️ " + tr("shared.actions.cut"), !f->can_clear(), false, L"", false});
  its.push_back({L"📋 " + tr("shared.actions.copy"), !f->can_clear(), false, L"", false});
  its.push_back({L"📥 " + tr("shared.actions.paste"), !f->can_paste(), false, L"", false});
  its.push_back({L"🔍 " + tr("shared.actions.selectAll"), !f->can_select_all(), false, L"", false});
  its.push_back({L"", false, true, L"", false});
  its.push_back({L"🧹 " + tr("shared.actions.clearInput"), !f->can_clear(), false, L"", true});

  url_menu_->style_ = st;
  url_menu_->on_select = [f](int idx) {
    switch (idx) {
      case 0: if (!f->can_cut()) f->select_all(); f->cut(); break;
      case 1: if (!f->can_copy()) f->select_all(); f->copy(); break;
      case 2: f->paste(); break;
      case 3: f->select_all(); break;
      case 5: f->clear_text(); break;
      default: break;
    }
    f->request_focus();
  };
  // ContextMenu 是 dialog 子件：锚点转 dialog 局部坐标
  url_menu_->open(its, to_local(anchor));
}

}  // namespace clashwin::ui::scene
