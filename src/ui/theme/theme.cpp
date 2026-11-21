// ThemeService 实现（T12）：解析链 + 规格/生命周期/容器与卡片参数表。
// src: src/pages/_layout/hooks/use-custom-theme.ts
#include "theme.h"
#include "theme_internal.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "util/log.h"

namespace clashwin::ui::theme {

using detail::with_alpha;

namespace {
// src: src/pages/_layout/hooks/use-custom-theme.ts:12-43 系统 AccentColor 读取
std::optional<Color4f> query_os_accent_color() {
  DWORD val = 0;
  DWORD sz = sizeof(val);
  if (RegGetValueW(HKEY_CURRENT_USER,
                   L"SOFTWARE\\Microsoft\\Windows\\DWM",
                   L"AccentColor", RRF_RT_REG_DWORD, nullptr, &val, &sz) ==
      ERROR_SUCCESS) {
    // Windows Registry DWM\AccentColor 为 0xAABBGGRR
    const uint8_t r = static_cast<uint8_t>(val & 0xFFu);
    const uint8_t g = static_cast<uint8_t>((val >> 8) & 0xFFu);
    const uint8_t b = static_cast<uint8_t>((val >> 16) & 0xFFu);
    return Color4f{r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
  }
  HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
  if (!dwm) return std::nullopt;
  using Fn = HRESULT(__stdcall*)(DWORD*, BOOL*);
  auto fn = reinterpret_cast<Fn>(GetProcAddress(dwm, "DwmGetColorizationColor"));
  std::optional<Color4f> out;
  if (fn) {
    DWORD argb = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(fn(&argb, &opaque))) {
      const uint8_t r = static_cast<uint8_t>((argb >> 16) & 0xFFu);
      const uint8_t g = static_cast<uint8_t>((argb >> 8) & 0xFFu);
      const uint8_t b = static_cast<uint8_t>(argb & 0xFFu);
      out = Color4f{r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
    }
  }
  FreeLibrary(dwm);
  return out;
}

// CSS hex "#RRGGBB" / "#RRGGBBAA" → Color4f（解析失败返回 nullopt）
std::optional<Color4f> parse_hex(const std::string& css) {
  if (css.size() < 7 || css[0] != '#') return std::nullopt;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  auto byte = [&](size_t i) -> int {
    int hi = nib(css[i]), lo = nib(css[i + 1]);
    if (hi < 0 || lo < 0) return -1;
    return hi * 16 + lo;
  };
  int r = byte(1), g = byte(3), b = byte(5);
  if (r < 0 || g < 0 || b < 0) return std::nullopt;
  float a = 1.0f;
  if (css.size() >= 9) {
    int ab = byte(7);
    if (ab >= 0) a = ab / 255.0f;
  }
  return Color4f{r / 255.0f, g / 255.0f, b / 255.0f, a};
}
}  // namespace

// ---------- 逐皮肤的滑块规格 ----------
// src: pages/_layout/components/theme-settings-card.tsx:59-134
SkinSliderSpec skin_slider_spec(SkinId skin) {
  SkinSliderSpec s{};
  switch (skin) {
    case SkinId::Retro3d:
      s.max1 = 1.0f;  s.max2 = 5.0f;
      s.label1 = L"Depth";    s.label2 = L"Vibrancy";
      break;
    case SkinId::Original:
      s.max1 = 3.0f;  s.max2 = 3.0f;
      s.label1 = L"Radius";   s.label2 = L"Accent";
      break;
    case SkinId::ModernFlat:
      s.max1 = 3.0f;  s.max2 = 5.0f;
      s.label1 = L"Roundness"; s.label2 = L"Shadow";
      break;
    case SkinId::FrostedGlass:
      s.max1 = 5.0f;  s.max2 = 5.0f;
      s.label1 = L"Opacity";  s.label2 = L"Blur";
      break;
    case SkinId::Cyberpunk:
      s.max1 = 5.0f;  s.max2 = 5.0f;
      s.label1 = L"Glow";     s.label2 = L"Speed";
      break;
    case SkinId::Monochrome:
      s.max1 = 3.0f;  s.max2 = 5.0f;
      s.label1 = L"Radius";   s.label2 = L"Border";
      break;
  }
  return s;
}

// 逐皮肤因子缺省（← use-skin-controls.ts:13-24 readFactor）
void skin_default_factors(SkinId skin, float* depth, float* vibrancy) {
  if (skin == SkinId::Retro3d) {
    if (depth) *depth = 0.3f;      // :29 fallback 0.3
    if (vibrancy) *vibrancy = 1.0f;  // :33 fallback 1.0
  } else {
    if (depth) *depth = 1.0f;
    if (vibrancy) *vibrancy = 1.0f;
  }
}

// ★ §2.7 每皮肤全局字体（← Mini layout.scss 的 --control-font-family）
const std::vector<std::wstring>& skin_font_candidates(SkinId skin) {
  static const std::vector<std::wstring> kRetro3d = {
      L"Trebuchet MS", L"SimHei", L"Microsoft YaHei", L"Segoe UI"};
  static const std::vector<std::wstring> kOriginal = {
      L"Segoe UI", L"Microsoft YaHei", L"Tahoma"};
  static const std::vector<std::wstring> kModernFlat = {
      L"Outfit", L"DengXian", L"Microsoft YaHei", L"Segoe UI"};
  static const std::vector<std::wstring> kFrostedGlass = {
      L"Segoe UI Light", L"Microsoft YaHei Light", L"Segoe UI",
      L"Microsoft YaHei"};
  static const std::vector<std::wstring> kCyberpunk = {
      L"Consolas", L"NSimSun", L"Courier New"};
  static const std::vector<std::wstring> kMonochrome = {
      L"Georgia", L"KaiTi", L"SimSun", L"Times New Roman"};
  switch (skin) {
    case SkinId::Retro3d:
      return kRetro3d;
    case SkinId::Original:
      return kOriginal;
    case SkinId::ModernFlat:
      return kModernFlat;
    case SkinId::FrostedGlass:
      return kFrostedGlass;
    case SkinId::Cyberpunk:
      return kCyberpunk;
    case SkinId::Monochrome:
      return kMonochrome;
  }
  return kOriginal;
}

ThemeService& ThemeService::instance() {
  static ThemeService svc;
  return svc;
}

void ThemeService::init(SkinId skin, ThemeMode mode,
                        const std::string& primary_override) {
  skin_ = skin;
  mode_ = mode;
  primary_override_ = primary_override;
  // ← use-skin-controls.ts:13-24 readFactor：因子缺省逐皮肤不同
  skin_default_factors(skin_, &depth_, &vibrancy_);
  ui_font_.clear();
  rebuild();
}

void ThemeService::set_factors(float depth, float vibrancy) {
  if (depth_ == depth && vibrancy_ == vibrancy) return;
  depth_ = depth;
  vibrancy_ = vibrancy;
  rebuild();
  notify();
}

void ThemeService::rebuild() {
  palette_ = base_palette(mode_);
  // ← use-custom-theme.ts:170-181 主色解析链
  if (skin_ == SkinId::Original) {
    // ← index.scss:299「--primary-main: hsl(calc(239 * var(--control-skin-val2)), 26%, 49%)」
    palette_.primary_color = original_primary(vibrancy_);
  } else if (!primary_override_.empty()) {
    if (auto c = parse_hex(primary_override_))
      palette_.primary_color = *c;
    else
      CWLOG_WARN("Theme: primary_color 解析失败 " + primary_override_);
  } else if (allow_os_accent_) {
    if (auto accent = query_os_accent_color()) {
      palette_.primary_color = *accent;
    }
  }
  vars_ = css_vars(mode_, skin_);
  if (skin_ == SkinId::Monochrome) {
    // src: src/assets/styles/index.scss:285-295 monochrome .text-secondary .65 / divider .45
    palette_.secondary_text.a *= 0.65f;
    vars_.divider_color.a *= 0.45f;
  }
}

void ThemeService::set_allow_os_accent(bool allow) {
  if (allow_os_accent_ == allow) return;
  allow_os_accent_ = allow;
  rebuild();
  notify();
}

void ThemeService::notify() {
  for (auto& [id, cb] : subs_) cb();
}

uint64_t ThemeService::subscribe(std::function<void()> cb) {
  uint64_t id = next_id_++;
  subs_.emplace_back(id, std::move(cb));
  return id;
}

void ThemeService::unsubscribe(uint64_t id) {
  subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
                             [id](const auto& p) { return p.first == id; }),
              subs_.end());
}

