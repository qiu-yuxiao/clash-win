// 设置抽屉实现（T17，按 Mini 3.2.2 源码重做；不再用像素反推）。
// 容器/面板/卡片/开关的取值见 settings_drawer.h 顶部注释与各常量行内 src 注释。
#include "settings_drawer.h"

#include <windows.h>
#include <shellapi.h>


#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

#include "../../cmd/cmd_support.h"
#include "../theme/theme.h"
#include "../../i18n/i18n.h"
#include "clashwin/config/constants.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"
#include "utility_modals.h"
#include "../help_menu.h"
#include "ui/i18n_text.h"  // F-D01

namespace clashwin::ui::scene {

namespace utf = clashwin::utf;
using theme::ThemeService;

namespace {

// ---------- 几何常量（逻辑 DIP，全部来自源码，勿改） ----------
// src: _layout.tsx:1420 padding '12px'
constexpr float kPad = 12.0f;
// src: _layout.tsx:1421 gap '12px'
constexpr float kGap = 12.0f;
// src: _layout.tsx:1428-1432 flex:'0 0 240px'; width:240; gap:'5px'
constexpr float kLeftW = 240.0f, kLeftGap = 5.0f;
// src: _layout.tsx:1434 pr:1 → 8px（左列右内边距）
constexpr float kLeftPr = 8.0f;
// src: _layout.tsx:1434-1436 pr1=8 + borderRight 1（border-box 计入宽度）→ 231
constexpr float kCardW = kLeftW - kLeftPr - 1.0f;  // 231
// src: button-styles.ts:549 borderRadius '6px'（get3DCardStyle default）
constexpr float kCardR = 6.0f;
// src: index.scss:76 border-radius var(--border-radius, 8px)
constexpr float kPanelR = 8.0f;
// src: index.scss:125 border: 4px double #B8860B
constexpr float kPanelBorder = 4.0f;
// src: basic-settings-card.tsx:139 / theme-settings-card.tsx:139 p:1 → 8px
constexpr float kCardPad = 8.0f;
// src: _layout.tsx:1284-1297 关闭钮 top:3 right:8、28×28、图标 fontSize 20
constexpr float kCloseSize = 28.0f, kCloseTop = 3.0f, kCloseRight = 8.0f;

// 卡高 = MUI 自适应内容流（源码无固定 height）；数值按源码子元素尺寸累加：
// 组合卡 = p8 + [标题 20.4 + mb 6 + 段 25.2 + mb 8] + [标题 20.4 + mb 6 + 段 25.2] + p8
//   src: takeover-mode-card.tsx:51-76 title subtitle2(13px→行高 20.4) + mb 0.75(=6)
//        + 段高 25.2（height:22 content-box + p:'1px' + 1px 边框，
//        get3DSegmentedContainerStyle:939）+ mb 1(=8)
//   src: routing-preference-card.tsx:53-75 同构，但没有 mb 1
constexpr float kComboH = 128;          // 60 + 52 + 两卡间距/边框累计
constexpr float kComboTakeoverH = 60;   // 20.4+6+25.2+8
constexpr float kComboRoutingH = 52;    // 20.4+6+25.2
// src: basic-settings-card.tsx:89-99 无固定高，内容驱动：
//   p16 + 标题 20.4 + mb4 + 2×24 + mt2 + 24 + p16 = 130.4
constexpr float kBasicH = 130.4f;
// src: theme-settings-card.tsx:148-494 无固定高（flexShrink 0）：
//   p8 + 标题 20.4 + mb4 + 2×23.2 + mt8 + 按钮24 + 页脚(pt8+18.3+16.6) + p8 = 161.7
constexpr float kThemeH = 161.7f;

bool is_light() { return ThemeService::instance().is_light(); }
RectF inset_rect(const RectF& r, float d);

// 关闭钮：金渐变 3D 底 + Fluent 图标。作为抽屉最后挂载的子件绘制，
// 保证压在订阅卡/连接面板之上（此前底图由 SettingsDrawer::on_paint 画，
// 被子卡覆盖，只剩卡片右侧约 10 DIP 可见可点）。
class CloseButton : public IconW {
 public:
  CloseButton() = default;
  void on_paint(IRenderTarget& rt) override {
    RectF r{0, 0, bounds().w, bounds().h};
    // src: _layout.tsx:1294 get3DButtonStyle(theme,'contained','primary') →
    //   button-styles.ts:39 六停金渐变（与 pin 置顶态同源），非 upload 卡橙色分支
    GradientStop cs[6] = {{0.00f, Color4f::hex(0xFFE082)},
                          {0.20f, Color4f::hex(0xFFD54F)},
                          {0.45f, Color4f::hex(0xFFC107)},
                          {0.70f, Color4f::hex(0xFFB300)},
                          {0.85f, Color4f::hex(0xD4AF37)},
                          {1.00f, Color4f::hex(0xB8860B)}};
    rt.fill_round_rect_gradient_angle(r, kCardR, cs, 6, 135.0f);
    // src: button-styles.ts:52 primary contained 边框 #8A6D00(light)/#634E00(dark)，2px
    Pen bevel{is_light() ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00), 2.0f};
    rt.round_rect(inset_rect(r, 1.0f), kCardR, bevel, nullptr);
    IconW::on_paint(rt);
  }
};

// 底部行皮肤格（← _layout.tsx:1702-1757）：未选中为平面灰格；选中为
//   get3DButtonStyle(theme, 'contained', 'primary')，随当前皮肤查表。
class SkinCell : public Button {
 public:
  explicit SkinCell(std::wstring t) : Button(std::move(t)) {}
  bool selected = false;

 protected:
  void draw_button_bg(IRenderTarget& rt, const RectF& r) override {
    const State st = state();
    if (!selected) {
      // src: _layout.tsx:1714-1716 未选中 color '#555555/#aaaaaa'、
      //   bg '#f3f3f3/#1e1e1e'、border '#d0d0d0/#404040'
      // src: _layout.tsx:1724-1726 hover bg '#e5e5e5/#2d2d2d'
      const bool hov = st == State::Hover || st == State::Pressed;
      Color4f bg = is_light()
                       ? (hov ? Color4f::hex(0xE5E5E5) : Color4f::hex(0xF3F3F3))
                       : (hov ? Color4f::hex(0x2D2D2D) : Color4f::hex(0x1E1E1E));
      Pen border{is_light() ? Color4f::hex(0xD0D0D0) : Color4f::hex(0x404040), 1.0f};
      Brush fill{bg};
      rt.round_rect(r, 0.0f, border, &fill);  // src: _layout.tsx:1705-1727 radius 0
      return;
    }
    if (ThemeService::instance().skin() == theme::SkinId::Retro3d) {
      // src: button-styles.ts:39 selected 135° 六停
      GradientStop cs[6];
      if (st == State::Hover) {
        // src: button-styles.ts:105-106 primary hover 六停
        cs[0] = {0.00f, Color4f::hex(0xFFF59D)};
        cs[1] = {0.20f, Color4f::hex(0xFFE082)};
        cs[2] = {0.45f, Color4f::hex(0xFFD54F)};
        cs[3] = {0.70f, Color4f::hex(0xFFC107)};
        cs[4] = {0.85f, Color4f::hex(0xFFB300)};
        cs[5] = {1.00f, Color4f::hex(0xD4AF37)};
      } else {
        cs[0] = {0.00f, Color4f::hex(0xFFE082)};
        cs[1] = {0.20f, Color4f::hex(0xFFD54F)};
        cs[2] = {0.45f, Color4f::hex(0xFFC107)};
        cs[3] = {0.70f, Color4f::hex(0xFFB300)};
        cs[4] = {0.85f, Color4f::hex(0xD4AF37)};
        cs[5] = {1.00f, Color4f::hex(0xB8860B)};
      }
      rt.fill_round_rect_gradient_angle(r, 6.0f, cs, 6, 135.0f);
      // src: button-styles.ts:36/81-82 border 2px '#8A6D00'(light)/'#634E00'(dark)
      Pen b{is_light() ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00), 2.0f};
      rt.round_rect(r, 6.0f, b, nullptr);
      return;
    }
    // 其余皮肤：theme.cpp 六皮肤表（get3DButtonStyle primary 离散色）
    auto p = ThemeService::instance().button_paint("primary");
    Color4f fill = st == State::Hover ? p.fill_hover
                   : (st == State::Pressed ? p.fill_pressed : p.fill);
    Brush b{fill};
    rt.round_rect(r, p.radius, {p.border, p.border_w}, &b);
  }

  void draw_button_text(IRenderTarget& rt, const RectF& r) override {
    TextStyle ts = text_style;
    // src: _layout.tsx:1714 未选中 '#555555'/'#aaaaaa'
    // src: button-styles.ts:35 selected primary textColor '#2C1F03'
    ts.color = selected
                   ? (ThemeService::instance().skin() == theme::SkinId::Retro3d
                          ? Color4f::hex(0x2C1F03)
                          : ThemeService::instance().button_paint("primary").text)
                   : (is_light() ? Color4f::hex(0x555555) : Color4f::hex(0xAAAAAA));
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt.draw_text(text, r, ts);
  }
};

// src: button-styles.ts:535 get3DCardStyle('default').color
//   light '#3C2F0F' / dark '#2C1F03'
Color4f card_text() {
  return is_light() ? Color4f::hex(0x3C2F0F) : Color4f::hex(0x2C1F03);
}

