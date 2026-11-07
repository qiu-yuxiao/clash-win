#pragma once
// ThemeService（T12）：当前皮肤 + 亮暗 + 主色解析 + 控件族绘制参数 + 热切换。
// - 主色解析链（← use-custom-theme.ts:170-181）：
//   setting.primary_color || 系统 AccentColor || 色板缺省；
//   original 皮肤强制 hsl(239*val2, 26%, 49%)
// - 控件族参数 ← utils/button-styles.ts get3DButtonStyle/get3DSegmented*/
//   get3DInputStyle 按 skin×mode 查表（渐变/多层阴影 → C++ 离散色近似，
//   差异逐项标注 theme.cpp）
// - 切换热生效：subscribe 回调 → 控件 invalidate（对齐 clash-mini-skin-changed）
// - 持久化键（← localStorage）：clash-mini-control-skin / theme_mode 归 T17 接
//   verge patch（本层只暴露 set_skin/set_mode）
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "palettes.h"
#include "../render_target.h"  // GradientStop（F-D02 渐变站）

namespace clashwin::ui::theme {

// ---------- 控件族绘制参数 ----------
struct ButtonPaint {
  Color4f fill, fill_hover, fill_pressed, fill_disabled;
  Color4f text, text_disabled;
  Color4f border;       // a=0 表示无边框
  float border_w = 1;
  float radius = 4;
  int weight = 500;     // 字重（T12 字体近似：见 theme.cpp 注释）

  // F-D02：3D 渐变 / 浮雕 / 文字阴影 / 位移（对齐 utils/button-styles.ts）
  // retro-3d 用 135° 多停渐变；其余皮肤用单色 fill + 位移。
  bool use_grad = false;                 // true → 用 grad / grad_hover（retro-3d）
  GradientStop grad[6];                  // default 态渐变站
  GradientStop grad_hover[6];            // hover 态渐变站（无单独站时回退 default）
  int grad_count = 0;
  float grad_angle = 135.0f;
  Color4f bevel_dark{0, 0, 0, 0};       // 浮雕暗边（retro bevelShadowDark）
  Color4f bevel_hi{0, 0, 0, 0};         // 浮雕顶高光（白，light .8 / dark .08）
  Color4f glow{0, 0, 0, 0};             // 外发光（vibrancy 调制；retro 金/红，cyber 霓虹）
  float glow_blur = 8.0f;               // 外发光模糊半径（← 0 0 calc(8px*vibrancy)）
  Color4f text_shadow{0, 0, 0, 0};      // 文字阴影色（a=0 表示无）
  float text_shadow_dy = 1.0f;          // 文字阴影纵向偏移（px，负=上）
  float translate_hover = 0;             // hover 位移 translateY（px，负=上移）
  float translate_active = 0;            // active 位移 translateY（px）

  // 外阴影（← button-styles.ts 各皮肤 boxShadow 的常规层；
  //   retro 的多层硬 bevel 由 bevel_dark + depth_factor 单独表达）
  bool has_shadow = false;
  Color4f shadow_color;
  float shadow_dy = 0;
  float shadow_blur = 0;
};

struct SegmentedPaint {
  Color4f container_bg, container_border;   // ← get3DSegmentedContainerStyle
  Color4f active_bg, active_border;         // ← get3DSegmentedActiveStyle
  Color4f active_text;                      // ← get3DSegmentedActiveTextColor
  Color4f text;                             // 未选中文字（text.primary）
  float active_radius = 3;
  // 容器圆角（← :954 original calc(4px * val1)）；0 = 用控件自身 radius_
  float container_radius = 0.0f;

  // 选中块渐变（← :1005 retro-3d 'linear-gradient(135deg, #FFE082 0%, …)'）；
  // grad_count=0 → 用纯色 active_bg（其余 5 套皮肤）
  GradientStop active_grad[6];
  int active_grad_count = 0;
  float active_grad_angle = 135.0f;