void ThemeService::set_skin(SkinId skin) {
  if (skin_ == skin) return;
  skin_ = skin;
  ui_font_.clear();
  rebuild();
  notify();
}

void ThemeService::set_mode(ThemeMode mode) {
  if (mode_ == mode) return;
  mode_ = mode;
  rebuild();
  notify();
}

void ThemeService::set_primary_override(const std::string& css_hex) {
  primary_override_ = css_hex;
  rebuild();
  notify();
}

// ---------- 容器（.theme-panel）：6 套皮肤差异总表 ----------
PanelPaint ThemeService::panel_paint() const {
  const bool light = is_light();
  const float d = depth();       // ← --depth-factor
  const float v = vibrancy();    // ← --vibrancy-factor
  const float g = val1();        // ← --control-skin-val1
  PanelPaint p{};
  p.bg = vars_.theme_panel_bg;  // 多数皮肤的底色来源
  p.radius = 8.0f;              // --border-radius 缺省 8（index.scss:76）

  switch (skin_) {
    case SkinId::Retro3d:
      // index.scss:123-132（暗）/ :145-153（亮）
      p.grad_bg = true;
      p.grad_count = 3;
      p.grad_angle = 135.0f;
      if (light) {
        p.grad[0] = {0.00f, Color4f::hex(0xFFFDE7)};
        p.grad[1] = {0.50f, Color4f::hex(0xFFF9C4)};
        p.grad[2] = {1.00f, Color4f::hex(0xFFF59D)};
        p.shadow_color = Color4f::hex(0x000000, 0.15f * d);
      } else {
        p.grad[0] = {0.00f, Color4f::hex(0x1A160E)};
        p.grad[1] = {0.50f, Color4f::hex(0x2A2413)};
        p.grad[2] = {1.00f, Color4f::hex(0x17140B)};
        p.shadow_color = Color4f::hex(0x000000, 0.30f * d);
      }
      p.border = PanelPaint::BorderKind::Double;
      p.border_color = Color4f::hex(0xB8860B);
      p.has_shadow = true;
      p.shadow_dy = 8.0f * d;
      p.shadow_blur = 32.0f * d;
      p.has_glow = true;
      p.glow_color = Color4f::hex(0xFFC107, 0.20f * v);
      p.glow_blur = 12.0f * v;
      // index.scss:128-130 / :149-151
      p.has_inner_bevel = true;
      p.inner_hi = light ? Color4f::hex(0xFFFFFF, 0.65f * d)
                         : Color4f::hex(0xFFFFFF, 0.20f * d);
      p.inner_dark = light ? Color4f::hex(0x8A6D00, 0.30f * d)
                           : Color4f::hex(0x000000, 0.55f * d);
      p.inner_blur = 2.0f * d;
      break;

    case SkinId::Original:
      // index.scss:298-320
      p.border = PanelPaint::BorderKind::None;
      p.radius = 8.0f * g;   // ← --border-radius: calc(8px * var(--control-skin-val1))
      p.has_shadow = true;
      p.shadow_color = Color4f::hex(0x000000, 0.08f);
      p.shadow_dy = 1.0f;
      p.shadow_blur = 3.0f;
      break;

    case SkinId::ModernFlat:
      // index.scss:165-183
      p.border = PanelPaint::BorderKind::Double;
      p.border_color = vars_.theme_border;
      p.radius = 8.0f * g;   // ← --border-radius: calc(8px * var(--control-skin-val1))
      p.has_shadow = true;
      p.shadow_color = Color4f::hex(0x000000, (light ? 0.10f : 0.45f) * v);
      p.shadow_dy = 4.0f * v;
      p.shadow_blur = 16.0f * v;
      break;

    case SkinId::FrostedGlass:
      // index.scss:185-198
      p.border = PanelPaint::BorderKind::DoubleGlass;
      p.border_color = light ? Color4f::hex(0x000000, std::clamp(0.08f + 0.06f * d, 0.04f, 0.35f))
                             : Color4f::hex(0xFFFFFF, std::clamp(0.10f + 0.06f * d, 0.05f, 0.40f));
      p.bg = light ? Color4f::hex(0xFFFFFF, std::clamp(0.08f + 0.14f * d + 0.05f * v, 0.02f, 0.90f))
                   : Color4f::hex(0x1E2438, std::clamp(0.04f + 0.12f * d + 0.05f * v, 0.02f, 0.88f));
      p.radius = 8.0f;
      p.has_shadow = true;
      p.shadow_color = Color4f::hex(0x000000, 0.06f);
      p.shadow_dy = 8.0f;
      p.shadow_blur = 32.0f * std::max(0.25f, v);
      p.has_glow = (v > 0.05f);
      p.glow_color = Color4f::hex(0xFFFFFF, (light ? 0.12f : 0.06f) * v);
      p.glow_blur = 12.0f * v;
      break;

    case SkinId::Cyberpunk:
      // index.scss:231-255
      p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x05070C);
      p.border = PanelPaint::BorderKind::DoubleNeon;
      p.border_color = Color4f::hex(0xFF0055);
      p.radius = 8.0f;
      p.pulse = true;
      p.pulse_from = Color4f::hex(0xFF0055, 0.60f);
      p.pulse_to = Color4f::hex(0xFF0055, 1.00f);
      p.pulse_period_s = 2.5f / (v + 0.001f);
      p.has_glow = (d > 0.01f);
      p.glow_color = Color4f::hex(0xFF0055, 0.25f * std::min(d, 2.5f));
      p.glow_blur = 12.0f * d;
      break;

    case SkinId::Monochrome:
      // index.scss:257-296
      p.bg = vars_.theme_panel_bg;
      p.border = PanelPaint::BorderKind::Double;
      p.border_color = vars_.theme_border;
      p.radius = 8.0f * g;   // ← --border-radius: calc(8px * var(--control-skin-val1))
      p.has_shadow = false;
      break;
  }
  return p;
}

