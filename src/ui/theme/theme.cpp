// ThemeService 实现（T12）：解析链 + 六皮肤控件族参数表。
// 差异标注：CSS 渐变/多层 boxShadow/backdropFilter → C++ 离散色近似。
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "util/log.h"

namespace clashwin::ui::theme {

namespace {
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

namespace {
Color4f with_alpha(const Color4f& c, float a) { return {c.r, c.g, c.b, a}; }
}  // namespace

// ---------- 逐皮肤的滑块规格 ----------
// src: pages/_layout/components/theme-settings-card.tsx:59-134
//   getSlider1Label / getSlider1Max（:59-99）
//   getSlider2Label / getSlider2Max（:101-134）
SkinSliderSpec skin_slider_spec(SkinId skin) {
  SkinSliderSpec s{};
  switch (skin) {
    case SkinId::Retro3d:
      // :63 'Depth' / :86 max 1.0；:120 'Vibrancy' / :103 max 5.0
      s.max1 = 1.0f;  s.max2 = 5.0f;
      s.label1 = L"Depth";    s.label2 = L"Vibrancy";
      break;
    case SkinId::Original:
      // :65 'Radius' / :89 max 3.0；:122 'Accent' / :105 max 3.0
      s.max1 = 3.0f;  s.max2 = 3.0f;
      s.label1 = L"Radius";   s.label2 = L"Accent";
      break;
    case SkinId::ModernFlat:
      // :67 'Roundness' / :92 max 3.0；:124 'Shadow' / :104 max 5.0
      s.max1 = 3.0f;  s.max2 = 5.0f;
      s.label1 = L"Roundness"; s.label2 = L"Shadow";
      break;
    case SkinId::FrostedGlass:
      // :69 'Opacity' / :95 max 5.0；:126 'Blur' / :108 max 5.0
      s.max1 = 5.0f;  s.max2 = 5.0f;
      s.label1 = L"Opacity";  s.label2 = L"Blur";
      break;
    case SkinId::Cyberpunk:
      // :71 'Glow' / :98 max 5.0；:128 'Speed' / :111 max 5.0
      s.max1 = 5.0f;  s.max2 = 5.0f;
      s.label1 = L"Glow";     s.label2 = L"Speed";
      break;
    case SkinId::Monochrome:
      // :73 'Radius' / :89 max 3.0；:130 'Border' / :108 max 5.0
      s.max1 = 3.0f;  s.max2 = 5.0f;
      s.label1 = L"Radius";   s.label2 = L"Border";
      break;
  }
  return s;
}

// 逐皮肤因子缺省（← use-skin-controls.ts:13-24 readFactor）
//   saved = localStorage['clash-mini-{skin}-{key}']  → 命中即用
//   否则若 skin===DEFAULT_SKIN('retro-3d')：查 legacy 键
//     'clash-mini-depth-factor' / 'clash-mini-vibrancy-factor'
//   否则返回 1.0（注意：非 retro 皮肤缺省是 1.0，不是 retro 的 0.3）
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
//   layout.scss:319 把该变量作用到 .MuiSlider-root / .MuiInputBase-root /
//   .MuiTypography-root / .MuiListItem-root / .MuiMenuItem-root /
//   .MuiListItemButton-root —— 即「除图标字体外的全部文本」。
//
//   各皮肤定义（← layout.scss:324/328/352/371/418/442）：
//     retro-3d      Trebuchet MS, SimHei, sans-serif
//     original      Segoe UI, Microsoft YaHei, sans-serif
//     modern-flat   Outfit, DengXian, sans-serif
//     frosted-glass Segoe UI Light, Microsoft YaHei Light, sans-serif
//     cyberpunk     Consolas, NSimSun, monospace
//     monochrome    Georgia, KaiTi, serif
//
//   ★ Mini 是 CSS font-family **列表**（多字体回退）。DirectWrite 的
//     CreateTextFormat 只接受单一 family，因此这里保留完整候选链，
//     由 D2D 层按「本机是否安装该字体」择一 —— 首选缺失时落到同族下一候选，
//     而不是静默变成默认字体（那会让 6 套皮肤在缺字体机型上看起来一样）。
const std::vector<std::wstring>& skin_font_candidates(SkinId skin) {
  // 末位统一是 Windows 通用族，任何机器都至少有其中之一
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
  // ← use-skin-controls.ts:13-24 readFactor：因子缺省**逐皮肤不同**
  //   （retro-3d 是 0.3/1.0，其余 5 套是 1.0/1.0）。
  //   Win 侧此前 init() 不重置因子，导致切皮肤后仍沿用 retro-3d 的 0.3 ——
  //   original 的 `--border-radius: calc(8px * val1)` 会算成 2.4px 而非 8px。
  skin_default_factors(skin_, &depth_, &vibrancy_);
  ui_font_.clear();  // ★ §2.7 init 可能换皮肤 → 字体失效重解析
  rebuild();
}

void ThemeService::set_factors(float depth, float vibrancy) {
  if (depth_ == depth && vibrancy_ == vibrancy) return;
  depth_ = depth;
  vibrancy_ = vibrancy;
  rebuild();  // original 的 --primary-main 由 val2 决定，必须重算
  notify();   // 关键：不 notify 则滑块只改数值不重绘（此前"能滑但没效果"的根因之一）
}
void ThemeService::rebuild() {
  palette_ = base_palette(mode_);
  // ← use-custom-theme.ts:170-181 主色解析链：
  // setting.primary_color || 系统 AccentColor || 色板缺省；original 强制 hsl
  if (skin_ == SkinId::Original) {
    // ← index.scss:299「--primary-main: hsl(calc(239 * var(--control-skin-val2)), 26%, 49%)」
    //   Accent 滑块（val2）直接改主色色相，必须跟随 vibrancy_ 而非写死 1.0
    palette_.primary_color = original_primary(vibrancy_);
  } else if (!primary_override_.empty()) {
    if (auto c = parse_hex(primary_override_))
      palette_.primary_color = *c;
    else
      CWLOG_WARN("Theme: primary_color 解析失败 " + primary_override_);
  }
  // 系统 AccentColor 探测（Windows AccentColor 注册表/DWM）→ T17 接
  // （use-custom-theme.ts:12-40 用 CSS AccentColor 探测；C++ 侧落 T17）
  vars_ = css_vars(mode_, skin_);
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
  // ★ §2.7 皮肤切换 → 全局字体失效（下次取文本格式时按新皮肤候选链重解析）
  ui_font_.clear();
  rebuild();
  notify();  // → 控件 invalidate（热生效）
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

// ---------- Button（← get3DButtonStyle colorType 分支 × 6 皮肤） ----------
// 【近似差异】retro-3d 渐变（6 站点）→ 取中段站色为 fill、hover 渐变取其
// 中段站色；多层 bevel boxShadow（×depth/vibrancy）由 Button::draw_button_bg
// 的重写点用 depth_factor()/vibrancy_factor() 绘制（T13 节点卡接入时按需）。
ButtonPaint ThemeService::button_paint(const char* color_type,
                                      bool outlined) const {
  bool light = is_light();
  bool is_primary = std::strcmp(color_type, "primary") == 0;
  bool is_error = std::strcmp(color_type, "error") == 0;
  // 语义化因子（← Mini 四变量注入 + layout.scss !important 覆盖）
  //   dep → --depth-factor（retro-3d 的 3D 深度）；original/modern-flat 被强制 0
  //   g   → --control-skin-val1（original=Radius / modern=Roundness：圆角）
  //   v   → --vibrancy-factor ≡ --control-skin-val2
  const float dep = depth();
  const float g = val1();
  const float v = vibrancy();
  ButtonPaint p{};
  p.text_disabled = light ? Color4f::hex(0x9e9e9e) : Color4f::hex(0x6e6e75);
  p.fill_disabled = light ? Color4f::hex(0xe0e0e0) : Color4f::hex(0x32323a);

  switch (skin_) {
    case SkinId::Retro3d: {
      // 金色 3D（← :39-140）：135° 多停渐变 + 浮雕 + 外发光 + 文字阴影 + 位移
      p.use_grad = true;
      p.border_w = 2.0f * dep;   // ← :84 border: calc(2px * --depth-factor)
      p.radius = 6;                 // ← :83 borderRadius '6px'（不乘 depth）
      p.weight = 700;      // bold
      p.translate_hover = -2.0f;   // transform: translateY(-2px)
      p.translate_active = 3.0f;   // transform: translateY(3px)
      if (is_primary) {
        p.text = Color4f::hex(0x2C1F03);
        p.border = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00);
        p.bevel_dark = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x5D4037);
        p.bevel_hi = light ? Color4f::hex(0xFFFFFF, 0.8f) : Color4f::hex(0xFFFFFF, 0.08f);
        p.glow = Color4f::hex(0xFFC107, 0.2f * v);  // 0.2*vibrancy
        p.glow_blur = 8.0f * v;
        p.text_shadow = Color4f::hex(0xFFFFFF, 0.45f); p.text_shadow_dy = 1.0f;
        p.grad[0] = {0.0f, Color4f::hex(0xFFE082)};
        p.grad[1] = {0.20f, Color4f::hex(0xFFD54F)};
        p.grad[2] = {0.45f, Color4f::hex(0xFFC107)};
        p.grad[3] = {0.70f, Color4f::hex(0xFFB300)};
        p.grad[4] = {0.85f, Color4f::hex(0xD4AF37)};
        p.grad[5] = {1.0f, Color4f::hex(0xB8860B)};
        p.grad_count = 6;
        p.grad_hover[0] = {0.0f, Color4f::hex(0xFFF59D)};
        p.grad_hover[1] = {0.20f, Color4f::hex(0xFFE082)};
        p.grad_hover[2] = {0.45f, Color4f::hex(0xFFD54F)};
        p.grad_hover[3] = {0.70f, Color4f::hex(0xFFC107)};
        p.grad_hover[4] = {0.85f, Color4f::hex(0xFFB300)};
        p.grad_hover[5] = {1.0f, Color4f::hex(0xD4AF37)};
      } else if (is_error) {
        p.text = Color4f::hex(0xFFFFFF);
        p.border = light ? Color4f::hex(0xB71C1C) : Color4f::hex(0x6B0505);
        p.bevel_dark = light ? Color4f::hex(0x8B0000) : Color4f::hex(0x4A0404);
        p.bevel_hi = light ? Color4f::hex(0xFFFFFF, 0.8f) : Color4f::hex(0xFFFFFF, 0.08f);
        p.glow = Color4f::hex(0xD50000, 0.2f * v);
        p.glow_blur = 8.0f * v;
        p.text_shadow = Color4f::hex(0x000000, 0.4f); p.text_shadow_dy = -1.0f;
        p.grad[0] = {0.0f, Color4f::hex(0xFF8A80)};
        p.grad[1] = {0.30f, Color4f::hex(0xFF5252)};
        p.grad[2] = {0.75f, Color4f::hex(0xD50000)};
        p.grad[3] = {1.0f, Color4f::hex(0x880E4F)};
        p.grad_count = 4;
        p.grad_hover[0] = {0.0f, Color4f::hex(0xFFAB91)};
        p.grad_hover[1] = {0.30f, Color4f::hex(0xFF8A80)};
        p.grad_hover[2] = {0.75f, Color4f::hex(0xFF5252)};
        p.grad_hover[3] = {1.0f, Color4f::hex(0xC62828)};
      } else {
        p.text = light ? Color4f::hex(0x3C2F0F) : Color4f::hex(0x2C1F03);
        p.border = light ? Color4f::hex(0xD4AF37) : Color4f::hex(0x8A6D00);
        p.bevel_dark = light ? Color4f::hex(0xB8860B) : Color4f::hex(0x755500);
        p.bevel_hi = light ? Color4f::hex(0xFFFFFF, 0.8f) : Color4f::hex(0xFFFFFF, 0.08f);
        p.glow = Color4f::hex(0xFFC107, 0.2f * v);
        p.glow_blur = 8.0f * v;
        p.text_shadow = Color4f::hex(0xFFFFFF, 0.45f); p.text_shadow_dy = 1.0f;
        if (outlined) {
          // ← :57-63 outlined && default：linear-gradient(to bottom,
          //   rgba(255,253,231,.4) → rgba(255,245,157,.2)) / 暗色两档
          p.grad_angle = 180.0f;
          p.grad[0] = {0.0f, light ? Color4f::hex(0xFFFDE7, 0.40f)
                                   : Color4f::hex(0x2A2413, 0.40f)};
          p.grad[1] = {1.0f, light ? Color4f::hex(0xFFF59D, 0.20f)
                                   : Color4f::hex(0x17140B, 0.20f)};
          p.grad_count = 2;
          p.grad_hover[0] = p.grad[0];
          p.grad_hover[1] = p.grad[1];
          // ← :40-52 outlined default：border #D4AF37/#8A6D00、文字 #3C2F0F/#FFE082
          p.border = light ? Color4f::hex(0xD4AF37) : Color4f::hex(0x8A6D00);
          p.text = light ? Color4f::hex(0x3C2F0F) : Color4f::hex(0xFFE082);
        } else if (light) {
          p.grad[0] = {0.0f, Color4f::hex(0xFFFDE7)};
          p.grad[1] = {0.35f, Color4f::hex(0xFFF9C4)};
          p.grad[2] = {0.70f, Color4f::hex(0xFFF59D)};
          p.grad[3] = {1.0f, Color4f::hex(0xFFE082)};
          p.grad_count = 4;
          p.grad_hover[0] = {0.0f, Color4f::hex(0xFFFDE7)};
          p.grad_hover[1] = {0.20f, Color4f::hex(0xFFF9C4)};
          p.grad_hover[2] = {0.50f, Color4f::hex(0xFFF59D)};
          p.grad_hover[3] = {1.0f, Color4f::hex(0xFFD54F)};
        } else {
          p.grad[0] = {0.0f, Color4f::hex(0xFFF59D)};
          p.grad[1] = {0.35f, Color4f::hex(0xFBC02D)};
          p.grad[2] = {0.70f, Color4f::hex(0xF57F17)};
          p.grad[3] = {1.0f, Color4f::hex(0xE65100)};
          p.grad_count = 4;
          p.grad_hover[0] = {0.0f, Color4f::hex(0xFFF9C4)};
          p.grad_hover[1] = {0.20f, Color4f::hex(0xFFF59D)};
          p.grad_hover[2] = {0.50f, Color4f::hex(0xFBC02D)};
          p.grad_hover[3] = {1.0f, Color4f::hex(0xF57F17)};
        }
      }
      p.fill = p.grad[p.grad_count / 2].color;   // last_fill_ 兜底
      p.fill_hover = p.grad_hover[p.grad_count / 2].color;
      p.fill_pressed = p.fill;  // active = inset bevel（沿用 default 渐变）
      // 禁用态：MUI Button.disabled 缺省（src: MUI palette action.disabledBackground / text.disabled）
      p.fill_disabled = light ? Color4f::hex(0xe0e0e0) : Color4f::hex(0x32323a);
      p.text_disabled = light ? Color4f::hex(0x9e9e9e) : Color4f::hex(0x6e6e75);
      break;
    }
    case SkinId::Original: {
      // ← :144-196：primary=var(--primary-main) 白字；default 白底/暗 #2e303d
      p.fill = is_primary ? primary() : (is_error ? Color4f::hex(0xFF3B30)
                                            : (light ? Color4f::hex(0xffffff)
                                                     : Color4f::hex(0x2e303d)));
      p.fill_hover = is_primary ? primary() : (is_error ? Color4f::hex(0xB71C1C)
                                                  : (light ? Color4f::hex(0xf5f5f5)
                                                           : Color4f::hex(0x242530)));
      p.fill_pressed = p.fill_hover;
      p.text = (is_primary || is_error) ? Color4f::hex(0xFFFFFF) : text_primary();
      p.border = is_primary ? primary()
                         : (is_error ? Color4f::hex(0xFF3B30) : vars_.theme_border);
      p.radius = 4.0f * g;   // ← :186 borderRadius calc(4px * val1)
      p.border_w = 1.0f;          // ← :185 border: '1px solid'
      p.weight = 500;
      p.translate_active = 1.0f;   // transform: translateY(1px)
      // ← :196 boxShadow：default 无、primary/error 0 1px 3px rgba(0,0,0,.15)
      if (is_primary || is_error) {
        p.has_shadow = true;
        p.shadow_color = Color4f::hex(0x000000, 0.15f);
        p.shadow_dy = 1.0f;
        p.shadow_blur = 3.0f;
      }
      break;
    }
    case SkinId::ModernFlat: {
      // ← :199-255：primary=palette.primary；default #f3f4f6/#1f2937
      p.fill = is_primary ? primary() : (is_error ? Color4f::hex(0xFF3B30)
                                            : (light ? Color4f::hex(0xf3f4f6)
                                                     : Color4f::hex(0x1f2937)));
      p.fill_hover = is_primary ? Color4f::lerp(primary(), Color4f::hex(0x000000), 0.15f)
                             : (light ? Color4f::hex(0xe5e7eb) : Color4f::hex(0x374151));
      p.fill_pressed = p.fill_hover;
      p.text = (is_primary || is_error) ? Color4f::hex(0xFFFFFF) : text_primary();
      p.border = is_primary ? primary() : (light ? Color4f::hex(0xd1d5db)
                                              : Color4f::hex(0x4b5563));
      p.radius = 6.0f * g;   // ← :258 borderRadius calc(6px * val1)
      p.border_w = 1.0f;          // ← :257 border: '1px solid'
      p.weight = 700;
      p.translate_active = 1.0f;   // transform: translateY(1px)
      // ← :259 boxShadow 0 2v 4v rgba(0,0,0, .06v)
      p.has_shadow = true;
      p.shadow_color = Color4f::hex(0x000000, 0.06f * v);
      p.shadow_dy = 2.0f * v;
      p.shadow_blur = 4.0f * v;
      break;
    }
    case SkinId::FrostedGlass: {
      // ← :258-315：半透明 + backdropFilter（D2D 无此滤镜，保留 alpha 色值）
      p.fill = is_primary ? with_alpha(primary(), 0.25f)
                       : (light ? Color4f::hex(0x000000, 0.06f)
                                : Color4f::hex(0xFFFFFF, 0.08f));
      p.fill_hover = is_primary ? with_alpha(primary(), 0.40f)
                             : (light ? Color4f::hex(0x000000, 0.12f)
                                      : Color4f::hex(0xFFFFFF, 0.15f));
      p.fill_pressed = p.fill_hover;
      p.text = text_primary();
      p.border = light ? Color4f::hex(0x000000, 0.12f)
                       : Color4f::hex(0xFFFFFF, 0.12f);
      p.radius = 8;               // ← :318 borderRadius '8px'（不乘 val1）
      p.border_w = 1.0f;          // ← :317 border: 1px solid rgba(...)
      p.weight = 400;
      p.translate_hover = -1.0f;   // translateY(-1px)
      p.translate_active = 1.0f;   // translateY(1px)
      // ← :324 boxShadow 0 4px 12px rgba(0,0,0,.08)
      p.has_shadow = true;
      p.shadow_color = Color4f::hex(0x000000, 0.08f);
      p.shadow_dy = 4.0f;
      p.shadow_blur = 12.0f * std::max(0.25f, v);
      break;
    }
    case SkinId::Cyberpunk: {
      // ← :350-420：霓虹描边 + boxShadow 0 0 calc(5d) borderClr
      Color4f neon = is_primary ? Color4f::hex(0x00f0ff)
                             : (is_error ? Color4f::hex(0xff0055)
                                      : Color4f::hex(0x39ff14));
      p.fill = light ? Color4f::hex(0xffffff)
                     : (is_primary ? Color4f::hex(0x0a0e17)
                                : (is_error ? Color4f::hex(0x1c0a0a)
                                         : Color4f::hex(0x0d1117)));
      p.fill_hover = light ? (is_primary ? Color4f::hex(0xe0f7fa)
                                      : (is_error ? Color4f::hex(0xffebee)
                                                  : Color4f::hex(0xe8f5e9)))
                           : (is_primary ? Color4f::hex(0x101726)
                                      : (is_error ? Color4f::hex(0x2c1010)
                                                  : Color4f::hex(0x161b22)));
      p.fill_pressed = p.fill_hover;
      // ← :381-389 textClr / borderClr 按 colorType × light 分别取值
      p.text = light ? (is_primary ? Color4f::hex(0x006064)
                                : (is_error ? Color4f::hex(0xc2185b)
                                            : Color4f::hex(0x1b5e20)))
                     : neon;
      p.border = light ? (is_primary ? Color4f::hex(0x00bcd4)
                                  : (is_error ? Color4f::hex(0xff0055)
                                              : Color4f::hex(0x4caf50)))
                       : neon;
      p.radius = 0;               // ← :401 borderRadius '0px'
      p.border_w = 1.0f;          // ← :400 border: '1px solid'
      p.weight = 700;
      p.translate_hover = -1.0f;    // translateY(-1px)
      p.translate_active = 1.0f;    // translateY(1px)
      // ← :403 boxShadow 0 0 calc(5px * depth) borderClr（常亮，非仅 hover）
      p.glow = with_alpha(p.border, 0.55f);
      p.glow_blur = 5.0f * dep;
      break;
    }
    case SkinId::Monochrome: {
      // ← :424-486：黑白对调
      p.fill = is_primary ? (light ? Color4f::hex(0x000000) : Color4f::hex(0xffffff))
                       : (light ? Color4f::hex(0xffffff) : Color4f::hex(0x000000));
      p.fill_hover = is_primary
                         ? (light ? Color4f::hex(0x333333) : Color4f::hex(0xcccccc))
                         : (light ? Color4f::hex(0xcccccc) : Color4f::hex(0x333333));
      p.fill_pressed = p.fill_hover;
      p.text = is_primary ? (light ? Color4f::hex(0xffffff) : Color4f::hex(0x000000))
                       : (light ? Color4f::hex(0x000000) : Color4f::hex(0xffffff));
      p.border = light ? Color4f::hex(0x000000) : Color4f::hex(0xffffff);
      p.radius = 0.0f;        // src: button-styles.ts:719 borderRadius: '0px'
      p.border_w = 1.0f * v;  // ← :454 calc(1px * --vibrancy-factor)
      p.weight = 700;
      p.translate_active = 1.0f;   // transform: translateY(1px)
      // ← :457 boxShadow: 'none'
      break;
    }
  }
  return p;
}

// ---------- Segmented（← get3DSegmentedContainer/Active/TextColor） ----------
// ★ d/v 消费点（← button-styles.ts:932-990 容器 + :992-1103 选中块）：
//   retro-3d   容器 inset 0 2.5d 4.5d(亮)/0 3d 5d(暗)；选中块多层 bevel + 金辉光
//   original   容器圆角 4*val1；选中块圆角 3*val1
//   modern-flat选中块圆角 4*val1
//   cyberpunk  选中块外辉光 0 0 8d
SegmentedPaint ThemeService::segmented_paint() const {
  bool light = is_light();
  // 语义化因子（← Mini 四变量注入 + layout.scss !important 覆盖）
  const float d = depth();       // --depth-factor
  const float v = vibrancy();    // --vibrancy-factor
  const float g = val1();        // --control-skin-val1
  SegmentedPaint p{};
  // src: routing-preference-card.tsx:110-114 & theme-settings-card.tsx:194-200
  p.text = (skin_ == SkinId::Retro3d)
               ? Color4f::hex(0x2C1F03, 0.75f)
               : palette_.secondary_text;
  switch (skin_) {
    case SkinId::Retro3d:
      p.container_bg = light ? Color4f::hex(0xD4AF37, 0.06f)
                             : Color4f::hex(0xD4AF37, 0.08f);
      p.container_border = light ? Color4f::hex(0xD4AF37, 0.35f)
                                 : Color4f::hex(0xD4AF37, 0.22f);
      p.active_bg = Color4f::hex(0xFFC107);  // 渐变 45% 站
      p.active_border = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00);
      p.active_text = Color4f::hex(0x2C1F03);  // ← ActiveTextColor retro
      p.active_radius = 3;
      // ← :1006-1007 选中块 135° 六停金渐变
      p.active_grad[0] = {0.00f, Color4f::hex(0xFFE082)};
      p.active_grad[1] = {0.20f, Color4f::hex(0xFFD54F)};
      p.active_grad[2] = {0.45f, Color4f::hex(0xFFC107)};
      p.active_grad[3] = {0.70f, Color4f::hex(0xFFB300)};
      p.active_grad[4] = {0.85f, Color4f::hex(0xD4AF37)};
      p.active_grad[5] = {1.00f, Color4f::hex(0xB8860B)};
      p.active_grad_count = 6;
      p.active_grad_angle = 135.0f;
      // ← :934-938 容器 boxShadow：inset 0 2.5d 4.5d α.2(亮) / 0 3d 5d α.7(暗)
      //   + 0 1px 0 rgba(255,255,255,.8|.08)
      p.container_inset = true;
      p.container_inset_color = light ? Color4f::hex(0x000000, 0.20f)
                                      : Color4f::hex(0x000000, 0.70f);
      p.container_inset_dy = (light ? 2.5f : 3.0f) * d;
      p.container_inset_blur = (light ? 4.5f : 5.0f) * d;
      p.container_top_hi = light ? Color4f::hex(0xFFFFFF, 0.80f)
                                 : Color4f::hex(0xFFFFFF, 0.08f);
      // ← :1009-1020 选中块 boxShadow：多层硬 bevel（1d/2d）+ 金辉光 0 0 6v/8v
      p.active_has_bevel = true;
      p.active_bevel_dark = light ? Color4f::hex(0x8A6D00)
                                  : Color4f::hex(0x5D4037);
      p.active_bevel_dy = 2.0f * d;
      p.active_has_glow = true;
      p.active_glow_color = Color4f::hex(0xFFC107, (light ? 0.20f : 0.25f) * v);
      p.active_glow_blur = (light ? 6.0f : 8.0f) * v;
      break;
    case SkinId::Original:
      p.container_bg = light ? Color4f::hex(0xf1f5f9) : Color4f::hex(0x1e1f29);
      p.container_border = light ? Color4f::hex(0xcbd5e1) : Color4f::hex(0x475569);
      p.active_bg = primary();  // var(--primary-main)
      p.active_text = Color4f::hex(0xFFFFFF);
      // ← :1033 borderRadius calc(3px * var(--control-skin-val1))
      p.active_radius = 3.0f * g;
      // ← :954 容器 borderRadius calc(4px * var(--control-skin-val1))
      p.container_radius = 4.0f * g;
      break;
    case SkinId::ModernFlat:
      p.container_bg = light ? Color4f::hex(0xf3f4f6) : Color4f::hex(0x111827);
      p.container_border = light ? Color4f::hex(0xd1d5db) : Color4f::hex(0x4b5563);
      p.active_bg = primary();
      p.active_text = Color4f::hex(0xFFFFFF);
      // ← :1048 borderRadius calc(4px * var(--control-skin-val1))
      p.active_radius = 4.0f * g;
      break;
    case SkinId::FrostedGlass:
      p.container_bg = Color4f::hex(light ? 0x000000 : 0xFFFFFF, 0.06f);
      p.container_border = Color4f::hex(light ? 0x000000 : 0xFFFFFF, 0.12f);
      p.active_bg = with_alpha(primary(), 0.30f);
      p.active_border = Color4f::hex(light ? 0x000000 : 0xFFFFFF, 0.20f);
      p.active_text = light ? Color4f::hex(0x000000, 0.85f)
                            : Color4f::hex(0xFFFFFF);
      p.active_radius = 4;
      break;
    case SkinId::Cyberpunk:
      p.container_border = light ? Color4f::hex(0x00b0ff) : Color4f::hex(0x39ff14);
      p.container_bg = light ? Color4f::hex(0xffffff) : Color4f::hex(0x05070c);
      p.active_bg = light ? Color4f::hex(0x00b0ff) : Color4f::hex(0x39ff14);
      p.active_text = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x000000);
      p.active_radius = 0;
      // ← :1081-1082 选中块 boxShadow：0 0 calc(8px * var(--depth-factor)) 霓虹色
      p.active_has_glow = true;
      p.active_glow_color = light ? Color4f::hex(0x00B0FF) : Color4f::hex(0x39FF14);
      p.active_glow_blur = 8.0f * d;
      break;
    case SkinId::Monochrome:
      p.container_border = light ? Color4f::hex(0x000000) : Color4f::hex(0xffffff);
      p.container_bg = light ? Color4f::hex(0xffffff) : Color4f::hex(0x000000);
      p.active_bg = light ? Color4f::hex(0x000000) : Color4f::hex(0xffffff);
      p.active_text = light ? Color4f::hex(0xffffff) : Color4f::hex(0x000000);
      // src: button-styles.ts:1094 borderRadius: '0px'
      p.active_radius = 0.0f;
      p.container_radius = 0.0f;
      break;
  }
  return p;
}

