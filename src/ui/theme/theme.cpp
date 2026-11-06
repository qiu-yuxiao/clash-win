// ThemeService 实现（T12）：解析链 + 六皮肤控件族参数表。
// 差异标注：CSS 渐变/多层 boxShadow/backdropFilter → C++ 离散色近似。
#include "theme.h"

#include <algorithm>
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

ThemeService& ThemeService::instance() {
  static ThemeService svc;
  return svc;
}

void ThemeService::init(SkinId skin, ThemeMode mode,
                        const std::string& primary_override) {
  skin_ = skin;
  mode_ = mode;
  primary_override_ = primary_override;
  rebuild();
}

void ThemeService::set_factors(float depth, float vibrancy) {
  depth_ = depth;
  vibrancy_ = vibrancy;
}

void ThemeService::rebuild() {
  palette_ = base_palette(mode_);
  // ← use-custom-theme.ts:170-181 主色解析链：
  // setting.primary_color || 系统 AccentColor || 色板缺省；original 强制 hsl
  if (skin_ == SkinId::Original) {
    palette_.primary_color = original_primary(1.0f);  // val2 缺省 1.0
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
ButtonPaint ThemeService::button_paint(const char* color_type) const {
  bool light = is_light();
  bool is_primary = std::strcmp(color_type, "primary") == 0;
  bool is_error = std::strcmp(color_type, "error") == 0;
  ButtonPaint p{};
  p.text_disabled = light ? Color4f::hex(0x9e9e9e) : Color4f::hex(0x6e6e75);
  p.fill_disabled = light ? Color4f::hex(0xe0e0e0) : Color4f::hex(0x32323a);

  switch (skin_) {
    case SkinId::Retro3d: {
      // 金色 3D（← :39-140）：135° 多停渐变 + 浮雕 + 外发光 + 文字阴影 + 位移
      p.use_grad = true;
      p.border_w = 2;      // calc(2px * depth)
      p.radius = 6;
      p.weight = 700;      // bold
      p.translate_hover = -2.0f;   // transform: translateY(-2px)
      p.translate_active = 3.0f;   // transform: translateY(3px)
      if (is_primary) {
        p.text = Color4f::hex(0x2C1F03);
        p.border = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00);
        p.bevel_dark = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x5D4037);
        p.bevel_hi = light ? Color4f::hex(0xFFFFFF, 0.8f) : Color4f::hex(0xFFFFFF, 0.08f);
        p.glow = Color4f::hex(0xFFC107, 0.2f);   // 0.2*vibrancy
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
        p.glow = Color4f::hex(0xD50000, 0.2f);
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
        p.glow = Color4f::hex(0xFFC107, 0.2f);
        p.text_shadow = Color4f::hex(0xFFFFFF, 0.45f); p.text_shadow_dy = 1.0f;
        if (light) {
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
      p.radius = 4;
      p.weight = 500;
      p.translate_active = 1.0f;   // transform: translateY(1px)
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
      p.radius = 6;
      p.weight = 700;
      p.translate_active = 1.0f;   // transform: translateY(1px)
      break;
    }
    case SkinId::FrostedGlass: {
      // ← :258-315：半透明 + backdropFilter（D2D 无此滤镜，保留 alpha 色值）
      p.fill = is_primary ? with_alpha(primary(), 0.25f)
                       : (light ? Color4f::hex(0x000000, 0.06f)
                                : Color4f::hex(0xFFFFFF, 0.08f));
      p.fill_hover = is_primary ? with_alpha(primary(), 0.4f)
                             : (light ? Color4f::hex(0x000000, 0.12f)
                                      : Color4f::hex(0xFFFFFF, 0.15f));
      p.fill_pressed = p.fill_hover;
      p.text = text_primary();
      p.border = light ? Color4f::hex(0x000000, 0.12f)
                       : Color4f::hex(0xFFFFFF, 0.12f);
      p.radius = 8;
      p.weight = 400;
      p.translate_hover = -1.0f;   // translateY(-1px)
      p.translate_active = 1.0f;   // translateY(1px)
      break;
    }
    case SkinId::Cyberpunk: {
      // ← :318-420：霓虹描边
      Color4f neon = is_primary ? Color4f::hex(0x00f0ff)
                             : (is_error ? Color4f::hex(0xff0055)
                                      : Color4f::hex(0x39ff14));
      p.fill = light ? Color4f::hex(0xffffff)
                     : (is_primary ? Color4f::hex(0x0a0e17)
                                : (is_error ? Color4f::hex(0x1c0a0a)
                                         : Color4f::hex(0x0d1117)));
      p.fill_hover = light ? Color4f::hex(0xe8f5e9)
                           : Color4f::hex(0x161b22);
      p.fill_pressed = p.fill_hover;
      p.text = light ? Color4f::hex(0x1b5e20) : neon;
      p.border = light ? Color4f::hex(0x4caf50) : neon;
      p.radius = 0;
      p.weight = 700;
      p.translate_hover = -1.0f;    // translateY(-1px)
      p.translate_active = 1.0f;    // translateY(1px)
      p.glow = Color4f{p.border.r, p.border.g, p.border.b, 0.35f};  // 霓虹外发光
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
      p.radius = 0;
      p.weight = 700;
      p.translate_active = 1.0f;   // transform: translateY(1px)
      break;
    }
  }
  return p;
}

// ---------- Segmented（← get3DSegmentedContainer/Active/TextColor） ----------
SegmentedPaint ThemeService::segmented_paint() const {
  bool light = is_light();
  SegmentedPaint p{};
  p.text = text_primary();
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
      break;
    case SkinId::Original:
      p.container_bg = light ? Color4f::hex(0xf1f5f9) : Color4f::hex(0x1e1f29);
      p.container_border = light ? Color4f::hex(0xcbd5e1) : Color4f::hex(0x475569);
      p.active_bg = primary();  // var(--primary-main)
      p.active_text = Color4f::hex(0xFFFFFF);
      p.active_radius = 3;
      break;
    case SkinId::ModernFlat:
      p.container_bg = light ? Color4f::hex(0xf3f4f6) : Color4f::hex(0x111827);
      p.container_border = light ? Color4f::hex(0xd1d5db) : Color4f::hex(0x4b5563);
      p.active_bg = primary();
      p.active_text = Color4f::hex(0xFFFFFF);
      p.active_radius = 4;
      break;
    case SkinId::FrostedGlass:
      p.container_bg = Color4f::hex(0xFFFFFF, 0.04f);
      p.container_border = Color4f::hex(0xFFFFFF, 0.10f);
      p.active_bg = with_alpha(primary(), 0.3f);
      p.active_border = Color4f::hex(0xFFFFFF, 0.2f);
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
      break;
    case SkinId::Monochrome:
      p.container_border = light ? Color4f::hex(0x000000) : Color4f::hex(0xffffff);
      p.container_bg = light ? Color4f::hex(0xffffff) : Color4f::hex(0x000000);
      p.active_bg = light ? Color4f::hex(0x000000) : Color4f::hex(0xffffff);
      p.active_text = light ? Color4f::hex(0xffffff) : Color4f::hex(0x000000);
      p.active_radius = 0;
      break;
  }
  return p;
}

// ---------- Input（← get3DInputStyle 骨架：input bg/border） ----------
InputPaint ThemeService::input_paint() const {
  return {vars_.theme_input_bg, vars_.theme_input_border, primary()};
}

}  // namespace clashwin::ui::theme