// ---------- 卡片（get3DCardStyle：4 卡型 × 6 皮肤） ----------
// 出处：src/utils/button-styles/card.ts:6-245。
CardPaint ThemeService::card_paint(const char* card_type) const {
  // src: src/utils/button-styles/card.ts:62-63
  const bool light = is_light();
  const float d = depth();       // --depth-factor
  const float v = vibrancy();    // --vibrancy-factor ≡ --control-skin-val2
  const float g = val1();        // --control-skin-val1
  const bool is_primary = std::strcmp(card_type, "primary") == 0;
  const bool is_upload = std::strcmp(card_type, "upload") == 0;
  const bool is_download = std::strcmp(card_type, "download") == 0;

  CardPaint p{};
  p.bg = vars_.theme_panel_bg;
  p.text_color = palette_.primary_text;
  p.radius = 6.0f;

  switch (skin_) {
    case SkinId::Retro3d: {
      // :508-567 radius 6 固定、无边框、135° 多停渐变 + 浮雕硬影 + 金 glow
      p.grad_bg = true;
      p.grad_angle = 135.0f;
      Color4f bevel;
      if (is_primary) {
        p.text_color = Color4f::hex(0x2C1F03);
        bevel = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x5D4037);
        p.grad[0] = {0.00f, Color4f::hex(0xFFE082)};
        p.grad[1] = {0.20f, Color4f::hex(0xFFD54F)};
        p.grad[2] = {0.45f, Color4f::hex(0xFFC107)};
        p.grad[3] = {0.70f, Color4f::hex(0xFFB300)};
        p.grad[4] = {0.85f, Color4f::hex(0xD4AF37)};
        p.grad[5] = {1.00f, Color4f::hex(0xB8860B)};
        p.grad_count = 6;
      } else if (is_upload) {
        p.text_color = light ? Color4f::hex(0x5D4037) : Color4f::hex(0xFFD54F);
        bevel = light ? Color4f::hex(0xB38F1E) : Color4f::hex(0x705407);
        if (light) {
          p.grad[0] = {0.00f, Color4f::hex(0xFFE0B2)};
          p.grad[1] = {0.35f, Color4f::hex(0xFFCC80)};
          p.grad[2] = {0.70f, Color4f::hex(0xFFA726)};
          p.grad[3] = {1.00f, Color4f::hex(0xFB8C00)};
        } else {
          p.grad[0] = {0.00f, Color4f::hex(0xE65100)};
          p.grad[1] = {0.40f, Color4f::hex(0xD84315)};
          p.grad[2] = {0.75f, Color4f::hex(0xBF360C)};
          p.grad[3] = {1.00f, Color4f::hex(0x800000)};
        }
        p.grad_count = 4;
      } else if (is_download) {
        p.text_color = light ? Color4f::hex(0x37474F) : Color4f::hex(0x90A4AE);
        bevel = light ? Color4f::hex(0x78909C) : Color4f::hex(0x263238);
        if (light) {
          p.grad[0] = {0.00f, Color4f::hex(0xECEFF1)};
          p.grad[1] = {0.35f, Color4f::hex(0xCFD8DC)};
          p.grad[2] = {0.70f, Color4f::hex(0x90A4AE)};
          p.grad[3] = {1.00f, Color4f::hex(0x78909C)};
        } else {
          p.grad[0] = {0.00f, Color4f::hex(0x263238)};
          p.grad[1] = {0.40f, Color4f::hex(0x37474F)};
          p.grad[2] = {0.75f, Color4f::hex(0x455A64)};
          p.grad[3] = {1.00f, Color4f::hex(0x1C252C)};
        }
        p.grad_count = 4;
      } else {
        p.text_color = light ? Color4f::hex(0x3C2F0F) : Color4f::hex(0x2C1F03);
        bevel = light ? Color4f::hex(0xB8860B) : Color4f::hex(0x755500);
        if (light) {
          p.grad[0] = {0.00f, Color4f::hex(0xFFFDE7)};
          p.grad[1] = {0.35f, Color4f::hex(0xFFF9C4)};
          p.grad[2] = {0.70f, Color4f::hex(0xFFF59D)};
          p.grad[3] = {1.00f, Color4f::hex(0xFFE082)};
        } else {
          p.grad[0] = {0.00f, Color4f::hex(0xFFF59D)};
          p.grad[1] = {0.35f, Color4f::hex(0xFBC02D)};
          p.grad[2] = {0.70f, Color4f::hex(0xF57F17)};
          p.grad[3] = {1.00f, Color4f::hex(0xE65100)};
        }
        p.grad_count = 4;
      }
      p.radius = 6.0f;      // CSS borderRadius '6px'（不随 depth）
      p.border_w = 0.0f;    // border: none
      p.has_shadow = true;
      p.shadow_color = bevel;
      p.shadow_dy = 3.0f * d;
      p.shadow_blur = 2.0f * d;
      // ← :556/:561 boxShadow 首层 '0 1px 0 0 ${bevelShadowDark}' = 底缘 1px 实线
      p.has_bottom_bevel = true;
      p.bottom_bevel_color = bevel;
      if (is_primary || is_upload || is_download) {
        p.has_glow = true;    // 这三类额外带金色 glow（:543-546）
        p.glow_color = Color4f::hex(0xFFC107, 0.20f * v);
        p.glow_blur = 8.0f * v;
      }
      break;
    }

    case SkinId::Original:
      // :572-606：radius 8*val1、无边框、upload/download 底色由 val2 调色相
      p.radius = 8.0f * g;   // ← borderRadius: calc(8px * var(--control-skin-val1))
      p.border_w = 0.0f;
      p.text_color = is_primary ? Color4f::hex(0xFFFFFF) : text_primary();
      if (is_primary) {
        p.bg = primary();  // var(--primary-main)（hsl 239*val2）
      } else if (is_upload) {
        p.bg = light ? hsl_to_rgb(38.0f * v, 1.0f, 0.94f)
                     : hsl_to_rgb(38.0f * v, 1.0f, 0.12f);
      } else if (is_download) {
        p.bg = light ? hsl_to_rgb(200.0f * v, 0.90f, 0.94f)
                     : hsl_to_rgb(200.0f * v, 1.0f, 0.12f);
      } else {
        p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x1E1F29);
      }
      p.has_shadow = true;
      p.shadow_color = Color4f::hex(0x000000, 0.08f);
      p.shadow_dy = 1.0f;
      p.shadow_blur = 3.0f;
      break;

    case SkinId::ModernFlat:
      // :611-648：radius 8*val1、1px 边框、阴影 0 2v 8v
      p.radius = 8.0f * g;   // ← borderRadius: calc(8px * var(--control-skin-val1))
      p.border_w = 1.0f;
      p.border = light ? Color4f::hex(0xE5E7EB) : Color4f::hex(0x374151);
      p.text_color = is_primary ? Color4f::hex(0xFFFFFF) : text_primary();
      if (is_primary) {
        p.bg = primary();
      } else if (is_upload) {
        p.bg = light ? Color4f::hex(0xFFFDE7) : Color4f::hex(0xFFA000);
      } else if (is_download) {
        p.bg = light ? Color4f::hex(0xE3F2FD) : Color4f::hex(0x00B0FF);
      } else {
        p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x1F2937);
      }
      p.has_shadow = true;
      p.shadow_color = Color4f::hex(0x000000, (light ? 0.06f : 0.30f) * v);
      p.shadow_dy = 2.0f * v;
      p.shadow_blur = 8.0f * v;
      break;

    case SkinId::FrostedGlass:
      // :653-669 + index.scss:186-198：radius 8 固定、1px 玻璃边、半透明底
      p.radius = 8.0f;
      p.border_w = 1.0f;
      p.border = Color4f::hex(light ? 0x000000 : 0xFFFFFF, 0.10f);
      p.bg = is_primary ? with_alpha(primary(), 0.15f)
                        : Color4f::hex(0xFFFFFF, 0.06f);
      p.text_color = text_primary();
      p.has_shadow = true;
      p.shadow_color = Color4f::hex(0x000000, 0.06f);
      p.shadow_dy = 8.0f;
      p.shadow_blur = (light ? 24.0f : 32.0f) * std::max(0.25f, v);
      break;

    case SkinId::Cyberpunk:
      // :674-711：radius 0、霓虹单线、发光 0 0 10d
      p.radius = 0.0f;
      p.border_w = 1.0f;
      p.border = light ? (is_upload ? Color4f::hex(0xF59E0B)
                                    : (is_download ? Color4f::hex(0x3B82F6)
                                                   : Color4f::hex(0x00B0FF)))
                       : Color4f::hex(0x00F0FF);
      p.bg = light ? (is_primary ? Color4f::hex(0xF8FAFC) : Color4f::hex(0xFFFFFF))
                   : (is_primary ? Color4f::hex(0x0A0E17) : Color4f::hex(0x05070C));
      p.text_color = light ? (is_upload ? Color4f::hex(0xB78103)
                                        : (is_download ? Color4f::hex(0x0066CC)
                                                       : Color4f::hex(0x0F172A)))
                           : Color4f::hex(0x00F0FF);
      p.has_shadow = true;
      p.shadow_color = light ? Color4f::hex(0x00B0FF, 0.15f * d)
                             : Color4f::hex(0x00F0FF, 0.20f * d);
      p.shadow_dy = 0.0f;
      p.shadow_blur = 10.0f * d;
      break;

    case SkinId::Monochrome:
      // src: src/assets/styles/index.scss:257-269 .MuiPaper-root radius = 8*val1
      p.radius = 8.0f * g;
      p.border_w = 1.0f * v;
      p.border = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x000000);
      p.text_color = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.has_shadow = false;
      break;
  }
  return p;
}