// ---------- Input（← get3DInputStyle 骨架：input bg/border） ----------
InputPaint ThemeService::input_paint() const {
  const bool light = is_light();
  // 语义化因子（← Mini 四变量注入 + layout.scss !important 覆盖）
  const float d = depth();       // --depth-factor
  const float v = vibrancy();    // --vibrancy-factor ≡ --control-skin-val2
  const float g = val1();        // --control-skin-val1
  InputPaint p{};
  p.bg = vars_.theme_input_bg;
  p.border = vars_.theme_input_border;
  p.border_focused = with_alpha(primary(), 1.0f);
  p.radius = 4.0f;
  p.border_w = 1.0f;
  p.border_w_focused = 1.0f;
  p.text = text_primary();
  p.placeholder = with_alpha(p.text, p.text.a * 0.5f);

  switch (skin_) {
    case SkinId::Retro3d:
      // :739-774：底 rgba(255,253,231,.8)/rgba(26,22,14,.4)、描边 2d、
      //   inset 0 3d 5d（暗）/ 0 4d 6d、focus 金 + 0 0 8v 金发光
      p.bg = light ? Color4f::hex(0xFFFDE7, 0.8f)
                   : Color4f::hex(0x1A160E, 0.4f);
      p.border = light ? Color4f::hex(0xD4AF37, 0.35f)
                       : Color4f::hex(0xD4AF37, 0.25f);
      p.border_w = 2.0f * d;
      p.border_w_focused = 2.0f * d;
      p.border_focused = light ? Color4f::hex(0xB8860B) : Color4f::hex(0xFFA000);
      p.has_inset = true;
      p.inset_color = light ? Color4f::hex(0x000000, 0.20f)
                            : Color4f::hex(0x000000, 0.60f);
      p.inset_dy = (light ? 3.0f : 4.0f) * d;
      p.inset_blur = (light ? 5.0f : 6.0f) * d;
      p.inset_top_hi = light ? Color4f::hex(0xFFFFFF, 0.80f)
                             : Color4f::hex(0xFFFFFF, 0.08f);
      p.focus_glow = true;
      p.focus_glow_color = Color4f::hex(0xFFC107, 0.35f * v);
      p.focus_glow_blur = 8.0f * v;
      p.text = light ? Color4f::hex(0x2C1F03) : Color4f::hex(0xFFFDE7);
      p.placeholder = with_alpha(p.text, 0.5f);
      break;

    case SkinId::Original:
      // :779-806：白底/暗 #1e1f29、1px #cbd5e1/#475569、radius 4*val1、
      //   focus --primary-main 1.5px
      p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x1E1F29);
      p.text = light ? Color4f::hex(0x1E293B) : Color4f::hex(0xF1F5F9);
      p.placeholder = with_alpha(p.text, 0.5f);
      p.border = light ? Color4f::hex(0xCBD5E1) : Color4f::hex(0x475569);
      p.border_w = 1.0f;
      p.border_focused = primary();
      p.border_w_focused = 1.5f;
      p.radius = 4.0f * g;   // ← :787 borderRadius calc(4px * var(--control-skin-val1))
      break;

    case SkinId::ModernFlat:
      // :808-837：白底/暗 #111827、radius 6*val1、1px #d1d5db/#4b5563、
      //   inset 0 v 3v rgba(0,0,0, .06v)、focus = 主色 1px
      p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x111827);
      p.border = light ? Color4f::hex(0xD1D5DB) : Color4f::hex(0x4B5563);
      p.border_focused = primary();
      p.radius = 6.0f * g;   // ← :811 borderRadius calc(6px * var(--control-skin-val1))
      p.has_inset = true;
      p.inset_color = Color4f::hex(0x000000, 0.06f * v);
      p.inset_dy = 1.0f * v;
      p.inset_blur = 3.0f * v;
      break;

    case SkinId::FrostedGlass:
      // :839-866 + index.scss:186-198：半透明底随 depth/vibrancy 变化、focus 主色
      p.bg = Color4f::hex(light ? 0x000000 : 0xFFFFFF,
                          std::clamp(0.03f + 0.05f * d + 0.02f * v, 0.02f, 0.40f));
      p.border = Color4f::hex(light ? 0x000000 : 0xFFFFFF,
                              std::clamp(0.08f + 0.06f * d, 0.04f, 0.40f));
      p.border_focused = primary();
      p.radius = 8.0f;
      break;

    case SkinId::Cyberpunk:
      // :868-906：白底/暗 #030508、1px #cbd5e1/#ff0055、
      //   light focus 0 0 0 3px 粉环；dark focus 0 0 8d 粉光
      p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x030508);
      p.text = light ? Color4f::hex(0x0F172A) : Color4f::hex(0xFF0055);
      p.placeholder = with_alpha(p.text, 0.5f);
      p.border = light ? Color4f::hex(0xCBD5E1) : Color4f::hex(0xFF0055);
      p.border_focused = Color4f::hex(0xFF0055);
      if (light) {
        p.has_inset = true;
        p.inset_color = Color4f::hex(0x000000, 0.05f);
        p.inset_dy = 1.0f;
        p.inset_blur = 3.0f;
        p.focus_ring = 3.0f;
        p.focus_glow_color = Color4f::hex(0xFF0055, 0.15f);
      } else {
        p.focus_glow = true;
        p.focus_glow_color = Color4f::hex(0xFF0055, 0.35f * d);
        p.focus_glow_blur = 8.0f * d;
      }
      p.radius = 4.0f;
      break;

    case SkinId::Monochrome:
      // :908-926：黑白底、描边 1*v、focus 2*v（不乘 depth）
      p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x000000);
      p.text = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.placeholder = with_alpha(p.text, 0.5f);
      p.border = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.border_focused = p.border;
      p.border_w = 1.0f * v;
      p.border_w_focused = 2.0f * v;
      p.radius = 0.0f;  // src: button-styles.ts:908 borderRadius: '0px'
      break;
  }
  return p;
}