  // 容器 inset 内凹阴影（← :934-938 retro-3d inset 0 2.5d 4.5d / 0 3d 5d）
  bool container_inset = false;
  Color4f container_inset_color;
  float container_inset_dy = 0;
  float container_inset_blur = 0;
  // 容器顶部高光线（← 0 1px 0 rgba(255,255,255,.8|.08)）
  Color4f container_top_hi{0, 0, 0, 0};

  // 选中块硬浮雕（← :1009-1020 retro-3d 0 1d 0 / 0 2d 0 两层实影）
  bool active_has_bevel = false;
  Color4f active_bevel_dark;
  float active_bevel_dy = 0;
  // 选中块外辉光（← retro-3d 0 0 6v/8v 金；cyberpunk 0 0 8d 霓虹）
  bool active_has_glow = false;
  Color4f active_glow_color;
  float active_glow_blur = 0;
};

// ---------- 输入框（get3DInputStyle）绘制参数 ----------
// 出处：utils/button-styles.ts:734-927 get3DInputStyle(theme)：6 套皮肤的底色、
// 描边宽（retro = calc(2px * depth)、monochrome = calc(1px * vibrancy)）、
// 圆角（original calc(4px*val1) / modern calc(6px*val1)）、inset 内凹阴影
// （retro inset 0 3d 5d / modern inset 0 v 3v / cyber light inset 0 1px 3px）
// 与聚焦外发光（retro 0 0 8v 金、cyber 0 0 0 3px 粉）全部在此表达。
struct InputPaint {
  Color4f bg, border, border_focused;
  float border_w = 1;            // 已含 depth/vibrancy
  float border_w_focused = 1;    // 已含 depth/vibrancy
  float radius = 4;              // 已含 val1
  Color4f text, placeholder;

  // inset 内凹阴影（CSS inset 0 dy blur color）；has_inset=false 表示无
  bool has_inset = false;
  Color4f inset_color;
  float inset_dy = 0;
  float inset_blur = 0;
  Color4f inset_top_hi{0, 0, 0, 0};  // 附带 0 1px 0 rgba(255,255,255,.8) 顶高光

  // 聚焦外发光（CSS 0 0 blur color）
  bool focus_glow = false;
  Color4f focus_glow_color;
  float focus_glow_blur = 0;
  // 聚焦硬环（cyberpunk light: 0 0 0 3px rgba(255,0,85,.15)）
  float focus_ring = 0;
};

// ---------- 容器（.theme-panel）绘制参数 ----------
// 6 套皮肤的容器差异全部集中在这里，调用方只按参数绘制，不再各自 if(skin)。
// 出处：index.scss:74-84（基线 .theme-panel）+ 各皮肤 override
//   retro-3d :123-142/:145-162、modern-flat :165-183、frosted-glass :185-198、
//   cyberpunk :231-255、monochrome :257-296、original :298-329
// 两个滑块（--control-skin-val1=深度 / --control-skin-val2=活力）在这里换算成
// 圆角倍数、阴影位移/模糊、发光强度与脉冲周期。
struct PanelPaint {
  // 背景
  bool grad_bg = false;
  GradientStop grad[3] = {};
  int grad_count = 0;
  float grad_angle = 135.0f;
  Color4f bg;  // 非渐变底色；frosted 时 a = 0.02 + 0.1*depth

  // 边框：none / 4px double / 玻璃双线 / 霓虹双线
  enum class BorderKind { None, Double, DoubleGlass, DoubleNeon };
  BorderKind border = BorderKind::Double;
  Color4f border_color;

  float radius = 8.0f;  // ← utils/button-styles.ts:598/639/661（card 8px×val1），已含 skin 的 base×depth

  // 外阴影（多层等效，见 ui/soft_shadow.h）
  bool has_shadow = false;
  Color4f shadow_color;
  float shadow_dy = 0.0f;
  float shadow_blur = 0.0f;

  // 外发光（retro 金色 / cyberpunk 霓虹）
  bool has_glow = false;
  Color4f glow_color;
  float glow_blur = 0.0f;

  // 内浮雕（← retro-3d .theme-panel 的 inset boxShadow 两层）
  //   inset 1d 1d 2d rgba(255,255,255,.2d)  +  inset -1d -1d 2d rgba(0,0,0,.55d)
  bool has_inner_bevel = false;
  Color4f inner_hi;
  Color4f inner_dark;
  float inner_blur = 2.0f;