PopupSkinTokens ThemeService::popup_tokens() const {
  const bool light = is_light();
  const bool retro = (skin_ == SkinId::Retro3d);
  PopupSkinTokens t{};
  t.is_retro3d = retro;
  // src: src/utils/popup-styles.ts:59-69
  t.primary_text = retro ? (light ? Color4f::hex(0x3C2F0F) : Color4f::hex(0x2C1F03))
                         : palette_.primary_text;
  t.secondary_text = retro ? Color4f::hex(0x2C1F03, 0.78f) : palette_.secondary_text;
  t.disabled_text = retro ? Color4f::hex(0x2C1F03, 0.50f)
                          : (light ? Color4f::hex(0x000000, 0.38f)
                                   : Color4f::hex(0xFFFFFF, 0.50f));
  t.divider_color = retro ? Color4f::hex(0x2C1F03, 0.20f) : vars_.divider_color;
  // src: src/utils/popup-styles.ts:70-82
  t.ready_color = retro ? Color4f::hex(0x1B5E20) : palette_.success_color;
  t.warning_color = retro ? Color4f::hex(0x8C4A03) : palette_.warning_color;
  t.error_color = retro ? Color4f::hex(0x991B1B) : palette_.error_color;
  t.info_color = retro ? Color4f::hex(0x01579B) : palette_.info_color;
  t.accent_primary = retro ? Color4f::hex(0x0D47A1) : palette_.primary_color;
  // src: src/utils/popup-styles.ts:75-82
  t.subtle_bg = retro ? Color4f::hex(0xFFFFFF, 0.22f)
                      : (light ? Color4f::hex(0x000000, 0.04f)
                               : Color4f::hex(0xFFFFFF, 0.06f));
  t.subtle_border = retro ? Color4f::hex(0x7A3E00, 0.32f) : vars_.divider_color;
  return t;
}