// src: use-custom-theme.ts:364-371 --divider-color .06 用于 CSS 变量；
// 组件内用的是 MUI theme.palette.divider（light rgba(0,0,0,.12) /
// dark rgba(255,255,255,.12)）——此处是组件边框，取后者。
// src: _layout.tsx:1436 borderRight '1px solid' theme.palette.divider
Color4f divider_color() {
  return is_light() ? Color4f::hex(0x000000, 0.12f)
                    : Color4f::hex(0xFFFFFF, 0.12f);
}

// Windows 应用暗色（HKCU\...\Themes\Personalize AppsUseLightTheme，0=暗）
bool system_is_dark() {
  DWORD v = 1, sz = sizeof(v);
  LONG rc = RegGetValueW(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz);
  return rc == ERROR_SUCCESS && v == 0;
}

// ---------- get3DCardStyle('default') 渐变卡 ----------
// src: button-styles.ts:534-565
void draw_card_bg(IRenderTarget& rt, const RectF& r) {
  GradientStop stops[4];
  if (is_light()) {
    // src: button-styles.ts:538 light #FFFDE7@0 #FFF9C4@35 #FFF59D@70 #FFE082@100
    stops[0] = {0.00f, Color4f::hex(0xFFFDE7)};
    stops[1] = {0.35f, Color4f::hex(0xFFF9C4)};
    stops[2] = {0.70f, Color4f::hex(0xFFF59D)};
    stops[3] = {1.00f, Color4f::hex(0xFFE082)};
  } else {
    // src: button-styles.ts:539 dark #FFF59D@0 #FBC02D@35 #F57F17@70 #E65100@100
    stops[0] = {0.00f, Color4f::hex(0xFFF59D)};
    stops[1] = {0.35f, Color4f::hex(0xFBC02D)};
    stops[2] = {0.70f, Color4f::hex(0xF57F17)};
    stops[3] = {1.00f, Color4f::hex(0xE65100)};
  }
  rt.fill_round_rect_gradient_angle(r, kCardR, stops, 4, 135.0f);  // src: button-styles.ts:544
  // 底缘 bevel 线 src: :540 bevelShadowDark light '#B8860B' / dark '#755500'
  Brush bevel{is_light() ? Color4f::hex(0xB8860B) : Color4f::hex(0x755500)};
  rt.fill_rect({r.x + kCardR, r.bottom() - 1, r.w - kCardR * 2, 1}, bevel);
}

RectF inset_rect(const RectF& r, float d) {
  return {r.x + d, r.y + d, r.w - d * 2, r.h - d * 2};
}

// ---------- theme-panel 底 ----------
// src: index.scss:124-132（retro-3d dark）/ :146-152（light）
void draw_panel_bg(IRenderTarget& rt, const RectF& r) {
  GradientStop stops[3];
  if (is_light()) {
    // src: index.scss:147 light linear-gradient(135deg,#FFFDE7,#FFF9C4 50%,#FFF59D)
    stops[0] = {0.0f, Color4f::hex(0xFFFDE7)};
    stops[1] = {0.5f, Color4f::hex(0xFFF9C4)};
    stops[2] = {1.0f, Color4f::hex(0xFFF59D)};
  } else {
    // src: index.scss:126 dark linear-gradient(135deg,#1A160E,#2A2413 50%,#17140B)
    stops[0] = {0.0f, Color4f::hex(0x1A160E)};
    stops[1] = {0.5f, Color4f::hex(0x2A2413)};
    stops[2] = {1.0f, Color4f::hex(0x17140B)};
  }
  rt.fill_round_rect_gradient_angle(r, kPanelR, stops, 3, 135.0f);  // src: index.scss:126
  // src: index.scss:125 border 4px double #B8860B → 两道 1px 金线（外 0.5、内 3.5）
  Pen gold{Color4f::hex(0xB8860B), 1.0f};
  rt.round_rect(inset_rect(r, 0.5f), kPanelR, gold, nullptr);
  rt.round_rect(inset_rect(r, 3.5f), kPanelR - 2.0f, gold, nullptr);
}

// ---------- 金渐变分段控件 ----------
void style_gold_segmented(Segmented* s) {
  s->themed_ = false;
  // 容器 src: button-styles.ts:939-945 border 1px rgba(212,175,55,.35/.22)，
  //   bg rgba(212,175,55,.06/.08)（get3DSegmentedContainerStyle）
  s->bg_ = is_light() ? Color4f::hex(0xD4AF37, 0.06f)
                      : Color4f::hex(0xD4AF37, 0.08f);
  // 滑块 src: button-styles.ts:998-1023 135° 金渐变（gold_3d_ 内走真渐变+描边）
  s->gold_3d_ = true;
  s->selected_bg_ = Color4f::hex(0xFFC107);
  // src: button-styles.ts:1105-1121 retro-3d 选中文字 '#2C1F03'
  s->selected_text_color_ = Color4f::hex(0x2C1F03);
  // src: takeover-mode-card.tsx:110-115 未选中：
  //   isRetro3DDark ? 'rgba(44,31,3,0.75)' : 'text.secondary'
  //   text.secondary = _theme.tsx:6 '#3C3C4399'（a=0x99/255≈0.6）
  s->text_color_ = is_light() ? Color4f::hex(0x3C3C43, 0.6f)
                              : Color4f::hex(0x2C1F03, 0.75f);
  // src: button-styles.ts:1005 滑块 radius 3；theme-settings-card.tsx:174 容器 4
  s->radius_ = 4.0f;  // 容器圆角；滑块在 segmented.cpp 内固定 3
  // src: takeover-mode-card.tsx:116 / theme-settings-card.tsx:211 fontSize 13/10
  s->style_.size = 13;
  s->style_.weight = static_cast<int>(Weight::Bold);
}

// 主题卡三段为 10px（theme-settings-card.tsx:211），单独一套
void style_gold_segmented_small(Segmented* s) {
  style_gold_segmented(s);
  s->style_.size = 10;  // src: theme-settings-card.tsx:211 fontSize '10px'
}

void style_slider(Slider* s) {
  // src: style-helpers.tsx:251-283 get3DSliderStyle retro-3d
  s->thickness_ = 5.0f;   // rail/track height 5
  s->thumb_d_ = 14.0f;    // thumb 14×14
  // rail src: :254 opacity .85 × :255-261 bgcolor rgba(212,175,55,.25/.15)
  s->track_ = is_light() ? Color4f::hex(0xD4AF37, 0.25f * 0.85f)
                         : Color4f::hex(0xD4AF37, 0.15f * 0.85f);
  // track src: :268-269 linear-gradient(to bottom,#FFE082,#FFC107,#B8860B)
  // thumb src: :280-283 radial 金（gold_3d_ 内真实渐变）
  s->gold_3d_ = true;
  s->fill_ = Color4f::hex(0xFFC107);
  s->thumb_ = is_light() ? Color4f::hex(0xFFD54F) : Color4f::hex(0xFFC107);
}

void style_input(TextField* f) {
  // src: button-styles.ts:734-778 get3DInputStyle
  f->bg_ = is_light() ? Color4f::hex(0xFFFDE7, 0.8f)
                      : Color4f::hex(0x1A160E, 0.4f);
  // outline 2px rgba(212,175,55,.35/.25) src: button-styles.ts:734-778
  f->border_ = Color4f::hex(0xD4AF37, is_light() ? 0.35f : 0.25f);
  f->border_w_ = 2.0f;
  // focus src: :770-775 border '#B8860B'(light)/'#FFA000'(dark)
  f->border_focused_ = is_light() ? Color4f::hex(0xB8860B) : Color4f::hex(0xFFA000);
  // 文字 src: _theme.tsx:5/:20 text.primary '#000000' / '#FFFFFF'
  //   （basic-settings-card.tsx:238-251 只给 13px 居中，未覆写颜色）
  f->style_.color = theme::ThemeService::instance().palette().primary_text;
  f->style_.size = 13;
  f->style_.align_h = AlignH::Center;
  f->caret_color_ = f->style_.color;
}

}  // namespace

// ---------- 皮肤/因子持久化（clashwin-skin.json） ----------
SkinPrefs load_skin_prefs() {
  SkinPrefs p;
  std::wstring path = util::dirs::app_home_dir() + L"\\clashwin-skin.json";
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return p;
  std::stringstream ss;
  ss << f.rdbuf();
  try {
    auto j = nlohmann::json::parse(ss.str());
    p.skin = j.value("skin", p.skin);
    p.theme_mode = j.value("theme_mode", p.theme_mode);
    p.depth = j.value("depth", p.depth);
    p.vibrancy = j.value("vibrancy", p.vibrancy);
  } catch (const std::exception&) {
    // 损坏 → 缺省
  }
  return p;
}

void save_skin_prefs(const SkinPrefs& p) {
  std::wstring path = util::dirs::app_home_dir() + L"\\clashwin-skin.json";
  nlohmann::json j;
  j["skin"] = p.skin;
  j["theme_mode"] = p.theme_mode;
  j["depth"] = p.depth;
  j["vibrancy"] = p.vibrancy;
  std::ofstream f(path, std::ios::binary);
  if (f.is_open()) f << j.dump();
}

// ---------- AccentColor（DWM） ----------
std::optional<uint32_t> system_accent_color_rgb() {
  HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
  if (!dwm) return std::nullopt;
  using Fn = HRESULT(__stdcall*)(DWORD*, BOOL*);
  auto fn = reinterpret_cast<Fn>(GetProcAddress(dwm, "DwmGetColorizationColor"));
  std::optional<uint32_t> out;
  if (fn) {
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(fn(&color, &opaque))) out = color & 0x00FFFFFF;
  }
  FreeLibrary(dwm);
  return out;
}