  // 霓虹呼吸（cyberpunk：period = 2.5s / vibrancy）
  bool pulse = false;
  Color4f pulse_from, pulse_to;
  float pulse_period_s = 2.5f;
};

// ---------- 卡片（get3DCardStyle）绘制参数 ----------
// 出处：utils/button-styles.ts:492-731 get3DCardStyle(theme, cardType)
// 4 种卡型 × 6 套皮肤。Win 侧此前只有一套手写金色渐变（quick_bar /
// settings_drawer / traffic_pane / 各弹窗），切换皮肤时卡片仍是金色 —— 这是
// "只移植了一套皮肤"的实感来源。所有卡片绘制统一改吃本结构。
struct CardPaint {
  bool grad_bg = false;
  GradientStop grad[6] = {};
  int grad_count = 0;
  float grad_angle = 135.0f;
  Color4f bg;  // 非渐变底色
  Color4f border;
  float border_w = 0.0f;  // 0 = 无边框
  float radius = 6.0f;    // ← utils/button-styles.ts:79（button 6px），已含 skin 的 base×depth
  bool has_shadow = false;
  Color4f shadow_color;
  float shadow_dy = 0.0f;
  float shadow_blur = 0.0f;
  bool has_glow = false;  // retro 金 glow / cyberpunk 霓虹
  Color4f glow_color;
  float glow_blur = 0.0f;
  Color4f text_color;  // 卡面文字色（各皮肤不同）
  // 底缘 1px 实线（← retro-3d 专属：boxShadow 首层 '0 1px 0 0 ${bevelShadowDark}'
  //   ，light #B8860B / dark #755500；其余 5 套皮肤的 boxShadow 无此层）
  bool has_bottom_bevel = false;
  Color4f bottom_bevel_color;
  bool pulse = false;
  Color4f pulse_from, pulse_to;
  float pulse_period_s = 2.5f;
};

// ---------- 滑块（get3DSliderStyle）绘制参数 ----------
// 出处：pages/_layout/utils/style-helpers.tsx:241-467 get3DSliderStyle(theme,mode)
// 6 套皮肤的轨厚（retro 5 / original·modern·frosted 4 / cyber·mono 2）、
// 滑块尺寸（14 / 12 / 12 / 12 / 8×12 / 10×10）与圆角（retro 2.5、cyber 与
// monochrome 0）、渐变（retro 轨 to bottom 三停 + thumb radial 四停）、
// 描边与阴影全部在此表达。
struct SliderPaint {
  float thickness = 4;          // MuiSlider-rail / track height
  float thumb_w = 12, thumb_h = 12, thumb_radius = 6;

  Color4f rail;                 // 滑轨底色（retro 已乘 .85 opacity）
  Color4f rail_border;          // a=0 表示无边框
  float rail_bw = 0;
  float rail_radius = 2;
  bool rail_inset = false;      // retro inset 0 3 5 rgba(0,0,0,.3)
  Color4f rail_inset_color;
  float rail_inset_dy = 0;
  float rail_inset_blur = 0;
  Color4f rail_top_hi{0, 0, 0, 0};  // 0 1px 0 rgba(255,255,255,.8) 顶高光

  bool track_grad = false;      // retro to bottom 三停
  GradientStop track_grad_stops[3] = {};
  int track_grad_n = 0;
  Color4f track;                // 纯色填充段
  float track_radius = 2;

