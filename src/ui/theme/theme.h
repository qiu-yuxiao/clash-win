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

namespace clashwin::ui::theme {

// ---------- 控件族绘制参数 ----------
struct ButtonPaint {
  Color4f fill, fill_hover, fill_pressed, fill_disabled;
  Color4f text, text_disabled;
  Color4f border;       // a=0 表示无边框
  float border_w = 1;
  float radius = 4;
  int weight = 500;     // 字重（T12 字体近似：见 theme.cpp 注释）
};

struct SegmentedPaint {
  Color4f container_bg, container_border;   // ← get3DSegmentedContainerStyle
  Color4f active_bg, active_border;         // ← get3DSegmentedActiveStyle
  Color4f active_text;                      // ← get3DSegmentedActiveTextColor
  Color4f text;                             // 未选中文字（text.primary）
  float active_radius = 3;
};

struct InputPaint {
  Color4f bg, border, border_focused;
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

  const Palette& palette() const { return palette_; }
  const CssVars& vars() const { return vars_; }
  Color4f primary() const { return palette_.primary_color; }
  Color4f text_primary() const { return palette_.primary_text; }

  // 控件族查表
  ButtonPaint button_paint(const char* color_type = "default") const;
  SegmentedPaint segmented_paint() const;
  InputPaint input_paint() const;

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
  std::vector<std::pair<uint64_t, std::function<void()>>> subs_;
  uint64_t next_id_ = 1;
};

}  // namespace clashwin::ui::theme