CardPaint ThemeService::popup_card_paint() const {
  CardPaint p = card_paint("default");
  if (skin_ == SkinId::FrostedGlass) {
    const bool light = is_light();
    // src: src/utils/popup-styles.ts:95-109
    p.grad_bg = false;
    p.bg = light ? Color4f::hex(0xF8FAFC, 0.97f) : Color4f::hex(0x1A1C26, 0.97f);
    p.border_w = 1.0f;
    p.border = light ? Color4f::hex(0x0F172A, 0.12f)
                     : Color4f::hex(0xFFFFFF, 0.14f);
    // src: src/utils/popup-styles.ts:106-108
    p.has_shadow = true;
    p.shadow_color = light ? Color4f::hex(0x0F172A, 0.16f)
                           : Color4f::hex(0x000000, 0.45f);
    p.shadow_dy = 8.0f;
    p.shadow_blur = 28.0f;
  }
  return p;
}

// 霓虹呼吸相位（← CSS `animation: … ease-in-out infinite alternate`）
float ThemeService::pulse_phase(float time_s) const {
  if (skin_ != SkinId::Cyberpunk) return 0.0f;
  const float period = 2.5f / (vibrancy_ + 0.001f);
  if (period <= 0.0001f) return 0.0f;
  float t = std::fmod(time_s, period) / period;
  if (t < 0.0f) t += 1.0f;
  return t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f;
}

}  // namespace clashwin::ui::theme