  bool thumb_grad = false;      // retro radial 四停
  GradientStop thumb_stops[4] = {};
  int thumb_grad_n = 0;
  float thumb_grad_cx = 0.35f, thumb_grad_cy = 0.35f;
  Color4f thumb;
  Color4f thumb_border;
  float thumb_bw = 0;
  bool thumb_shadow = false;
  Color4f thumb_shadow_color;
  float thumb_shadow_dy = 0;
  float thumb_shadow_blur = 0;
  bool thumb_glow = false;
  Color4f thumb_glow_color;
  float thumb_glow_blur = 0;
  // ★ thumb 内凹高光（← style-helpers.tsx:285/288 `inset 0 calc(1px*d) 0 rgba(255,255,255,.8|.4)`）
  //   retro-3d 滑块旋钮顶部的内高光 —— 缺了旋钮会显得扁平（无实体感）
  bool thumb_inset_hi = false;
  Color4f thumb_inset_hi_color;
  float thumb_inset_hi_dy = 0;
  // ★ thumb 按下内凹（← style-helpers.tsx:306-307 `:active` 的 inset 1d/1d/2d rgba(0,0,0,.15|.5)）
  bool thumb_active_inset = false;
  Color4f thumb_active_inset_color;
  float thumb_active_inset_dx = 0;
  float thumb_active_inset_dy = 0;
  float thumb_active_inset_blur = 0;

  // ★ thumb 悬停态（← style-helpers.tsx:292 `&:hover, &.Mui-focusVisible`）
  //   retro-3d: transform scale(1.2) + filter brightness(1.15)
  //             + boxShadow 0 5d|6d 10d|12d rgba(0,0,0,.35|.7)
  //                       + inset 0 1d 0 rgba(255,255,255,.9|.5)
  //                       + 0 0 12v|14v rgba(255,193,7,.35v|.4v)
  //   original / modern-flat / frosted-glass / cyberpunk: transform scale(1.15)
  //   frosted-glass 另加 bgcolor rgba(255,255,255,1.0)（常态 .9|.8）
  //   cyberpunk 另加 boxShadow 0 0 8px <主色>（常态 0 0 5px）
  bool thumb_hover = false;
  float thumb_hover_scale = 1.0f;      // transform: scale()
  float thumb_hover_brightness = 1.0f; // filter: brightness()
  bool thumb_hover_shadow = false;      // 覆盖常态 thumb_shadow
  Color4f thumb_hover_shadow_color;
  float thumb_hover_shadow_dy = 0;
  float thumb_hover_shadow_blur = 0;
  bool thumb_hover_glow = false;        // 覆盖常态 thumb_glow
  Color4f thumb_hover_glow_color;
  float thumb_hover_glow_blur = 0;
  bool thumb_hover_inset_hi = false;    // 覆盖常态 thumb_inset_hi
  Color4f thumb_hover_inset_hi_color;
  float thumb_hover_inset_hi_dy = 0;
  bool thumb_hover_fill = false;        // 覆盖常态 thumb 底色（frosted-glass 悬停变实心白）
  Color4f thumb_hover_fill_color;
};

// ---------- 开关（base-switch.tsx）绘制参数 ----------
// 出处：components/base/base-switch.tsx:15-556 的 6 套皮肤 styled() 定义。
// 尺寸差异（retro/original/modern 28×14、frosted 28×14 且 thumb 12、
// cyberpunk 18×10、monochrome 28×14 且 thumb 12）、轨道底/边/渐变、
// 旋钮底/边/径向渐变与阴影全部在此表达。
struct SwitchPaint {
  float track_w = 28, track_h = 14, track_radius = 7;

  // 轨道（.MuiSwitch-track）底/边
  Color4f off_bg, off_border;
  float off_bw = 0;
  // 轨道内凹阴影（CSS inset 0 dy blur color）—— retro-3d「滑槽可见」的关键：
  //   槽底半透明金色叠在同色卡片底上几乎无对比，真正的立体感来自这层 inset
  //   暗影 + 底部 1px 高光线（src: base-switch.tsx:103-115）。
  bool off_inset = false;
  Color4f off_inset_color;
  float off_inset_dy = 0;
  float off_inset_blur = 0;
  // 轨道底部高光线（CSS 0 1px 0 color）：1px 线画在槽底缘
  bool off_bottom_hi = false;
  Color4f off_bottom_hi_color;
  float off_bottom_hi_w = 1.0f;