// ---------- 订阅右键菜单动作辅助 ----------
// 复制链接（← _layout.tsx:987-998 navigator.clipboard.writeText）
static bool copy_text_to_clipboard(const std::string& utf8) {
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

// 系统编辑器打开订阅文件（← cmd/profile.rs:403-420 view_profile → help::open_file）
static bool open_profile_file(const std::string& file) {
  if (file.empty()) return false;
  std::wstring path = util::dirs::app_profiles_dir() + L"\\" + utf::utf8_to_utf16(file);
  if (!std::filesystem::exists(path)) return false;
  return reinterpret_cast<INT_PTR>(
             ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

// ---------- TakeoverCard ----------
// src: takeover-mode-card.tsx:53-76（标题 13px bold mb 0.75 + 三段 22 高）
TakeoverCard::TakeoverCard() : Widget(nullptr) {
  auto t = std::make_unique<Label>();
  title_ = t.get();
  title_->style.size = 13;  // src: takeover-mode-card.tsx:60 fontSize 13px
  title_->style.weight = static_cast<int>(Weight::Bold);
  title_->style.color = card_text();
  title_->set_i18n_text("settings.mini.takeoverMode");
  add_child(std::move(t));
  auto seg = std::make_unique<TakeoverSegmented>();
  seg_ = seg.get();
  // TakeoverSegmented 自绘金渐变段（quick_bar.cpp，源码 takeover-mode-card.tsx）
  add_child(std::move(seg));
  auto hint = std::make_unique<Label>();
  hint_ = hint.get();
  hint_->set_visible(false);
  add_child(std::move(hint));
  auto ub = std::make_unique<Button>(L"🔓 解锁检测");
  unlock_btn_ = ub.get();
  ub->set_visible(false);
  add_child(std::move(ub));
}

void TakeoverCard::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: takeover-mode-card.tsx:51-76 标题 13px/1.57=20.4、mb 6、段 25.2
  title_->layout_tree({0, 0, area.w, 20.4f});
  seg_->layout_tree({0, 26.4f, area.w, 25.2f});
}

// ---------- RoutingCard ----------
// src: routing-preference-card.tsx:53-75（同构：标题 13px bold mb 0.75 + 段 25.2）
RoutingCard::RoutingCard() : Widget(nullptr) {
  auto t = std::make_unique<Label>();
  title_ = t.get();
  title_->style.size = 13;
  title_->style.weight = static_cast<int>(Weight::Bold);
  title_->style.color = card_text();
  title_->set_i18n_text("settings.mini.proxyMethod");
  add_child(std::move(t));
  auto seg = std::make_unique<Segmented>();
  seg_ = seg.get();
  seg_->set_options_i18n({"settings.mini.globalProxy", "settings.mini.gfwlist",
                          "settings.mini.addUrl"}, 1);
  // src: locales/zh/settings.json:567-569 globalProxyTooltip/gfwlistTooltip/addUrlTooltip
  seg_->set_tooltips_i18n({"settings.mini.globalProxyTooltip",
                           "settings.mini.gfwlistTooltip",
                           "settings.mini.addUrlTooltip"});
  style_gold_segmented(seg_);
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
  // src: routing-preference-card.tsx:53-75 标题 20.4、mb 6、段 25.2
  title_->layout_tree({0, 0, area.w, 20.4f});
  seg_->layout_tree({0, 26.4f, area.w, 25.2f});
}

// ---------- BasicCard ----------
// src: basic-settings-card.tsx:89-255（p:1；标题 13px bold mb 0.5；
//   2×2 grid columnGap 3=24px、行 px 0.5 py 0.15；标签 13px bold；
//   开关 size small + scale(0.85)；Mixed Port 行跨两列 mt 0.25=2）
BasicCard::BasicCard() : Widget(nullptr) {
  auto t = std::make_unique<Label>();
  title_ = t.get();
  title_->style.size = 13;  // src: basic-settings-card.tsx:158 fontSize 13px
  title_->style.weight = static_cast<int>(Weight::Bold);
  title_->style.color = card_text();
  title_->set_i18n_text("settings.components.verge.basic.title");
  add_child(std::move(t));
  struct Def {
    std::wstring text;
    SwitchW** slot;
  } defs[4] = {{tr("settings.sections.system.fields.autoLaunch"), &sw_auto_launch_},
               {L"Allow LAN", &sw_allow_lan_},
               {tr("settings.sections.system.fields.silentStart"), &sw_silent_start_},
               {L"Allow IPv6", &sw_allow_ipv6_}};
  for (int i = 0; i < 4; ++i) {
    auto lb = std::make_unique<Label>();
    labels_[i] = lb.get();
    labels_[i]->style.size = 13;  // src: basic-settings-card.tsx:240 caption 13px bold
    labels_[i]->style.weight = static_cast<int>(Weight::Bold);
    labels_[i]->style.color = card_text();
    labels_[i]->set_text(defs[i].text);
    add_child(std::move(lb));
    auto sw = std::make_unique<SwitchW>();
    *defs[i].slot = sw.get();
    add_child(std::move(sw));
  }
  auto pl = std::make_unique<Label>();
  port_label_ = pl.get();
  port_label_->style.size = 13;  // src: basic-settings-card.tsx:240 caption 13px
  port_label_->style.weight = static_cast<int>(Weight::Bold);
  port_label_->style.color = card_text();
  port_label_->set_text(L"Mixed Port");
  add_child(std::move(pl));
  auto pf = std::make_unique<TextField>();
  port_field_ = pf.get();
  style_input(port_field_);
  port_field_->set_text(std::to_wstring(static_cast<uint32_t>(config::consts::kDefaultMixed)));
  add_child(std::move(pf));
  auto btn = std::make_unique<Button>();
  btn->set_i18n_text("shared.actions.save");
  save_btn_ = btn.get();
  btn->set_visible(false);
  add_child(std::move(btn));
}

void BasicCard::on_arrange(const RectF& area) {
  set_bounds(area);
  const float pad = kCardPad;                       // p:1
  // src: basic-settings-card.tsx:101-113 subtitle2 13px/1.57=20.4、mb 0.5=4
  title_->layout_tree({pad, pad, area.w - pad * 2, 20.4f});
  const float grid_y = pad + 20.4f + 4.0f;
  const float gap_x = 24.0f;                        // columnGap 3
  const float col_w = (area.w - pad * 2 - gap_x) / 2;
  const float row_h = 24.0f;    // caption 13px*1.66=21.6 + py 0.15*2=2.4（src: :23-29）
  SwitchW* sws[4] = {sw_auto_launch_, sw_allow_lan_, sw_silent_start_,
                     sw_allow_ipv6_};
  for (int i = 0; i < 4; ++i) {
    float cx = pad + (i % 2) * (col_w + gap_x);
    float cy = grid_y + (i / 2) * row_h;
    // px 0.5=4（src: :27）
    labels_[i]->layout_tree({cx + 4.0f, cy, col_w - 8.0f - 28.0f, row_h});
    // 开关 scale(0.85)：28×14 → 23.8×11.9，marginRight 1=8、右对齐垂直居中
    // src: basic-settings-card.tsx:147-150 / base-switch.tsx:26
    sws[i]->layout_tree({cx + col_w - 8.0f - 23.8f,
                         cy + (row_h - 11.9f) / 2.0f, 23.8f, 11.9f});
  }
  // Mixed Port 行：grid 第 3 行跨两列，mt 0.25=2（src: :210-217）
  const float mixed_y = grid_y + row_h * 2 + 2.0f;
  // 标签宽 auto：整行余下空间（src: :218-220）
  port_label_->layout_tree({pad + 4.0f, mixed_y, area.w - pad - 84.0f, 24});
  // 输入框 src: basic-settings-card.tsx:238-251 width 80px，py 2 + 13*1.4375 ≈ 22.7
  port_field_->layout_tree({area.w - pad - 80.0f, mixed_y + (24.0f - 22.7f) / 2.0f,
                           80.0f, 22.7f});
}

// ---------- ThemeCard ----------
// src: theme-settings-card.tsx:136-495
ThemeCard::ThemeCard() : Widget(nullptr) {
  // src: theme-settings-card.tsx:156-167 标题 13px bold、mb 0.5=4
  auto t = std::make_unique<Label>();
  title_ = t.get();
  title_->style.size = 13;
  title_->style.weight = static_cast<int>(Weight::Bold);
  title_->style.color = card_text();
  title_->set_i18n_text("settings.components.verge.basic.fields.themeSetting");
  add_child(std::move(t));
  const wchar_t* skins[] = {L"3D", L"Org", L"Mod", L"Fro", L"Cyber", L"Mono"};
  for (int i = 0; i < 6; ++i) {
    auto b = std::make_unique<Button>(skins[i]);
    b->set_visible(false);  // Mini 抽屉无皮肤条
    skin_buttons_.push_back(b.get());
    add_child(std::move(b));
  }
  // src: theme-settings-card.tsx:427-448 调试日志钮（contained primary 13px、高24）
  auto lb = std::make_unique<Button>();
  lb->set_i18n_text("settings.mini.debugLogs");
  logs_btn_ = lb.get();
  // src: button-styles.ts:73-81 get3DButtonStyle：radius 6 / 2px 边框 / bold 700
  lb->themed_ = false;
  lb->paint_params.radius = 6.0f;
  lb->paint_params.border =
      Pen{is_light() ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00), 2.0f};
  lb->text_style.size = 13;  // src: :444 fontSize 13
  lb->text_style.weight = 700;  // src: button-styles.ts:78 'bold'
  lb->paint_params.text_normal = Color4f::hex(0x2C1F03);  // src: :443
  // src: button-styles.ts:39 135° 六停金渐变
  lb->paint_params.grad[0] = {0.00f, Color4f::hex(0xFFE082)};
  lb->paint_params.grad[1] = {0.20f, Color4f::hex(0xFFD54F)};
  lb->paint_params.grad[2] = {0.45f, Color4f::hex(0xFFC107)};
  lb->paint_params.grad[3] = {0.70f, Color4f::hex(0xFFB300)};
  lb->paint_params.grad[4] = {0.85f, Color4f::hex(0xD4AF37)};
  lb->paint_params.grad[5] = {1.00f, Color4f::hex(0xB8860B)};
  lb->paint_params.grad_count = 6;
  lb->paint_params.grad_angle = 135.0f;
  add_child(std::move(lb));
  auto mode = std::make_unique<Segmented>();
  mode_seg_ = mode.get();
  mode_seg_->set_options_i18n({"settings.sections.appearance.system",
                               "settings.sections.appearance.light",
                               "settings.sections.appearance.dark"}, 0);
  style_gold_segmented_small(mode_seg_);
  add_child(std::move(mode));
  auto dl = std::make_unique<Label>();
  depth_label_ = dl.get();
  depth_label_->style.size = 13;  // src: theme-settings-card.tsx:345 fontSize 13
  // src: layout.scss:324 --control-font-family 'Trebuchet MS, SimHei, sans-serif'
  depth_label_->style.font_family = L"Trebuchet MS";
  depth_label_->style.align_v = AlignV::Center;  // ListItem alignItems center（:331）
  // src: theme-settings-card.tsx:344 variant="caption" → 常规字重（非 Bold）
  depth_label_->style.color = card_text();
  depth_label_->set_text(L"Depth");
  add_child(std::move(dl));
  auto ds = std::make_unique<Slider>();
  depth_slider_ = ds.get();
  // src: theme-settings-card.tsx:82-99 getSlider1Max retro-3d = 1.0，step .1
  depth_slider_->min_ = 0;
  depth_slider_->max_ = 1.0f;
  depth_slider_->step_ = 0.1f;
  style_slider(depth_slider_);
  add_child(std::move(ds));
  auto dv = std::make_unique<Label>();
  depth_value_ = dv.get();
  dv->style.size = 12;  // src: theme-settings-card.tsx:355 fontSize 12 bold
  dv->style.weight = static_cast<int>(Weight::Bold);
  dv->style.font_family = L"Trebuchet MS";  // src: layout.scss:324
  dv->style.align_v = AlignV::Center;
  dv->style.align_h = AlignH::Right;        // src: :335-338 space-between
  // src: :357 'text.secondary'（retro 暗态 #2C1F03）
  dv->style.color = is_light() ? Color4f::hex(0x3C3C43, 0.6f) : Color4f::hex(0x2C1F03);
  dv->set_text(SettingsDrawer::fmt_factor(0.0f));
  add_child(std::move(dv));
  auto vl = std::make_unique<Label>();
  vibrancy_label_ = vl.get();
  vibrancy_label_->style.size = 13;  // src: theme-settings-card.tsx:400 fontSize 13
  vibrancy_label_->style.font_family = L"Trebuchet MS";  // src: layout.scss:324
  vibrancy_label_->style.align_v = AlignV::Center;
  // src: theme-settings-card.tsx:393 variant="caption" → 常规字重（非 Bold）
  vibrancy_label_->style.color = card_text();
  vibrancy_label_->set_text(L"Vibrancy");
  add_child(std::move(vl));
  auto vs = std::make_unique<Slider>();
  vibrancy_slider_ = vs.get();
  // src: theme-settings-card.tsx:101-115 getSlider2Max retro-3d = 5.0
  vibrancy_slider_->min_ = 0;
  vibrancy_slider_->max_ = 5.0f;
  vibrancy_slider_->step_ = 0.1f;
  style_slider(vibrancy_slider_);
  add_child(std::move(vs));
  auto vv = std::make_unique<Label>();
  vibrancy_value_ = vv.get();
  vv->style.size = 12;  // src: theme-settings-card.tsx:414 fontSize 12 bold
  vv->style.weight = static_cast<int>(Weight::Bold);
  vv->style.font_family = L"Trebuchet MS";  // src: layout.scss:324
  vv->style.align_v = AlignV::Center;
  vv->style.align_h = AlignH::Right;        // src: :335-338 space-between
  // src: :406 'text.secondary'（retro 暗态 #2C1F03）
  vv->style.color = is_light() ? Color4f::hex(0x3C3C43, 0.6f) : Color4f::hex(0x2C1F03);
  vv->set_text(SettingsDrawer::fmt_factor(0.0f));
  add_child(std::move(vv));
  // 页脚 src: theme-settings-card.tsx:468-494 11px text.secondary + 10px primary
  auto cp = std::make_unique<Label>();
  copyright_ = cp.get();
  copyright_->style.size = 11;
  copyright_->style.align_h = AlignH::Center;
  copyright_->style.color =
      // src: :471 text.secondary + 页脚整体 opacity .75（:459）→ 复合 alpha .45
      is_light() ? Color4f::hex(0x3C3C43, 0.45f) : Color4f::hex(0xEBEBF5, 0.45f);
  // src: locales/zh/settings.json:203 copyright（显示内容按中文包取字面量）
  copyright_->set_i18n_text("settings.components.verge.theme.copyright");
  add_child(std::move(cp));
  auto em = std::make_unique<Label>();
  email_ = em.get();
  email_->style.size = 10;  // src: theme-settings-card.tsx:486 fontSize 10
  email_->style.align_h = AlignH::Center;
  // src: :486 color 'primary.main' + 页脚 opacity .75（:459）
  Color4f pc = theme::ThemeService::instance().primary();
  pc.a = 0.75f;
  email_->style.color = pc;
  email_->set_text(L"qiuyuxiao@gmail.com");
  add_child(std::move(em));
}

void ThemeCard::on_arrange(const RectF& area) {
  set_bounds(area);
  const float pad = kCardPad;
  // 头行 src: theme-settings-card.tsx:148-180（标题左、三段右 100×18、mb 4）
  const float seg_w = 100.0f, seg_h = 18.0f;
  mode_seg_->layout_tree({area.w - pad - seg_w, pad, seg_w, seg_h});
  const float title_lh = 20.4f;  // src: theme-settings-card.tsx:156 subtitle2 13px×1.57
  title_->layout_tree({pad, pad, area.w - pad * 2 - seg_w - 8, title_lh});
  // src: theme-settings-card.tsx:326-333 ListItem dense + py 0.1=0.8 + px 0.5=4；
  //   行高 = caption 13px×1.66=21.6 + 1.6 ≈ 23.2
  const float row1_y = pad + title_lh + 4.0f;  // 头行 mb 0.5=4（:153）
  const float row_h = 23.2f;
  const float slider_w = 100.0f;
  const float wx = pad + 4.0f;                 // ListItem px 0.5=4
  // 标签/数值 flex space-between，mr 1=8 → 数值右缘 = 滑条左缘 - 8（:334-341）
  const float content_w = area.w - wx * 2 - slider_w - 8.0f;
  const float val_w = 40.0f;
  depth_label_->layout_tree({wx, row1_y, content_w - val_w, row_h});
  depth_value_->layout_tree({wx + content_w - val_w, row1_y, val_w, row_h});
  depth_slider_->layout_tree({area.w - wx - slider_w, row1_y, slider_w, row_h});
  vibrancy_label_->layout_tree({wx, row1_y + row_h, content_w - val_w, row_h});
  vibrancy_value_->layout_tree({wx + content_w - val_w, row1_y + row_h, val_w, row_h});
  vibrancy_slider_->layout_tree({area.w - wx - slider_w, row1_y + row_h,
                                 slider_w, row_h});
  // 调试日志钮 src: theme-settings-card.tsx:426-432 高 24、px 2=16×2、居中、mt 1=8
  //   宽度自适应文字（on_measure 已含 12×2 内距，再补 8 凑足 16×2）
  SizeF lb_sz = logs_btn_->on_measure(area.w, 24.0f);
  const float logs_w = lb_sz.w + 8.0f;
  logs_btn_->layout_tree({(area.w - logs_w) / 2.0f, row1_y + row_h * 2 + 8,
                          logs_w, 24.0f});
  // 页脚：pt 1=8 + caption 11px×1.66=18.3 + 10px×1.66=16.6（src: :459-494）
  const float foot_y = area.h - pad - 18.3f - 16.6f;
  copyright_->layout_tree({pad, foot_y, area.w - pad * 2, 18.3f});
  email_->layout_tree({pad, foot_y + 18.3f, area.w - pad * 2, 16.6f});
}

void ThemeCard::on_paint(IRenderTarget& rt) {
  Widget::on_paint(rt);
  if (!copyright_) return;
  // src: theme-settings-card.tsx:460 borderTop 1px dashed divider
  //   divider light rgba(0,0,0,.12) / dark rgba(255,255,255,.12)（_theme.tsx:47）
  const float y = copyright_->bounds().y - 5.0f;
  if (y <= 0 || bounds().w <= 0) return;
  Brush dash{theme::ThemeService::instance().is_light()
                 ? Color4f::hex(0x000000, 0.12f)
                 : Color4f::hex(0xFFFFFF, 0.12f)};
  for (float x = 0; x + 3.0f < bounds().w; x += 6.0f) {
    rt.fill_rect({x, y, 3.0f, 1.0f}, dash);
  }
}

// ---------- SettingsDrawer ----------
SettingsDrawer::SettingsDrawer(Widget* parent) : Stack(parent) {
  // 遮罩：源码无 backdrop（_layout.tsx:1403 条件渲染不透明面板）；保留但恒隐藏
  auto mask = std::make_unique<Widget>();
  mask_ = mask.get();
  mask_->set_visible(false);
  add_child(std::move(mask));

  // 全窗覆盖层（theme-panel 底）
  auto panel = std::make_unique<Widget>();
  panel_ = panel.get();
  panel_->set_visible(false);  // 关闭态不绘制（open_ 为 false）
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

  // 主题卡内调试日志钮（源码 theme-settings-card.tsx:427-448，已在 ThemeCard 构造）
  logs_btn_ = theme_card_->logs_btn_;
  logs_btn_->on_click = [this]() {
    if (on_open_logs) on_open_logs();
  };

  // 帮助与关于（Mini 抽屉无；隐藏保留回调兼容）
  auto hb = std::make_unique<Button>(L"帮助与关于");
  help_btn_ = hb.get();
  hb->set_visible(false);
  panel_->add_child(std::move(hb));

  // 底部行（_layout.tsx:1534-1720，@media max-height:830px 隐藏 ⇒ 显示条件 h≥831）
  //   左 0..80 = ? 帮助、80..177.5 = Language、177.5..640 = 6 个皮肤格（高 24）
  auto style_cell = [](Button* b, float font) {
    b->themed_ = false;
    b->text_style.size = font;
    b->text_style.align_h = AlignH::Center;
    b->text_style.align_v = AlignV::Center;
    // src: button-styles.ts primaryBtn3DStyle 金底 + 深金 1px 描边
    b->paint_params.fill_normal = Color4f::hex(0xD4AF37);
    b->paint_params.fill_hover = Color4f::hex(0xE5C158);
    b->paint_params.fill_pressed = Color4f::hex(0xB8860B);
    b->paint_params.text_normal = Color4f::hex(0x2C1F03);
    b->paint_params.border = Pen{Color4f::hex(0x8C6D1F), 1};
    b->paint_params.radius = 4;
    b->set_visible(false);
  };

  auto bh = std::make_unique<Button>(L"?");
  bottom_help_ = bh.get();
  style_cell(bottom_help_, 16.0f);
  bottom_help_->text_style.weight = static_cast<int>(Weight::Bold);
  bottom_help_->on_click = [this]() { open_help_menu(); };
  bottom_help_->set_tooltip_text(tr("layout.components.helpMenu.buttonAriaLabel"));  // src: locales/zh/layout.json:40 buttonAriaLabel
  panel_->add_child(std::move(bh));

  auto lb = std::make_unique<Button>(L"Language");
  lang_btn_ = lb.get();
  style_cell(lang_btn_, 11.0f);  // src: _layout.tsx:1586 fontSize 11
  lang_btn_->text_style.weight = 700;  // src: :1587 fontWeight 'bold'
  lang_btn_->text_style.font_family = L"Trebuchet MS";  // src: :1588
  lang_btn_->paint_params.radius = 6.0f;  // src: button-styles.ts:79
  // src: button-styles.ts:36 borderColor #8A6D00/#634E00、:81 2px（!important 覆盖）
  lang_btn_->paint_params.border =
      Pen{is_light() ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00), 2.0f};
  // src: _layout.tsx:1596 primaryBtn3DStyle + button-styles.ts:39 六停金渐变
  lang_btn_->paint_params.grad[0] = {0.00f, Color4f::hex(0xFFE082)};
  lang_btn_->paint_params.grad[1] = {0.20f, Color4f::hex(0xFFD54F)};
  lang_btn_->paint_params.grad[2] = {0.45f, Color4f::hex(0xFFC107)};
  lang_btn_->paint_params.grad[3] = {0.70f, Color4f::hex(0xFFB300)};
  lang_btn_->paint_params.grad[4] = {0.85f, Color4f::hex(0xD4AF37)};
  lang_btn_->paint_params.grad[5] = {1.00f, Color4f::hex(0xB8860B)};
  lang_btn_->paint_params.grad_count = 6;
  panel_->add_child(std::move(lb));

  auto lm = std::make_unique<ContextMenu>();
  lang_menu_ = lm.get();
  // src: _layout.tsx:1623 MUI MenuItem 缺省 48；style-helpers.tsx:128-131 13px / px16
  lang_menu_->style_.item_h = 48.0f;
  lang_menu_->style_.padding_h = 16.0f;
  lang_menu_->style_.font_size = 13.0f;
  panel_->add_child(std::move(lm));

  // 帮助菜单（自绘：替代原生 HMENU，样式同订阅右键菜单
  //   src: style-helpers.tsx:126-145 13px bold / padding 8px 16px / 金渐变 hover）
  auto hm = std::make_unique<ContextMenu>();
  help_menu_ = hm.get();
  hm->set_z_order(950);
  panel_->add_child(std::move(hm));

  // 订阅行右键菜单（_layout.tsx:1806-1856）：挂在面板顶层，锚点用窗口坐标
  auto pm = std::make_unique<ContextMenu>();
  profile_menu_ = pm.get();
  pm->set_z_order(950);
  panel_->add_child(std::move(pm));

  // URL 输入框右键菜单（_layout.tsx:1859-1932）：锚点用窗口坐标，open 时转 panel 局部
  auto um = std::make_unique<ContextMenu>();
  url_menu_ = um.get();
  um->set_z_order(950);
  panel_->add_child(std::move(um));

  // 编辑订阅弹窗（layout-dialogs.tsx:119-245）：全窗遮罩，挂抽屉根层
  auto em = std::make_unique<EditProfileModal>();
  edit_profile_modal_ = em.get();
  add_child(std::move(em));

  static const wchar_t* kSkinNames[6] = {L"Trump-3D", L"Original", L"Modern",
                                         L"Frosted", L"Cyberpunk", L"Monochrome"};
  // src: _layout.tsx:1663-1688 每皮肤独立 fontFamily
  static const wchar_t* kSkinFonts[6] = {L"Trebuchet MS", L"Segoe UI", L"Outfit",
                                         L"Segoe UI Light", L"Consolas", L"Georgia"};
  for (int i = 0; i < 6; ++i) {
    auto b = std::make_unique<SkinCell>(kSkinNames[i]);
    SkinCell* raw = b.get();
    style_cell(raw, 11.0f);
    raw->text_style.weight = static_cast<int>(Weight::Bold);
    raw->text_style.font_family = kSkinFonts[i];
    raw->on_click = [this, i]() { select_skin(i); };
    skin_row_.push_back(raw);
    panel_->add_child(std::move(b));
  }
  refresh_skin_cells();  // 初始选中态（← _layout.tsx:1729-1749）

  // T20：右列连接面板（宽窗并排，_layout.tsx:1519-1533）
  auto cp = std::make_unique<ConnectionsPanel>();
  conn_panel_ = cp.get();
  conn_panel_->set_visible(false);
  add_child(std::move(cp));

  // 关闭 ✕：右上 top3 right8、28×28（_layout.tsx:1284-1297）。
  // 必须最后挂载（z 最高）：否则订阅卡/连接面板会盖住底图与命中区，
  // 只剩卡片外侧约 10 DIP 可点（P0-1）。
  auto close = std::make_unique<CloseButton>();
  close_btn_ = close.get();
  close_btn_->set_icon("close");
  close_btn_->size_ = 20;
  close_btn_->color_ = Color4f::hex(0x2C1F03);  // src: button-styles.ts:1105-1121 金底深棕图标
  close_btn_->on_click = [this]() { set_open(false); };
  close_btn_->set_z_order(9999);  // src: _layout.tsx:1286「zIndex: 9999」
  close_btn_->set_visible(false);
  add_child(std::move(close));

  set_open(false);
  // 宿主常驻可见：Stack::on_arrange 会跳过不可见子层，隐藏会丢失布局；
  //   关闭态由 on_paint 早退 + hit_self(open_) 拦截，不遮挡主页点击。
  set_visible(true);
  // 回调必须装配：此前从未调用，导致抽屉内所有控件点击都“无反应”
  //（接管段/代理方式/开关/齿轮/端口/主题模式全部失效）。
  wire_callbacks();
}