// ---------- 容器（.theme-panel）：6 套皮肤差异总表 ----------
// 每一条都对应 index.scss 的一行，depth=val1（圆角/阴影强度）、vibrancy=val2
// （阴影模糊/发光/脉冲速度）。调用方只按参数画，不再各自判断皮肤。
PanelPaint ThemeService::panel_paint() const {
  const bool light = is_light();
  // 语义化因子（← Mini 四变量注入 + layout.scss 的 !important 覆盖）
  //   val1 → --control-skin-val1（几何类：圆角）
  //   dep → --depth-factor（深度类：阴影位移/浮雕；original/modern-flat 被强制 0）
  //   v   → --vibrancy-factor ≡ --control-skin-val2（辉光/阴影强度/动画速度）
  const float d = depth();       // ← --depth-factor
  const float v = vibrancy();    // ← --vibrancy-factor
  const float g = val1();        // ← --control-skin-val1
  PanelPaint p{};
  p.bg = vars_.theme_panel_bg;  // 多数皮肤的底色来源
  p.radius = 8.0f;              // --border-radius 缺省 8（index.scss:76）

  switch (skin_) {
    case SkinId::Retro3d:
      // index.scss:123-132（暗）/ :145-153（亮）：135° 三停渐变 + 金色双线 +
      //   外阴影 0 8d 32d rgba(0,0,0,.3d) + 金色外发光 0 0 12v rgba(255,193,7,.2v)
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
      // index.scss:128-130 / :149-151：两层 inset 浮雕（亮/暗各一套色）
      p.has_inner_bevel = true;
      p.inner_hi = light ? Color4f::hex(0xFFFFFF, 0.65f * d)
                         : Color4f::hex(0xFFFFFF, 0.20f * d);
      p.inner_dark = light ? Color4f::hex(0x8A6D00, 0.30f * d)
                           : Color4f::hex(0x000000, 0.55f * d);
      p.inner_blur = 2.0f * d;
      break;

    case SkinId::Original:
      // index.scss:298-320：border none、极轻双层阴影、圆角 8*val1
      //   ★ val1 在本皮肤是 **Radius**（几何），不是深度；
      //     且 layout.scss:443 把 --depth-factor 强制为 0 → 本皮肤无阴影深度语义。
      p.border = PanelPaint::BorderKind::None;
      p.radius = 8.0f * g;   // ← --border-radius: calc(8px * var(--control-skin-val1))
      p.has_shadow = true;
      p.shadow_color = Color4f::hex(0x000000, 0.08f);
      p.shadow_dy = 1.0f;
      p.shadow_blur = 3.0f;
      break;

    case SkinId::ModernFlat:
      // index.scss:165-183：保留 4px double、纯色底、无毛玻璃、
      //   阴影 0 4v 16v rgba(0,0,0, .10v)（暗色基线 .45v）、圆角 8*val1
      //   ★ val1 = Roundness（几何）；val2 = Shadow（阴影强度）。
      p.border = PanelPaint::BorderKind::Double;
      p.border_color = vars_.theme_border;
      p.radius = 8.0f * g;   // ← --border-radius: calc(8px * var(--control-skin-val1))
      p.has_shadow = true;
      p.shadow_color = Color4f::hex(0x000000, (light ? 0.10f : 0.45f) * v);
      p.shadow_dy = 4.0f * v;
      p.shadow_blur = 16.0f * v;
      break;

    case SkinId::FrostedGlass:
      // index.scss:185-198：半透明白玻璃底 rgba(base, 0.02+0.1*depth)、
      //   玻璃双线 rgba(glass-border, .15)、固定 0 8 32 阴影。
      //   ★ val1 = Opacity（玻璃不透明度）→ 消费 --depth-factor 的底色 alpha；
      //     val2 = Blur → backdrop-filter: blur(16v)，D2D 无此原语（差异登记）。
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
      // index.scss:231-255：深底 #05070c（亮色 #fff）+ 霓虹双线 #ff0055 +
      //   呼吸动画 period = 2.5s / (vibrancy + 0.001)
      //   ★ val1 = Glow（辉光强度）；val2 = Speed（动画速度）。
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
      // index.scss:257-296：保留 4px double + 纯色底，但去掉全部阴影
      //   ★ val1 = Radius（几何圆角）；val2 = Border（边框宽度，见 button/card）。
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
// 出处：utils/button-styles.ts:492-731。
// cardType ∈ default / primary / upload / download。
// 这张表取代了 Win 侧原先散落在 quick_bar / settings_drawer / traffic_pane /
// 各弹窗里的「只有 retro-3d 金渐变」手写实现。
SwitchPaint ThemeService::switch_paint() const {
  const bool light = is_light();
  // 语义化因子（← Mini 四变量注入 + layout.scss !important 覆盖）
  const float d = depth();       // --depth-factor（retro-3d 的 3D 深度）
  const float v = vibrancy();    // --vibrancy-factor
  const float g = val1();        // --control-skin-val1（original 的轨道圆角）
  SwitchPaint p{};
  p.off_bg = Color4f::hex(0xD4AF37, light ? 0.25f : 0.15f);
  p.off_border = Color4f::hex(0xD4AF37, light ? 0.30f : 0.18f);
  p.off_bw = 1.0f;
  p.on_bg = Color4f::hex(0xFFC107);
  p.off_thumb = Color4f::hex(0xFFD54F);
  p.on_thumb = Color4f::hex(0xFFD54F);

  switch (skin_) {
    case SkinId::Retro3d: {
      // :15-140：28×14、track r7、off rgba(212,175,55,.25/.15)*.85 +
      //   1px rgba(.3/.18)、on = to-bottom 三停 @.85、thumb 14 radial 四停
      // ★ 轨道可见性（:103-115）：boxShadow 的 inset 暗影 + 底部 1px 高光线
      //   —— 槽底是半透明金色叠在同色卡片底上，仅靠底色对比极弱，
      //   真正的"槽"感来自这两层。缺了就只剩一个看不见的滑块悬空。
      p.track_w = 28.0f;
      p.track_h = 14.0f;
      p.track_radius = 7.0f;
      p.off_bg = Color4f::hex(0xD4AF37, (light ? 0.25f : 0.15f) * 0.85f);
      p.off_border = light ? Color4f::hex(0xB8860B, 0.55f) : Color4f::hex(0x8A6D00, 0.55f);  // src: style-helpers.tsx:261
      p.off_bw = 1.0f;
      // inset 0 calc(2d|3d) calc(4d|5d) rgba(0,0,0,.25|.7)
      p.off_inset = true;
      p.off_inset_color = Color4f::hex(0x000000, light ? 0.25f : 0.70f);
      p.off_inset_dy = (light ? 2.0f : 3.0f) * d;
      p.off_inset_blur = (light ? 4.0f : 5.0f) * d;
      // 0 1px 0 rgba(255,255,255,.8|.08)
      p.off_bottom_hi = true;
      p.off_bottom_hi_color = Color4f::hex(0xFFFFFF, light ? 0.80f : 0.08f);
      p.off_bottom_hi_w = 1.0f;
      p.on_grad = true;
      p.on_grad_stops[0] = {0.0f, Color4f::hex(0xFFE082, 0.85f)};
      p.on_grad_stops[1] = {0.5f, Color4f::hex(0xFFC107, 0.85f)};
      p.on_grad_stops[2] = {1.0f, Color4f::hex(0xB8860B, 0.85f)};
      p.on_grad_n = 3;
      p.on_bw = 0.0f;
      // :60-79 on 态轨道 boxShadow：inset 0 2d|3d 4d|5d rgba(0,0,0,.3|.75)
      //   + 0 1px 0 rgba(255,255,255,.8|.08)
      //   + inset 0 0 8v rgba(255,193,7,.4) + 0 0 8v rgba(255,193,7,.4v)
      p.on_inset = true;
      p.on_inset_color = Color4f::hex(0x000000, light ? 0.30f : 0.75f);
      p.on_inset_dy = (light ? 2.0f : 3.0f) * d;
      p.on_inset_blur = (light ? 4.0f : 5.0f) * d;
      p.on_bottom_hi = true;
      p.on_bottom_hi_color = Color4f::hex(0xFFFFFF, light ? 0.80f : 0.08f);
      p.on_bottom_hi_w = 1.0f;
      p.thumb_d = 14.0f;
      p.thumb_radius = 7.0f;
      p.off_thumb_grad = true;
      // 开/关两态小圆球颜色一致，对齐主题设置滑块小圆球设计（完全不透明金球）
      p.off_thumb_stops[0] = {0.00f, Color4f::hex(light ? 0xFFFDE7 : 0xFFF9C4)};
      p.off_thumb_stops[1] = {0.45f, Color4f::hex(light ? 0xFFD54F : 0xFFC107)};
      p.off_thumb_stops[2] = {0.80f, Color4f::hex(0xFFA000)};
      p.off_thumb_stops[3] = {1.00f, Color4f::hex(light ? 0xB8860B : 0x8A6D00)};
      p.off_thumb_n = 4;
      p.off_thumb_border = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00);
      p.off_thumb_bw = 1.0f;
      // :88-98 旋钮内浮雕：inset 0 1d 0 rgba(255,255,255,.7|.5)
      //   + inset 0 -1d|-1.5d 0 rgba(0,0,0,.15|.45)
      p.off_thumb_inset_hi = true;
      p.off_thumb_inset_hi_color = Color4f::hex(0xFFFFFF, light ? 0.70f : 0.50f);
      p.off_thumb_inset_hi_dy = 1.0f * d;
      p.off_thumb_inset_dark = true;
      p.off_thumb_inset_dark_color =
          Color4f::hex(0x000000, light ? 0.15f : 0.45f);
      p.off_thumb_inset_dark_dy = (light ? -1.0f : -1.5f) * d;
      p.on_thumb_grad = true;
      p.on_thumb_stops[0] = {0.00f, Color4f::hex(light ? 0xFFFDE7 : 0xFFF9C4)};
      p.on_thumb_stops[1] = {0.45f, Color4f::hex(light ? 0xFFD54F : 0xFFC107)};
      p.on_thumb_stops[2] = {0.80f, Color4f::hex(0xFFA000)};
      p.on_thumb_stops[3] = {1.00f, Color4f::hex(light ? 0xB8860B : 0x8A6D00)};
      p.on_thumb_n = 4;
      p.on_thumb_border = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00);
      p.on_thumb_bw = 1.0f;
      // ★ on 态旋钮同样带内浮雕（← base-switch.tsx:97-98）
      //   `.Mui-checked .MuiSwitch-thumb` 只覆盖 background 与 border，
      //   boxShadow 继承基础 `.MuiSwitch-thumb` 的 inset 顶高光 + 底暗线。
      p.on_thumb_inset_hi = true;
      p.on_thumb_inset_hi_color = Color4f::hex(0xFFFFFF, light ? 0.70f : 0.50f);
      p.on_thumb_inset_hi_dy = 1.0f * d;
      p.on_thumb_inset_dark = true;
      p.on_thumb_inset_dark_color =
          Color4f::hex(0x000000, light ? 0.15f : 0.45f);
      p.on_thumb_inset_dark_dy = (light ? -1.0f : -1.5f) * d;
      p.thumb_shadow = true;
      p.thumb_shadow_color = light ? Color4f::hex(0x000000, 0.20f)
                                   : Color4f::hex(0x000000, 0.50f);
      p.thumb_shadow_dy = (light ? 2.5f : 3.0f) * d;
      p.thumb_shadow_blur = (light ? 5.0f : 6.0f) * d;
      p.on_glow = true;
      p.on_glow_color = Color4f::hex(0xFFC107, 0.40f * v);
      p.on_glow_blur = 8.0f * v;
      // ★ 旋钮悬停态（← base-switch.tsx:114-128 `&:hover .MuiSwitch-thumb`）
      //   transform scale(1.2) + filter brightness(1.15)
      //   boxShadow 覆盖：0 4d|5d 8d|10d rgba(0,0,0,.3|.6)
      //                + inset 0 1d|1.2d 0 rgba(255,255,255,.8|.6)
      //                + inset 0 -1d|-1.5d 0 rgba(0,0,0,.1|.4)
      //                + 0 0 10v rgba(255,193,7,.35v)
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.2f;
      p.thumb_hover_brightness = 1.15f;
      p.thumb_hover_shadow = true;
      p.thumb_hover_shadow_color = light ? Color4f::hex(0x000000, 0.30f)
                                         : Color4f::hex(0x000000, 0.60f);
      p.thumb_hover_shadow_dy = (light ? 4.0f : 5.0f) * d;
      p.thumb_hover_shadow_blur = (light ? 8.0f : 10.0f) * d;
      p.thumb_hover_inset_hi = true;
      p.thumb_hover_inset_hi_color =
          Color4f::hex(0xFFFFFF, light ? 0.80f : 0.60f);
      p.thumb_hover_inset_hi_dy = (light ? 1.0f : 1.2f) * d;
      p.thumb_hover_inset_dark = true;
      p.thumb_hover_inset_dark_color =
          Color4f::hex(0x000000, light ? 0.10f : 0.40f);
      p.thumb_hover_inset_dark_dy = (light ? -1.0f : -1.5f) * d;
      p.thumb_hover_glow = true;
      p.thumb_hover_glow_color = Color4f::hex(0xFFC107, 0.35f * v);
      p.on_thumb_hover_glow_color = Color4f::hex(0xFFC107, 0.35f * v);
      p.thumb_hover_glow_blur = 10.0f * v;
      break;
    }
    case SkinId::Original:
      // :145-198：28×14、track r7*val1、off #e2e8f0/#30363d 无边、
      //   thumb 14 r7*val1 #cbd5e1/#475569 无边；on track 主色、on thumb 白
      p.track_w = 28.0f;
      p.track_h = 14.0f;
      p.track_radius = 7.0f * g;   // ← base-switch.tsx:188 calc(7px * var(--control-skin-val1))
      p.off_bg = light ? Color4f::hex(0xE2E8F0) : Color4f::hex(0x30363D);
      p.off_bw = 0.0f;
      p.on_bg = primary();
      p.on_bw = 0.0f;
      p.thumb_radius = 7.0f * g;   // ← base-switch.tsx:194 calc(7px * var(--control-skin-val1))
      p.off_thumb = Color4f::hex(0xFFFFFF);
      p.off_thumb_bw = 0.0f;
      p.on_thumb = Color4f::hex(0xFFFFFF);
      p.on_thumb_bw = 0.0f;
      // ★ original 皮肤**无** hover 规则（← base-switch.tsx:133-204 内无
      //   '&:hover .MuiSwitch-thumb'），悬停时保持常态。
      break;

    case SkinId::ModernFlat:
      // :200-278：28×14、off #e5e7eb/#374151、on 主色、thumb 白无边
      //   track boxShadow 'none'、border 'none'（:264-271）
      p.track_w = 28.0f;
      p.track_h = 14.0f;
      p.track_radius = 7.0f;
      p.off_bg = light ? Color4f::hex(0xE5E7EB) : Color4f::hex(0x374151);
      p.off_bw = 0.0f;
      p.on_bg = primary();
      p.on_bw = 0.0f;
      p.off_thumb = Color4f::hex(0xFFFFFF);
      p.off_thumb_bw = 0.0f;
      p.on_thumb = Color4f::hex(0xFFFFFF);
      p.on_thumb_bw = 0.0f;
      // thumb boxShadow '0 1px 2px rgba(0,0,0,0.15)'（:262）
      p.thumb_shadow = true;
      p.thumb_shadow_color = Color4f::hex(0x000000, 0.15f);
      p.thumb_shadow_dy = 1.0f;
      p.thumb_shadow_blur = 2.0f;
      // ★ 旋钮悬停态（← base-switch.tsx:361-363 `&:hover .MuiSwitch-thumb`）
      //   transform: scale(1.1) —— 仅缩放，不改阴影。
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.1f;
      break;

    case SkinId::FrostedGlass:
      // :280-372：28×14、thumb 12 + margin 1、off rgba(0,0,0,.06)/rgba(255,255,255,.08)
      //   + 1px、on 主色 .5/.35 + 1px、thumb 白 .8/.3 + 1px 主色 .3/白 .2
      p.track_w = 28.0f;
      p.track_h = 14.0f;
      p.track_radius = 7.0f;
      p.off_bg = light ? Color4f::hex(0x000000, 0.06f)
                       : Color4f::hex(0xFFFFFF, 0.08f);
      p.off_border = light ? Color4f::hex(0x000000, 0.08f)
                           : Color4f::hex(0xFFFFFF, 0.10f);
      p.off_bw = 1.0f;
      p.on_bg = with_alpha(primary(), light ? 0.5f : 0.35f);
      p.on_border = light ? with_alpha(primary(), 0.2f)
                          : Color4f::hex(0xFFFFFF, 0.15f);
      p.on_bw = 1.0f;
      p.thumb_d = 12.0f;
      p.thumb_margin = 1.0f;
      p.thumb_radius = 6.0f;
      p.off_thumb = light ? Color4f::hex(0xFFFFFF, 0.95f)
                          : Color4f::hex(0xFFFFFF, 0.9f);
      p.off_thumb_border = light ? with_alpha(primary(), 0.4f)
                                 : Color4f::hex(0xFFFFFF, 0.3f);
      p.off_thumb_bw = 1.0f;
      p.on_thumb = light ? Color4f::hex(0xFFFFFF, 0.95f)
                         : Color4f::hex(0xFFFFFF, 0.9f);
      p.on_thumb_border = light ? with_alpha(primary(), 0.4f)
                                : Color4f::hex(0xFFFFFF, 0.3f);
      p.on_thumb_bw = 1.0f;
      if (light) {
        // :350 thumb boxShadow '0 1px 2px rgba(0,0,0,0.1)'（暗态 none）
        p.thumb_shadow = true;
        p.thumb_shadow_color = Color4f::hex(0x000000, 0.10f);
        p.thumb_shadow_dy = 1.0f;
        p.thumb_shadow_blur = 2.0f;
      }
      // ★ frosted-glass 皮肤**无** hover 规则（← base-switch.tsx:280-372 内无
      //   '&:hover .MuiSwitch-thumb'），悬停时保持常态。
      break;

    case SkinId::Cyberpunk:
      // :374-452：18×10、track r0、off #f1f5f9/#05070c + 1px #ff0055、
      //   thumb 12 + margin 1 方角 #ff0055 + 0 0 4 #ff0055；
      //   on track #e0f7fa/#0a0e17 + 1px #00b0ff/#00f0ff、on thumb 同色 + 0 0 6
      p.track_w = 18.0f;
      p.track_h = 10.0f;
      p.track_radius = 0.0f;
      p.off_bg = light ? Color4f::hex(0xF1F5F9) : Color4f::hex(0x05070C);
      p.off_border = Color4f::hex(0xFF0055);
      p.off_bw = 1.0f;
      p.on_bg = light ? Color4f::hex(0xE0F7FA) : Color4f::hex(0x0A0E17);
      p.on_border = light ? Color4f::hex(0x00B0FF) : Color4f::hex(0x00F0FF);
      p.on_bw = 1.0f;
      // :427-434 thumb 12×12 margin 1 方角（sizeSmall 强制 12/1）
      p.thumb_d = 12.0f;
      p.thumb_margin = 1.0f;
      p.thumb_radius = 0.0f;
      p.off_thumb = Color4f::hex(0xFF0055);
      p.off_thumb_bw = 0.0f;
      p.thumb_glow = true;
      p.thumb_glow_color = Color4f::hex(0xFF0055, 0.85f);
      p.thumb_glow_blur = 4.0f;
      p.on_thumb = p.on_border;
      p.on_thumb_bw = 0.0f;
      // ★ 旋钮悬停态（← base-switch.tsx:442-444 `&:hover .MuiSwitch-thumb`）
      //   boxShadow: '0 0 10px currentColor' —— currentColor = 旋钮自身 color，
      //   off 态 #ff0055 / on 态 #00b0ff|#00f0ff（:418-419, :430-432）。
      p.thumb_hover = true;
      p.thumb_hover_glow = true;
      p.thumb_hover_glow_color = Color4f::hex(0xFF0055, 0.85f);
      p.on_thumb_hover_glow_color =
          Color4f::hex(light ? 0x00B0FF : 0x00F0FF, 0.85f);
      p.thumb_hover_glow_blur = 10.0f;
      break;
    case SkinId::Monochrome:
      // :454-545：28×14、track r14、off #e5e7eb/#374151 无边、
      //   thumb 12 + margin 1 r6 #000/#fff + 0 1 2 .15；
      //   on track #000/#fff、on thumb #fff/#000
      p.track_w = 28.0f;
      p.track_h = 14.0f;
      p.track_radius = 14.0f;
      p.off_bg = light ? Color4f::hex(0xE5E7EB) : Color4f::hex(0x374151);
      p.off_bw = 0.0f;
      p.on_bg = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.on_bw = 0.0f;
      p.thumb_d = 12.0f;
      p.thumb_margin = 1.0f;
      p.thumb_radius = 6.0f;
      p.off_thumb = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.off_thumb_bw = 0.0f;
      p.on_thumb = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x000000);
      p.on_thumb_bw = 0.0f;
      p.thumb_shadow = true;
      p.thumb_shadow_color = Color4f::hex(0x000000, 0.15f);
      p.thumb_shadow_dy = 1.0f;
      p.thumb_shadow_blur = 2.0f;
      // ★ 旋钮悬停态（← base-switch.tsx:543-545 `&:hover .MuiSwitch-thumb`）
      //   transform: scale(1.05) —— 仅缩放，不改阴影。
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.05f;
      break;
  }
  return p;
}