  bool on_grad = false;
  GradientStop on_grad_stops[3] = {};
  int on_grad_n = 0;
  Color4f on_bg, on_border;
  float on_bw = 0;
  // on 态轨道同样带 inset + 底部高光（src: base-switch.tsx:60-79）
  bool on_inset = false;
  Color4f on_inset_color;
  float on_inset_dy = 0;
  float on_inset_blur = 0;
  bool on_bottom_hi = false;
  Color4f on_bottom_hi_color;
  float on_bottom_hi_w = 1.0f;

  float thumb_d = 14, thumb_margin = 0, thumb_radius = 7;

  bool off_thumb_grad = false;
  GradientStop off_thumb_stops[4] = {};
  int off_thumb_n = 0;
  Color4f off_thumb, off_thumb_border;
  float off_thumb_bw = 0;
  // 旋钮内凹浮雕（CSS inset 0 dy 0 color）—— 旋钮自身的立体感
  bool off_thumb_inset_hi = false;
  Color4f off_thumb_inset_hi_color;
  float off_thumb_inset_hi_dy = 0;
  bool off_thumb_inset_dark = false;
  Color4f off_thumb_inset_dark_color;
  float off_thumb_inset_dark_dy = 0;

  bool on_thumb_grad = false;
  GradientStop on_thumb_stops[4] = {};
  int on_thumb_n = 0;
  Color4f on_thumb, on_thumb_border;
  float on_thumb_bw = 0;
  bool on_thumb_inset_hi = false;
  Color4f on_thumb_inset_hi_color;
  float on_thumb_inset_hi_dy = 0;
  bool on_thumb_inset_dark = false;
  Color4f on_thumb_inset_dark_color;
  float on_thumb_inset_dark_dy = 0;

  bool thumb_shadow = false;
  Color4f thumb_shadow_color;
  float thumb_shadow_dy = 0, thumb_shadow_blur = 0;
  bool on_glow = false;
  Color4f on_glow_color;
  float on_glow_blur = 0;
  bool thumb_glow = false;
  Color4f thumb_glow_color;
  float thumb_glow_blur = 0;