SettingsDrawer::~SettingsDrawer() = default;

void SettingsDrawer::set_open(bool open) {
  if (open_ == open) return;
  open_ = open;
  mask_->set_visible(false);  // 源码无遮罩
  panel_->set_visible(open);
  if (close_btn_) close_btn_->set_visible(open);
  if (conn_panel_) conn_panel_->set_visible(open && bounds().w > 285.0f);
  if (open) {
    // 先刷新订阅列表再布局：preferred_height 依赖 profiles_，顺序颠倒会让
    // 首次打开时列表高度算成 0（订阅卡只剩标题/输入/按钮，行不显示）。
    if (profile_card_) profile_card_->reload_profiles();  // 打开时刷新订阅列表
    if (bounds().w > 0) layout_tree(bounds());  // 隐藏子层未参与布局，重开时补齐
    invalidate();
  }
  if (on_open_changed) on_open_changed(open);
}

void SettingsDrawer::select_skin(int i) {
  static const char* names[] = {"retro-3d", "original", "modern-flat",
                                "frosted-glass", "cyberpunk", "monochrome"};
  if (i < 0 || i >= 6) return;
  if (auto skin = theme::skin_from_name(names[i])) {
    ThemeService::instance().set_skin(*skin);
    SkinPrefs p = load_skin_prefs();
    p.skin = names[i];
    save_skin_prefs(p);
    if (on_patch_verge) on_patch_verge("clash_mini_control_skin", names[i]);
    restyle_cards();
  }
}