SliderPaint ThemeService::slider_paint() const {
  const bool light = is_light();
  // 语义化因子（← Mini 四变量注入 + layout.scss !important 覆盖）
  const float d = depth();       // --depth-factor（retro-3d 旋钮阴影深度）
  const float v = vibrancy();    // --vibrancy-factor（retro-3d 金色辉光）
  const float g = val1();        // --control-skin-val1（modern-flat 圆角）
  SliderPaint p{};
  p.rail = Color4f::hex(0x000000, 0.20f);
  p.track = primary();
  p.thumb = Color4f::hex(0xFFFFFF);

  switch (skin_) {
    case SkinId::Retro3d: {
      // :251-330：rail h5 rgba(212,175,55,.25/.15)*.85、1px #B8860B/#8A6D00、
      //   radius 2.5、inset 0 3 5 rgba(0,0,0,.3/.75) + 0 1 0 白；
      //   track to-bottom 三停；thumb 14 radial 四停 + 1px #8A6D00/#634E00
      //   + 0 3d 6d 黑影 + 0 0 8v 金 glow
      p.thickness = 5.0f;
      p.thumb_w = p.thumb_h = 14.0f;
      p.thumb_radius = 7.0f;
      p.rail = Color4f::hex(0xD4AF37, (light ? 0.25f : 0.15f) * 0.85f);
      p.rail_border = light ? Color4f::hex(0xB8860B) : Color4f::hex(0x8A6D00);
      p.rail_bw = 1.0f;
      p.rail_radius = 2.5f;
      p.rail_inset = true;
      p.rail_inset_color = light ? Color4f::hex(0x000000, 0.30f)
                                 : Color4f::hex(0x000000, 0.75f);
      p.rail_inset_dy = 3.0f * d;    // src: theme-settings-card.tsx:241
      p.rail_inset_blur = 5.0f * d;  // src: theme-settings-card.tsx:241
      p.rail_top_hi = light ? Color4f::hex(0xFFFFFF, 0.80f)
                            : Color4f::hex(0xFFFFFF, 0.08f);
      p.track_grad = true;
      p.track_grad_stops[0] = {0.0f, Color4f::hex(0xFFE082)};
      p.track_grad_stops[1] = {0.5f, Color4f::hex(0xFFC107)};
      p.track_grad_stops[2] = {1.0f, Color4f::hex(0xB8860B)};
      p.track_grad_n = 3;
      p.track_radius = 2.5f;
      p.thumb_grad = true;
      p.thumb_stops[0] = {0.00f, Color4f::hex(light ? 0xFFFDE7 : 0xFFF9C4)};
      p.thumb_stops[1] = {0.45f, Color4f::hex(light ? 0xFFD54F : 0xFFC107)};
      p.thumb_stops[2] = {0.80f, Color4f::hex(0xFFA000)};
      p.thumb_stops[3] = {1.00f, Color4f::hex(light ? 0xB8860B : 0x8A6D00)};
      p.thumb_grad_n = 4;
      p.thumb_border = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00);
      p.thumb_bw = 1.0f;
      p.thumb_shadow = true;
      p.thumb_shadow_color = light ? Color4f::hex(0x000000, 0.25f)
                                   : Color4f::hex(0x000000, 0.60f);
      p.thumb_shadow_dy = (light ? 3.0f : 4.0f) * d;
      p.thumb_shadow_blur = (light ? 6.0f : 8.0f) * d;
      p.thumb_glow = true;
      p.thumb_glow_color =
          Color4f::hex(0xFFC107, (light ? 0.25f : 0.30f) * v);
      p.thumb_glow_blur = (light ? 8.0f : 10.0f) * v;
      // ★ thumb 内凹顶高光（← style-helpers.tsx:285/288）
      //   `inset 0 calc(1px * var(--depth-factor)) 0 rgba(255,255,255,.8|.4)`
      p.thumb_inset_hi = true;
      p.thumb_inset_hi_color = light ? Color4f::hex(0xFFFFFF, 0.80f)
                                     : Color4f::hex(0xFFFFFF, 0.40f);
      p.thumb_inset_hi_dy = 1.0f * d;
      // ★ thumb 按下内凹（← style-helpers.tsx:306-307 `:active`）
      //   light: inset 1d 1d 2d rgba(0,0,0,.15) / dark: inset 1.5d 1.5d 2.5d rgba(0,0,0,.5)
      p.thumb_active_inset = true;
      p.thumb_active_inset_color =
          Color4f::hex(0x000000, light ? 0.15f : 0.50f);
      p.thumb_active_inset_dx = (light ? 1.0f : 1.5f) * d;
      p.thumb_active_inset_dy = (light ? 1.0f : 1.5f) * d;
      p.thumb_active_inset_blur = (light ? 2.0f : 2.5f) * d;
      // ★ thumb 悬停/聚焦态（← style-helpers.tsx:292-305
      //   `'&:hover, &.Mui-focusVisible'`）
      //   transform scale(1.2) + filter brightness(1.15)
      //   boxShadow: 0 5d|6d 10d|12d rgba(0,0,0,.35|.7)
      //            + inset 0 1d 0 rgba(255,255,255,.9|.5)
      //            + 0 0 12v|14v rgba(255,193,7,.35v|.4v)
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.2f;
      p.thumb_hover_brightness = 1.15f;
      p.thumb_hover_shadow = true;
      p.thumb_hover_shadow_color = light ? Color4f::hex(0x000000, 0.35f)
                                         : Color4f::hex(0x000000, 0.70f);
      p.thumb_hover_shadow_dy = (light ? 5.0f : 6.0f) * d;
      p.thumb_hover_shadow_blur = (light ? 10.0f : 12.0f) * d;
      p.thumb_hover_inset_hi = true;
      p.thumb_hover_inset_hi_color =
          Color4f::hex(0xFFFFFF, light ? 0.90f : 0.50f);
      p.thumb_hover_inset_hi_dy = 1.0f * d;
      p.thumb_hover_glow = true;
      p.thumb_hover_glow_color =
          Color4f::hex(0xFFC107, (light ? 0.35f : 0.40f) * v);
      p.thumb_hover_glow_blur = (light ? 12.0f : 14.0f) * v;
      break;
    }
    case SkinId::Original:
      // :333-360：rail h4 #cbd5e1/#30363d、track h4 --primary-main、thumb 12
      //   白底 2px 主色边 + 0 1 2 rgba(0,0,0,.15)
      p.thickness = 4.0f;
      p.thumb_w = p.thumb_h = 12.0f;
      p.thumb_radius = 6.0f;
      p.rail = light ? Color4f::hex(0xCBD5E1) : Color4f::hex(0x30363D);
      p.track = primary();
      p.thumb = Color4f::hex(0xFFFFFF);
      p.thumb_border = primary();
      p.thumb_bw = 2.0f;
      p.thumb_shadow = true;
      p.thumb_shadow_color = Color4f::hex(0x000000, 0.15f);
      p.thumb_shadow_dy = 1.0f;
      p.thumb_shadow_blur = 2.0f;
      // ★ thumb 悬停态（← style-helpers.tsx:334-336 `&:hover, &.Mui-focusVisible`）
      //   transform: scale(1.15) —— 仅缩放。
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.15f;
      break;

    case SkinId::ModernFlat:
      // :363-392：rail h4 #e5e7eb/#374151 radius 2*val1、track 主色、
      //   thumb 12 白底 radius 6*val1 + 2px 主色边 + 0 1 2 rgba(0,0,0,.15)
      p.thickness = 4.0f;
      p.thumb_w = p.thumb_h = 12.0f;
      p.thumb_radius = 6.0f * g;   // ← :380 borderRadius calc(6px * var(--control-skin-val1))
      p.rail = light ? Color4f::hex(0xE5E7EB) : Color4f::hex(0x374151);
      p.rail_radius = 2.0f * g;   // ← :368 calc(2px * var(--control-skin-val1))
      p.track = primary();
      p.track_radius = 2.0f * g;  // ← :375 calc(2px * var(--control-skin-val1))
      p.thumb = Color4f::hex(0xFFFFFF);
      p.thumb_border = primary();
      p.thumb_bw = 2.0f;
      p.thumb_shadow = true;
      p.thumb_shadow_color = Color4f::hex(0x000000, 0.15f);
      p.thumb_shadow_dy = 1.0f;
      p.thumb_shadow_blur = 2.0f;
      // ★ thumb 悬停态（← style-helpers.tsx:363-365 `&:hover, &.Mui-focusVisible`）
      //   transform: scale(1.15) —— 仅缩放。
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.15f;
      break;

    case SkinId::FrostedGlass:
      // :395-437：rail h4 rgba(0,0,0,.06)/rgba(255,255,255,.08) + 1px 边、
      //   track 主色 .6/.5、thumb 12 半透明白 + 1px 边 + light 0 1 3 .12
      p.thickness = 4.0f;
      p.thumb_w = p.thumb_h = 12.0f;
      p.thumb_radius = 6.0f;
      p.rail = light ? Color4f::hex(0x000000, 0.06f)
                     : Color4f::hex(0xFFFFFF, 0.08f);
      p.rail_border = light ? Color4f::hex(0x000000, 0.08f)
                            : Color4f::hex(0xFFFFFF, 0.10f);
      p.rail_bw = 1.0f;
      p.track = with_alpha(primary(), light ? 0.6f : 0.5f);
      p.thumb = light ? Color4f::hex(0xFFFFFF, 0.9f)
                      : Color4f::hex(0xFFFFFF, 0.8f);
      p.thumb_border = light ? with_alpha(primary(), 0.3f)
                             : Color4f::hex(0xFFFFFF, 0.3f);
      p.thumb_bw = 1.0f;
      if (light) {
        p.thumb_shadow = true;
        p.thumb_shadow_color = Color4f::hex(0x000000, 0.12f);
        p.thumb_shadow_dy = 1.0f;
        p.thumb_shadow_blur = 3.0f;
      }
      // ★ thumb 悬停态（← style-helpers.tsx:395-404 `&:hover, &.Mui-focusVisible`）
      //   transform scale(1.15)
      //   bgcolor rgba(255,255,255,1.0)      （常态 rgba(255,255,255,.9|.8)）
      //   boxShadow 亮态 '0 2px 5px rgba(0,0,0,0.18)'
      //             暗态 '0 0 4px rgba(255,255,255,0.4)'
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.15f;
      p.thumb_hover_fill = true;
      p.thumb_hover_fill_color = Color4f::hex(0xFFFFFF, 1.0f);
      if (light) {
        p.thumb_hover_shadow = true;
        p.thumb_hover_shadow_color = Color4f::hex(0x000000, 0.18f);
        p.thumb_hover_shadow_dy = 2.0f;
        p.thumb_hover_shadow_blur = 5.0f;
      } else {
        p.thumb_hover_glow = true;
        p.thumb_hover_glow_color = Color4f::hex(0xFFFFFF, 0.40f);
        p.thumb_hover_glow_blur = 4.0f;
      }
      break;

    case SkinId::Cyberpunk:
      // :452-480：rail h2 白/#05070c + 1px #ff0055、track h2 #4caf50/#39ff14、
      //   thumb 8×12 方角同色 + 0 0 5px 同色 glow
      p.thickness = 2.0f;
      p.thumb_w = 8.0f;
      p.thumb_h = 12.0f;
      p.thumb_radius = 0.0f;
      p.rail = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x05070C);
      p.rail_border = Color4f::hex(0xFF0055);
      p.rail_bw = 1.0f;
      p.rail_radius = 0.0f;
      p.track = light ? Color4f::hex(0x4CAF50) : Color4f::hex(0x39FF14);
      p.track_radius = 0.0f;
      p.thumb = p.track;
      p.thumb_glow = true;
      p.thumb_glow_color = with_alpha(p.track, 0.75f);
      p.thumb_glow_blur = 5.0f * d;  // src: theme-settings-card.tsx:303
      // ★ thumb 悬停态（← style-helpers.tsx:427-430 `&:hover, &.Mui-focusVisible`）
      //   transform scale(1.15) + boxShadow '0 0 8px <主色>'（常态 0 0 5px）
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.15f;
      p.thumb_hover_glow = true;
      p.thumb_hover_glow_color = with_alpha(p.track, 0.75f);
      p.thumb_hover_glow_blur = 8.0f * d;  // src: theme-settings-card.tsx:303
      break;

    case SkinId::Monochrome:
      // :482-508：rail h2 白/黑 + 1px 黑白、track h2 黑白、thumb 10×10 方角
      p.thickness = 2.0f;
      p.thumb_w = p.thumb_h = 10.0f;
      p.thumb_radius = 0.0f;
      p.rail = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x000000);
      p.rail_border = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.rail_bw = 1.0f * v;  // src: index.scss:270 --vibrancy-factor
      p.rail_radius = 0.0f;
      p.track = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.track_radius = 0.0f;
      p.thumb = p.track;
      break;
  }
  return p;
}