  // ★ 旋钮悬停态（← base-switch.tsx 各皮肤 '&:hover .MuiSwitch-thumb'）
  //   retro-3d(:114)      scale(1.2) + brightness(1.15)
  //                        + boxShadow 0 4d|5d 8d|10d rgba(0,0,0,.3|.6)
  //                                + inset 0 1d|1.2d 0 rgba(255,255,255,.8|.6)
  //                                + inset 0 -1d|-1.5d 0 rgba(0,0,0,.1|.4)
  //                                + 0 0 10v rgba(255,193,7,.35v)
  //   original(:271)      无 hover 规则（保持常态）
  //   modern-flat(:361)   scale(1.1)
  //   frosted-glass(:442) 无 hover 规则（保持常态）
  //   cyberpunk(:442)     boxShadow 0 0 10px currentColor（常态 0 0 4px）
  //   monochrome(:543)    scale(1.05)
  bool thumb_hover = false;
  float thumb_hover_scale = 1.0f;      // transform: scale()
  float thumb_hover_brightness = 1.0f; // filter: brightness()
  bool thumb_hover_shadow = false;      // 覆盖常态 thumb_shadow
  Color4f thumb_hover_shadow_color;
  float thumb_hover_shadow_dy = 0;
  float thumb_hover_shadow_blur = 0;
  bool thumb_hover_glow = false;        // 覆盖常态 thumb_glow
  Color4f thumb_hover_glow_color;       // off 态（cyberpunk 用 currentColor=#ff0055）
  Color4f on_thumb_hover_glow_color;     // on 态（cyberpunk #00b0ff|#00f0ff）
  float thumb_hover_glow_blur = 0;
  bool thumb_hover_inset_hi = false;    // 覆盖常态 inset 顶高光
  Color4f thumb_hover_inset_hi_color;
  float thumb_hover_inset_hi_dy = 0;
  bool thumb_hover_inset_dark = false;  // 覆盖常态 inset 底暗线
  Color4f thumb_hover_inset_dark_color;
  float thumb_hover_inset_dark_dy = 0;
};

// ---------- 逐皮肤的滑块规格 ----------
// 出处：pages/_layout/components/theme-settings-card.tsx:59-134
//   getSlider1Label / getSlider1Max / getSlider2Label / getSlider2Max
// Mini 的两个滑块不是固定量程：original 的「Radius」上限 3.0、frosted 的
// 「Opacity」上限 5.0、monochrome 的「Radius」上限 3.0……Win 侧此前写死
// 0~1.0 / 0~5.0，导致切皮肤后滑块拖到头也无法达到该皮肤应有的效果。
struct SkinSliderSpec {
  float max1 = 1.0f;   // 滑块1（val1）上限
  float max2 = 5.0f;   // 滑块2（val2）上限
  const wchar_t* label1 = L"Depth";     // 滑块1 左侧标签
  const wchar_t* label2 = L"Vibrancy";  // 滑块2 左侧标签
};
// 逐皮肤滑块规格（← theme-settings-card.tsx:59-134）
SkinSliderSpec skin_slider_spec(SkinId skin);
// 逐皮肤因子缺省值（← use-skin-controls.ts:13-24 readFactor）
//   只有 retro-3d 走 legacy 键且缺省 0.3/1.0；其余 5 套皮肤缺省均为 1.0/1.0。
//   这一点很关键：Win 侧此前全局用 0.3，切到 original/modern-flat 后
//   `--border-radius: calc(8px * val1)` 会算成 2.4px 而不是 8px。
void skin_default_factors(SkinId skin, float* depth, float* vibrancy);

// ★ §2.7 每皮肤全局字体候选链（← layout.scss 的 --control-font-family）
//   Mini 的 font-family 是**列表**（多字体回退）；DirectWrite 只接受单一
//   family，故保留完整链，由 D2D 层按本机字体存在性择一。
//   各皮肤定义见 layout.scss:324/328/352/371/418/442。
const std::vector<std::wstring>& skin_font_candidates(SkinId skin);

// ---------- 因子语义（← Mini 的四变量注入模型） ----------
// Mini（use-skin-controls.ts:70-93）把两个滑块值注入**四个** CSS 变量：
//   --depth-factor ≡ --control-skin-val1 ≡ val1
//   --vibrancy-factor ≡ --control-skin-val2 ≡ val2
// 但 layout.scss 对两套皮肤做了 **!important 覆盖**：
//   modern-flat（:329）与 original（:443）→ `--depth-factor: 0 !important`
// 后果：这 2 套皮肤的 val1 **只**经 --control-skin-val1 生效（几何/半径），
//       不再驱动 --depth-factor 消费的阴影深度；其余 4 套皮肤 val1 同时
//       驱动几何与深度两个维度。
//   详见 docs/evidence/2026-11-07-skin-slider-semantics.md
//
// 因此 Win 侧必须区分「val1 本身」与「深度语义下的 val1」：
//   val1()     → 永远是滑块的原始值（几何类消费用这个）
//   depth()    → 语义化后的深度值（original/modern-flat 为 0，其余 = val1）
//   vibrancy() → 永远是 val2
struct SkinFactorView {
  float val1 = 1.0f;      // --control-skin-val1（几何类：圆角/半径/不透明度…）
  float depth = 1.0f;     // --depth-factor（深度类：阴影位移/浮雕/辉光…）
  float vibrancy = 1.0f;  // --vibrancy-factor ≡ --control-skin-val2
};

class ThemeService {
 public:
  static ThemeService& instance();

  // init：skin/mode 缺省 retro-3d/light；primary_override =
  // verge theme_setting.primary_color（CSS hex；空=走 AccentColor/色板链）
  void init(SkinId skin = kDefaultSkin, ThemeMode mode = ThemeMode::Light,
            const std::string& primary_override = "");
  // depth/vibrancy（← use-skin-controls readFactor：val1 默认 0.3、val2 默认 1.0）
  void set_factors(float depth, float vibrancy);

  SkinId skin() const { return skin_; }
  ThemeMode mode() const { return mode_; }
  bool is_light() const { return mode_ == ThemeMode::Light; }
  float depth_factor() const { return depth_; }
  float vibrancy_factor() const { return vibrancy_; }