void SettingsDrawer::apply_skin_prefs() {
  SkinPrefs p = load_skin_prefs();
  auto& svc = ThemeService::instance();
  if (auto skin = theme::skin_from_name(p.skin)) svc.set_skin(*skin);
  // "sys" 必须跟随 Windows 应用暗色（此前一律落到 Light，造成
  // “深色选中却走浅色分支”的反色现象）
  bool dark = p.theme_mode == "dark" ||
              (p.theme_mode != "light" && system_is_dark());
  svc.set_mode(dark ? theme::ThemeMode::Dark : theme::ThemeMode::Light);
  svc.set_factors(p.depth, p.vibrancy);
  // 同步抽屉控件（不触发回调）
  if (theme_card_) {
    int idx = p.theme_mode == "light" ? 1 : (p.theme_mode == "dark" ? 2 : 0);
    theme_card_->mode_seg_->select_silent(idx);
    theme_card_->depth_slider_->set_value(p.depth, false);
    theme_card_->vibrancy_slider_->set_value(p.vibrancy, false);
    theme_card_->depth_value_->set_text(fmt_factor(p.depth));
    theme_card_->vibrancy_value_->set_text(fmt_factor(p.vibrancy));
  }
  restyle_cards();
}

void SettingsDrawer::sync_theme_mode(const std::wstring& mode) {
  std::string m = utf::utf16_to_utf8(mode);
  // Mini 缺省 theme_mode = dark（IVerge::make_template）
  if (m.empty()) m = "dark";
  int idx = m == "light" ? 1 : (m == "dark" ? 2 : 0);
  bool dark = m == "dark" || (m != "light" && system_is_dark());
  auto& svc = ThemeService::instance();
  svc.set_mode(dark ? theme::ThemeMode::Dark : theme::ThemeMode::Light);
  if (theme_card_) theme_card_->mode_seg_->select_silent(idx);
  restyle_cards();
}