CardPaint ThemeService::card_paint(const char* card_type) const {
  const bool light = is_light();
  // 语义化因子（← Mini 四变量注入 + layout.scss !important 覆盖）
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
      p.border_w = 0.0f;    // border: none（borderColor 仅继承用）
      p.has_shadow = true;
      p.shadow_color = bevel;  // CSS 是 0 1/2/3px 硬边 bevel，此处近似为贴边实影
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
      //   ★ val1 = Radius（几何）；val2 = Accent（主色相）。
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
      //   ★ val1 = Roundness（几何）；val2 = Shadow（阴影强度）。
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
      // :716-728：radius 0px、边框 1px*val2 纯黑白、无阴影
      p.radius = 0.0f;
      p.border_w = 1.0f * v;
      p.border = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x000000);
      p.text_color = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.has_shadow = false;
      break;
  }
  return p;
}

// 霓虹呼吸相位（← CSS `animation: … ease-in-out infinite alternate`）：
// 0 表示最暗、1 表示最亮，往返。非 cyberpunk 恒 0。
float ThemeService::pulse_phase(float time_s) const {
  if (skin_ != SkinId::Cyberpunk) return 0.0f;
  const float period = 2.5f / (vibrancy_ + 0.001f);
  if (period <= 0.0001f) return 0.0f;
  float t = std::fmod(time_s, period) / period;
  if (t < 0.0f) t += 1.0f;
  return t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f;
}

}  // namespace clashwin::ui::theme