  // 语义化因子视图（← Mini 四变量注入 + layout.scss 的 !important 覆盖）
  //   val1()     → 滑块原值（几何类消费）
  //   depth()    → 深度语义值（original / modern-flat 被强制为 0）
  //   vibrancy() → 滑块 2 原值
  float val1() const { return depth_; }
  float depth() const {
    // layout.scss:329（modern-flat）/ :443（original）→ --depth-factor: 0 !important
    return (skin_ == SkinId::Original || skin_ == SkinId::ModernFlat)
               ? 0.0f
               : depth_;
  }
  float vibrancy() const { return vibrancy_; }
  // 三元组快照（供 paint 表统一取用）
  SkinFactorView factors() const {
    return SkinFactorView{depth_, depth(), vibrancy_};
  }

  const Palette& palette() const { return palette_; }
  const CssVars& vars() const { return vars_; }
  Color4f primary() const { return palette_.primary_color; }
  Color4f text_primary() const { return palette_.primary_text; }

  // ★ §2.7 每皮肤全局字体（← Mini layout.scss:319 --control-font-family）
  //   Mini 通过 CSS 变量把字体作用到 .MuiSlider-root / .MuiInputBase-root /
  //   .MuiTypography-root / .MuiListItem-root / .MuiMenuItem-root /
  //   .MuiListItemButton-root —— 即「除图标字体外的全部文本」。
  //   Win 侧对应：TextStyle::font_family 为空时取本值（跟随皮肤）。
  //   各皮肤定义见 layout.scss:324/328/352/371/418/442。
  //
  //   ui_font() 返回**本机实际生效**的字体名（首选字体缺失时按候选链回退，
  //   由 D2D 层解析后写回）；skin_font_candidates() 是纯候选链。
  const std::wstring& ui_font() const { return ui_font_; }
  // 由 D2D 层在首次需要时解析并回填（存在性检测需要 DirectWrite）
  void set_ui_font(const std::wstring& f) { ui_font_ = f; }

  // 控件族查表
  // outlined=true → 源码 variant==="outlined"（retro-3d 有独立的半透明渐变）
  ButtonPaint button_paint(const char* color_type = "default",
                           bool outlined = false) const;
  SegmentedPaint segmented_paint() const;
  InputPaint input_paint() const;

  // 容器族查表（6 套皮肤 × depth/vibrancy）
  PanelPaint panel_paint() const;
  // card_type ∈ {"default","primary","upload","download"}（← get3DCardStyle）
  CardPaint card_paint(const char* card_type = "default") const;
  // 滑块族查表（6 套皮肤 × depth/vibrancy ← get3DSliderStyle）
  SliderPaint slider_paint() const;
  // 开关族查表（6 套皮肤 ← base-switch.tsx styled()）
  SwitchPaint switch_paint() const;
  // 霓虹呼吸当前相位（0..1，cyberpunk）：其它皮肤恒返回 0
  float pulse_phase(float time_s) const;

  // 热切换（对齐 clash-mini-skin-changed / theme_mode 切换语义）
  void set_skin(SkinId skin);
  void set_mode(ThemeMode mode);
  void set_primary_override(const std::string& css_hex);

  uint64_t subscribe(std::function<void()> cb);
  void unsubscribe(uint64_t id);

 private:
  ThemeService() = default;
  void rebuild();          // palette_/vars_ 重算（original 强制主色等）
  void notify();

  SkinId skin_ = kDefaultSkin;
  ThemeMode mode_ = ThemeMode::Light;
  std::string primary_override_;
  float depth_ = 0.3f;     // ← val1 缺省（depth-factor）
  float vibrancy_ = 1.0f;  // ← val2 缺省（vibrancy-factor）
  Palette palette_{};
  CssVars vars_{};
  // ★ §2.7 每皮肤全局字体（← layout.scss 的 --control-font-family）
  std::wstring ui_font_{L"Segoe UI"};
  std::vector<std::pair<uint64_t, std::function<void()>>> subs_;
  uint64_t next_id_ = 1;
};

}  // namespace clashwin::ui::theme