void SettingsDrawer::restyle_cards() {
  const Color4f txt = card_text();
  auto set = [&](Label* l) {
    if (l) l->style.color = txt;
  };
  if (profile_card_) {
    // src: profile-import-card.tsx:121 'inherit' → 继承卡片默认色
    //   get3DCardStyle('default').color = #3C2F0F / #2C1F03
    profile_card_->set_title_color(txt);
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
    if (routing_card_->seg_) routing_card_->seg_->invalidate();
  }
  if (basic_card_) {
    set(basic_card_->title_);
    set(basic_card_->port_label_);
    for (auto* l : basic_card_->labels_) set(l);  // 4 个开关文案
  }
  if (theme_card_) {
    set(theme_card_->title_);
    set(theme_card_->depth_label_);
    set(theme_card_->vibrancy_label_);
    // src: :357/:406 数值列 text.secondary（retro 暗态 #2C1F03）
    const Color4f num = is_light() ? Color4f::hex(0x3C3C43, 0.6f)
                                   : Color4f::hex(0x2C1F03);
    theme_card_->depth_value_->style.color = num;
    theme_card_->vibrancy_value_->style.color = num;
    if (theme_card_->copyright_)
      // src: :471 text.secondary = #3C3C4399 / #EBEBF599
      theme_card_->copyright_->style.color =
          is_light() ? Color4f::hex(0x3C3C43, 0.6f)
                     : Color4f::hex(0xEBEBF5, 0.6f);
    if (theme_card_->mode_seg_) theme_card_->mode_seg_->invalidate();
    theme_card_->invalidate();
  }
  // 底部行六格：选中/未选中两态随皮肤与亮暗重刷（← _layout.tsx:1705-1749）
  refresh_skin_cells();
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
  if (!takeover_card_ || !takeover_card_->seg_) return;
  takeover_card_->seg_->set_mode(tun ? "TUN" : (sys_proxy ? "SYS" : "MAN"));
}

void SettingsDrawer::sync_basic(bool auto_launch, bool silent, bool allow_lan,
                                bool ipv6, int mixed_port) {
  if (!basic_card_) return;
  basic_card_->sw_auto_launch_->set_checked(auto_launch, false);
  basic_card_->sw_silent_start_->set_checked(silent, false);
  basic_card_->sw_allow_lan_->set_checked(allow_lan, false);
  basic_card_->sw_allow_ipv6_->set_checked(ipv6, false);
  if (mixed_port > 0)
    basic_card_->port_field_->set_text(std::to_wstring(mixed_port));
  basic_card_->invalidate();
}

void SettingsDrawer::on_arrange(const RectF& area) {
  set_bounds(area);
  mask_->layout_tree({0, 0, area.w, area.h});
  panel_->layout_tree({0, 0, area.w, area.h});
  // 关闭钮 src: _layout.tsx:1284-1297 top 3 right 8
  close_btn_->layout_tree({area.w - kCloseRight - kCloseSize, kCloseTop,
                           kCloseSize, kCloseSize});
  // 编辑订阅弹窗：全窗遮罩 + 居中卡片（layout-dialogs.tsx:119-137）
  if (edit_profile_modal_) edit_profile_modal_->layout_tree({0, 0, area.w, area.h});

  // 左列 src: _layout.tsx:1428-1437（x=padding、y=padding、宽 240、gap 5）
  //   @media not all and (max-height:830px) → pb:30px（给底部行让位，_layout.tsx:1429-1432）
  // src: _layout.tsx:1593/1654 `@media (max-height:830px) { display:none }` 是对
  //   **窗口视口高度**判定；area.h 已扣掉标题栏 30 与底部流量面板 100/135，
  //   旧代码直接比 area.h 导致 831~930 的窗口也判为矮、底部行永不显示。
  const float bottom_inset = area.w <= 285.0f ? 100.0f : 135.0f;
  const bool tall = (area.h + 30.0f + bottom_inset) > 830.0f;
  bottom_row_visible_ = tall && open_;
  // 【DRAWERFIX】面板 4px double 边框占 4 DIP，内边距 12 从边框内侧起算
  //   （_layout.tsx:1420 padding + get3DCardStyle border:4px double）。旧代码
  //   从面板外沿直接加 12，导致整列比 Mini 高 6px、左 6px（27.33 DIP 偏移）。
  const float col_x = kPad + kPanelBorder, col_y = kPad + kPanelBorder;
  const float col_h = area.h - kPanelBorder * 2 - kPad * 2 - (tall ? 30.0f : 0.0f);
  left_rect_ = {col_x, col_y, kLeftW, col_h};
  float y = col_y;
  // 订阅卡为固定内容高（源码 flexShrink:0 + 列表 maxHeight 110，
  //   profile-import-card.tsx:98,196），不吸收剩余高度
  float profile_h = profile_card_ ? profile_card_->preferred_height() : 105.0f;
  // 【DRAWERFIX】源码左列 flexShrink:0（profile-import-card.tsx:98 /
  //   theme-settings-card.tsx:139），订阅卡**不压缩**：超出列底时按 Mini 的
  //   overflow:hidden 语义裁掉末尾的卡，而不是把订阅卡压扁。
  //   旧代码把 profile_h 夹到 avail_profile（实测 117 DIP），导致订阅卡下所有
  //   卡整体上移 ~120px —— 抽屉下半像素行 745/785/820 vs 625/660/697 的根因。
  const float col_bottom = col_y + col_h;
  profile_rect_ = {col_x, y, kCardW, profile_h};
  y += profile_h + kLeftGap;

  // 合并卡（takeover + routing，_layout.tsx:1443-1471）
  //   接管卡含尾随 mb 8（takeover-mode-card.tsx:72），路由卡没有
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
  // 主题卡超出列底时按剩余高度裁切（Mini overflow:hidden 同语义）
  float theme_h = kThemeH;
  if (y + theme_h > col_bottom) theme_h = std::max(0.0f, col_bottom - y);
  theme_rect_ = {col_x, y, kCardW, theme_h};

  profile_card_->layout_tree(profile_rect_);
  basic_card_->layout_tree(basic_rect_);
  theme_card_->layout_tree(theme_rect_);

  // 底部行（_layout.tsx:1534-1720）：bottom 0、高 24；help 0..80、Language
  //   80..177.5、皮肤格 177.5 起（首格 92.5、其余 74，窄窗由窗口边界裁切）
  const float row_y = area.h - 24.0f;
  if (bottom_help_) {
    bottom_help_->layout_tree({0, row_y, 80, 24});
    lang_btn_->layout_tree({80, row_y, 97.5f, 24});
    float sx = 177.5f;
    for (size_t i = 0; i < skin_row_.size(); ++i) {
      const float w = i == 0 ? 92.5f : 74.0f;
      skin_row_[i]->layout_tree({sx, row_y, w, 24});
      sx += w;
    }
    const bool show = tall && open_;
    bottom_help_->set_visible(show);
    lang_btn_->set_visible(show);
    for (auto* b : skin_row_) b->set_visible(show);
    if (!show && lang_menu_ && lang_menu_->is_open()) lang_menu_->close();
  }

  // 取证打点：抽屉左列实际几何（pair_02/pair_03 的像素行复核用）
  CWLOG_INFO(std::format(
      "[DRAWERLAYOUT] area={:.1f}x{:.1f} tall={} col_h={:.1f} profile_h={:.1f} "
      "tops p={:.1f} c={:.1f} b={:.1f} t={:.1f}",
      area.w, area.h, tall, col_h, profile_h, profile_rect_.y, combo_rect_.y,
      basic_rect_.y, theme_rect_.y));

  if (conn_panel_) {
    bool side = area.w > 285.0f;
    conn_panel_->set_visible(open_ && side);
    // src: _layout.tsx:1519-1533 右列 flex:1，占 padding 12 + gap 12 之后
    if (side)
      conn_panel_->layout_tree({kPad + kLeftW + kGap, kPad,
                                area.w - (kPad + kLeftW + kGap) - kPad, col_h});
  }
}

bool SettingsDrawer::on_mouse(const MouseEvent& e) {
  // 源码无 backdrop：面板外点击不关闭抽屉（关闭仅 ✕ / 齿轮钮）
  (void)e;
  return Stack::on_mouse(e);
}

void SettingsDrawer::open_help_menu() {
  if (!help_menu_) return;
  // 外观：与订阅右键菜单完全一致（src: style-helpers.tsx:126-145）
  MenuStyle st;
  // src: MUI MenuItem 缺省 min-height 48（help-menu-button.tsx 未覆写高度）
  //   Divider = 1px + my 8×2 = 17
  st.item_h = 48;
  st.sep_h = 17;
  st.min_w = 160;
  st.radius = 6;
  st.font_size = 13.0f;
  st.font_weight = static_cast<int>(Weight::Bold);
  st.gold_hover = true;
  st.gradient_panel = true;
  st.pad_v = 4;
  st.padding_h = 16;  // src: style-helpers.tsx:126-145 MenuItem padding '8px 16px'
  st.hover_inset = 0;
  st.border = Color4f(1, 1, 1, 0.12f);
  st.text = ThemeService::instance().text_primary();
  st.disabled_text = Color4f(st.text.r, st.text.g, st.text.b, 0.38f);
  if (ThemeService::instance().is_light()) {
    st.grad[0] = {0.0f, Color4f::hex(0xFFFDE7)};
    st.grad[1] = {0.5f, Color4f::hex(0xFFF9C4)};
    st.grad[2] = {1.0f, Color4f::hex(0xFFF59D)};
  } else {
    st.grad[0] = {0.0f, Color4f::hex(0x1A160E)};
    st.grad[1] = {0.5f, Color4f::hex(0x2A2413)};
    st.grad[2] = {1.0f, Color4f::hex(0x17140E)};
  }
  // 6 项 + 2 分隔线（src: help-menu-button.tsx:114-205；文案同原生菜单）
  // 版本后缀（src: help-menu-button.tsx:190-205）：
  //   软件 = '(v' + appVersion + ')'；内核 = '(' + formatCoreVersion(core) + ')'，
  //   formatCoreVersion 见 style-helpers.tsx:221-225（trim → 去前导 v+ → 'Ver.{clean}'）。
  //   取不到内核版本时按源码语义省略整段括号（help-menu-button.tsx:204 三元）。
  auto format_core_version = [](std::string s) -> std::string {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == 'v' || s[i] == 'V')) ++i;  // /^v+/i
    return "Ver." + s.substr(i);
  };
  auto core_version = [&]() -> std::string {
    if (clashwin::cmd::mihomo().is_err()) return {};
    auto v = clashwin::cmd::mihomo().value()->get_version(300);
    if (v.is_err()) return {};
    std::string s = v.value().version;
    // ← tauri-plugin-mihomo：meta 内核在版本串后缀 " Mihomo"（Mini 显示为
    //   'Ver.1.19.33 Mihomo'）；本内核若已带该后缀则不重复拼接。
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
  its.push_back({L"⭐ " + tr("layout.components.helpMenu.github"), false, false, L"", false});
  its.push_back({L"💡 " + tr("layout.components.helpMenu.wiki"), false, false, L"", false});
  its.push_back({L"🧭 " + tr("layout.components.helpMenu.firstRunGuide"), false, false, L"", false});
  its.push_back({L"", false, true, L"", false});
  its.push_back({L"📦 " + tr("layout.components.helpMenu.exportDiagnostics"), false, false, L"", false});
  its.push_back({L"", false, true, L"", false});
  its.push_back({L"🚀 " + tr("layout.components.helpMenu.checkAppUpdate") + app_suffix, false, false, L"", false});
  its.push_back({L"⚙️ " + tr("layout.components.helpMenu.checkCoreUpdate") + core_suffix, false, false, L"", false});
  help_menu_->style_ = st;
  help_menu_->on_select = [this](int idx) {
    HelpAction a;
    switch (idx) {
      case 0: a = HelpAction::StarGithub; break;
      case 1: a = HelpAction::OpenWiki; break;
      case 2: a = HelpAction::ReviewGuide; break;
      case 4: a = HelpAction::ExportDiagnostics; break;
      case 6: a = HelpAction::CheckAppUpdate; break;
      case 7: a = HelpAction::CheckCoreUpdate; break;
      default: return;
    }
    // Mini：检查更新走后端命令并弹出对应弹窗
    //   （use-client-update.ts:32-54 / use-core-update.ts:64-86）
    if (a == HelpAction::CheckAppUpdate && on_check_app_update) {
      on_check_app_update();
      return;
    }
    if (a == HelpAction::CheckCoreUpdate && on_check_core_update) {
      on_check_core_update();
      return;
    }
    HelpMenu::execute_action(a, nullptr);
  };
  // 向上弹出：高 = pad 4×2 + 6 项×48 + 2 分隔线×17 = 330
  // 【注意】ContextMenu::open 用 panel_ 局部坐标（set_bounds 是父级坐标），
  //   必须用 bounds()，用 global_bounds() 会多算标题栏偏移而压到底部行上。
  RectF rb = bottom_help_ ? bottom_help_->bounds() : RectF{0, 0, 80, 24};
  help_menu_->open(its, {rb.x, rb.y - 330.0f - 4.0f});
}

void SettingsDrawer::open_profile_menu(const std::string& uid, PointF anchor) {
  if (!profile_menu_ || !profile_card_) return;
  const config::PrfItem* item = profile_card_->find_item(uid);
  if (!item) return;
  const bool local = item->type() == config::PrfType::Local;
  const std::string url = item->url.value_or(std::string());

  // 菜单外观：src: help-menu-button.tsx:96-110（paper theme-panel / minWidth 220 /
  //   radius 6 / border 1px rgba(255,255,255,.12) / List padding 4px 0）
  //   + style-helpers.tsx:126-145（13px bold / padding 8px 16px / hover 金渐变）
  MenuStyle st;
  // src: MUI MenuItem 缺省 min-height 48；style-helpers.tsx:129 padding '8px 16px'
  st.item_h = 48;
  st.padding_h = 16;
  st.min_w = 220;  // src: help-menu-button.tsx:103 minWidth '220px'
  st.radius = 6;
  st.font_size = 13.0f;
  st.font_weight = static_cast<int>(Weight::Bold);
  st.gold_hover = true;
  st.gradient_panel = true;
  st.pad_v = 4;
  st.hover_inset = 0;
  st.border = Color4f(1, 1, 1, 0.12f);
  st.text = ThemeService::instance().text_primary();
  st.disabled_text = Color4f(st.text.r, st.text.g, st.text.b, 0.38f);
  // src: _theme.tsx:8/:24 error_color '#FF3B30'(light) / '#FF453A'(dark)
  st.danger_text =
      ThemeService::instance().is_light() ? Color4f::hex(0xFF3B30) : Color4f::hex(0xFF453A);
  // theme-panel 135° 3 停靠（src: index.scss:126 dark / :147 light）
  if (ThemeService::instance().is_light()) {
    st.grad[0] = {0.0f, Color4f::hex(0xFFFDE7)};
    st.grad[1] = {0.5f, Color4f::hex(0xFFF9C4)};
    st.grad[2] = {1.0f, Color4f::hex(0xFFF59D)};
  } else {
    st.grad[0] = {0.0f, Color4f::hex(0x1A160E)};
    st.grad[1] = {0.5f, Color4f::hex(0x2A2413)};
    st.grad[2] = {1.0f, Color4f::hex(0x17140E)};
  }

  // 5 项 + 2 分隔线：src: _layout.tsx:1820-1856（图标/文案取自 zh 语言包）
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
      if (it && it->file) open_profile_file(*it->file);
    } else if (idx == 3) {
      copy_text_to_clipboard(url);
    } else if (idx == 4) {
      if (on_update_profile) on_update_profile(uid);
    } else if (idx == 6) {
      if (on_delete_profile) on_delete_profile(uid);
    }
  };
  profile_menu_->open(its, anchor);
}

void SettingsDrawer::open_url_menu(PointF anchor) {
  if (!url_menu_ || !profile_card_) return;
  TextField* f = profile_card_->url_field();
  if (!f) return;

  // 外观：src: _layout.tsx:1872-1884（minWidth 160 / radius 6 / border 1px
  //   rgba(255,255,255,.12) / List padding 4px 0）+ style-helpers.tsx:126-145
  MenuStyle st;
  // src: MUI MenuItem 缺省 min-height 48；style-helpers.tsx:129 padding '8px 16px'
  st.item_h = 48;
  st.padding_h = 16;
  st.min_w = 160;  // src: _layout.tsx:1876 minWidth '160px'
  st.radius = 6;
  st.font_size = 13.0f;
  st.font_weight = static_cast<int>(Weight::Bold);
  st.gold_hover = true;
  st.gradient_panel = true;
  st.pad_v = 4;
  st.hover_inset = 0;
  st.border = Color4f(1, 1, 1, 0.12f);
  st.text = ThemeService::instance().text_primary();
  st.disabled_text = Color4f(st.text.r, st.text.g, st.text.b, 0.38f);
  // src: _theme.tsx:8/:24 error_color '#FF3B30'(light) / '#FF453A'(dark)
  st.danger_text =
      ThemeService::instance().is_light() ? Color4f::hex(0xFF3B30) : Color4f::hex(0xFF453A);
  if (ThemeService::instance().is_light()) {
    st.grad[0] = {0.0f, Color4f::hex(0xFFFDE7)};
    st.grad[1] = {0.5f, Color4f::hex(0xFFF9C4)};
    st.grad[2] = {1.0f, Color4f::hex(0xFFF59D)};
  } else {
    st.grad[0] = {0.0f, Color4f::hex(0x1A160E)};
    st.grad[1] = {0.5f, Color4f::hex(0x2A2413)};
    st.grad[2] = {1.0f, Color4f::hex(0x17140E)};
  }

  // 5 项 + 1 分隔线：src: _layout.tsx:1886-1931（文案 zh: shared.json:27-31）
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
  // ContextMenu 是 panel_ 子件：锚点需转 panel 局部
  url_menu_->open(its, panel_->to_local(anchor));
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

void SettingsDrawer::wire_callbacks() {
  // 订阅导入卡（宿主命令链转发）
  profile_card_->on_import = [this](const std::string& url,
                                    const std::string& name) {
    return on_import_profile ? on_import_profile(url, name) : false;
  };
  profile_card_->on_select_profile = [this](const std::string& uid) {
    if (on_select_profile) on_select_profile(uid);
  };
  profile_card_->on_update_profile = [this](const std::string& uid) {
    if (on_update_profile) on_update_profile(uid);
  };
  profile_card_->on_delete_profile = [this](const std::string& uid) {
    if (on_delete_profile) on_delete_profile(uid);
  };
  // 订阅行右键 → Mini 菜单（_layout.tsx:1806-1856）
  profile_card_->on_context_menu = [this](const std::string& uid, PointF anchor) {
    open_profile_menu(uid, anchor);
  };
  // URL 输入右键 → 编辑命令菜单（_layout.tsx:1859-1932）
  profile_card_->on_url_context_menu = [this](PointF anchor) { open_url_menu(anchor); };
  // 编辑弹窗保存（← _layout.tsx:1016-1039 handleSaveProfile → patch_profile）
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
  // 接管三段 → patch_verge_config(enable_system_proxy/enable_tun_mode)
  takeover_card_->seg_->apply = [this](const std::string& mode) {
    bool sys = mode == "SYS", tun = mode == "TUN";
    CWLOG_INFO("[TAKEOVER] apply mode=" + mode + " sys=" + (sys ? "1" : "0") +
               " tun=" + (tun ? "1" : "0") +
               " patch_verge=" + (on_patch_verge ? "set" : "NULL"));
    if (on_patch_verge) {
      on_patch_verge("enable_system_proxy", sys);
      on_patch_verge("enable_tun_mode", tun);
    }
    return true;
  };
  // 代理方式三段 → rule_fallback + clash mode
  //（全局代理→global；GFWList→rule；添加网址→rule）
  // ← _layout.tsx:391-424：索引 0=proxy(全局代理)、1=direct(GFWList)、
  //   2=addurl(添加网址)；三种情况统一 patchClashMode('rule')，第三段再开弹窗
  routing_card_->seg_->on_change = [this](int i) {
    static const char* fb[] = {"proxy", "direct", "addurl"};
    if (on_patch_verge) on_patch_verge("rule_fallback", fb[i]);
    if (on_patch_clash_mode) on_patch_clash_mode("rule");
    if (i == 2 && on_add_rule) on_add_rule();
  };
  // 2×2 开关
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
  // Mixed Port：Enter / 失焦提交（Mini 无保存钮；src: basic-settings-card.tsx:232）
  auto commit_port = [this] {
    std::wstring t = basic_card_->port_field_->text();
    if (t.empty()) return;
    int port = _wtoi(t.c_str());
    // src: use-minimal-settings.ts:87-104 无取值范围校验（仅占用检查 + 变更判断）
    if (on_patch_verge) on_patch_verge("verge_mixed_port", port);
  };
  basic_card_->port_field_->on_submit = commit_port;
  basic_card_->port_field_->on_focus_lost = commit_port;
  basic_card_->save_btn_->on_click = commit_port;  // 隐藏件兼容测试
  // 皮肤条（隐藏件；保留接线）
  static const char* skin_names[] = {"retro-3d", "original", "modern-flat",
                                     "frosted-glass", "cyberpunk",
                                     "monochrome"};
  (void)skin_names;
  for (size_t i = 0; i < theme_card_->skin_buttons_.size(); ++i) {
    theme_card_->skin_buttons_[i]->on_click = [this, i]() {
      select_skin(static_cast<int>(i));
    };
  }
  // 底部行 Language（_layout.tsx:1556-1571 Select，13 项）
  if (lang_btn_ && lang_menu_) {
    static const wchar_t* kLangLabels[13] = {
        L"简体中文", L"English", L"Русский",  L"فارسی",    L"Татарча",
        // src: _layout.tsx:1628 <MenuItem value="id">Bahasa Indonesia</MenuItem>
        L"Bahasa Indonesia", L"العربية", L"한국어", L"Türkçe", L"Deutsch",
        L"Español",  L"日本語",   L"繁體中文"};
    lang_btn_->on_click = [this]() {
      // src: _layout.tsx:1550-1558 MUI Select 的 MenuItem 自动带 selected
      //   （selected = 当前 language）
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
      RectF gb = lang_btn_->global_bounds();
      // 菜单向上弹出：13 项 × 48 + pad 2×2 = 628，锚点抬到按钮上方
      lang_menu_->open(items, {gb.x, gb.y - 628.0f - 4.0f});
    };
    lang_menu_->on_select = [this](int i) {
      static const char* codes[13] = {"zh",  "en", "ru",  "fa",   "tt",
                                      "id",  "ar", "ko",  "tr",   "de",
                                      "es",  "jp", "zhtw"};
      if (i < 0 || i >= 13) return;
      i18n::I18n::instance().set_language(codes[i]);
      if (on_patch_verge) on_patch_verge("language", std::string(codes[i]));
    };
  }
  // 主题三段 → ThemeService.set_mode + 持久化
  theme_card_->mode_seg_->on_change = [this](int i) {
    auto& svc = ThemeService::instance();
    bool dark = i == 2 || (i == 0 && system_is_dark());
    svc.set_mode(dark ? theme::ThemeMode::Dark : theme::ThemeMode::Light);
    SkinPrefs p = load_skin_prefs();
    p.theme_mode = i == 1 ? "light" : (i == 2 ? "dark" : "sys");
    save_skin_prefs(p);
    restyle_cards();  // Label 缓存色需随 mode 重刷
    if (on_patch_verge)
      on_patch_verge("theme_mode", i == 1 ? "light" : (i == 2 ? "dark" : "system"));
  };
  // Depth/Vibrancy 滑块
  theme_card_->depth_slider_->on_change = [this](float v) {
    auto& svc = ThemeService::instance();
    svc.set_factors(v, svc.vibrancy_factor());
    if (theme_card_->depth_value_)
      theme_card_->depth_value_->set_text(fmt_factor(v));
    SkinPrefs p = load_skin_prefs();
    p.depth = v;
    save_skin_prefs(p);
  };
  theme_card_->vibrancy_slider_->on_change = [this](float v) {
    auto& svc = ThemeService::instance();
    svc.set_factors(svc.depth_factor(), v);
    if (theme_card_->vibrancy_value_)
      theme_card_->vibrancy_value_->set_text(fmt_factor(v));
    SkinPrefs p = load_skin_prefs();
    p.vibrancy = v;
    save_skin_prefs(p);
  };
}

std::wstring SettingsDrawer::fmt_factor(float v) {
  wchar_t buf[16];
  swprintf(buf, 16, L"%.1f", v);
  return buf;
}

void SettingsDrawer::on_paint(IRenderTarget& rt) {
  if (!open_) return;
  const RectF area{0, 0, bounds().w, bounds().h};
  // theme-panel 底 + 4px double 金边
  draw_panel_bg(rt, area);
  // 左列 borderRight 1px divider（_layout.tsx:1436）
  const float div_x = kPad + kLeftW;
  rt.fill_rect({div_x, kPad, 1.0f, area.h - kPad * 2}, Brush{divider_color()});
  // 四张渐变卡（顺序与 _layout.tsx:1440-1516 一致）；
  // 订阅卡自绘底（ProfileImportCard::on_paint，避免重复画 bevel）
  draw_card_bg(rt, combo_rect_);
  draw_card_bg(rt, basic_rect_);
  draw_card_bg(rt, theme_rect_);
}

}  // namespace clashwin::ui::scene
