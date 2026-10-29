#pragma once
// 设置抽屉（T17）——窄窗单列全屏覆盖（← clashwin_drawer.png 基准）：
// 遮罩 + 右上 ✕ + 五卡纵排（订阅与节点配置/流量接管模式/分流路由偏好/
// 基础设置/界面外观与物质质感）+ 底部按钮行（日志/帮助）。
// 大窗两列形态（左 240px+右自适应）留 T20 连接面板对接（census §2）。
// 命令链：TakeoverSegmented→patch_verge_config(enable_system_proxy/
//   enable_tun_mode)+restartCore(T18)；分流三段→patch_verge_config(
//   rule_fallback)+patch_clash_mode("rule")；2×2 开关→enable_auto_launch/
//   enable_silent_start（verge）+allow-lan/ipv6（patch_clash_config）；
//   Mixed Port→verge_mixed_port；皮肤/亮暗/语言→ThemeService+i18n+patch；
//   Depth/Vibrancy→ThemeService.set_factors。
// 皮肤/因子持久化：app_home\clashwin-skin.json（原版为 localStorage——
// 差异说明见报告；verge.yaml 结构保持与原版 serde 一致不扩字段）。
#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "../button.h"
#include "../container.h"
#include "../dropdown.h"
#include "../icon_w.h"
#include "../label.h"
#include "../scroll_list.h"
#include "../segmented.h"
#include "../slider.h"
#include "../switch_w.h"
#include "../textfield.h"
#include "../widget.h"
#include "profile_import_card.h"
#include "quick_bar.h"  // TakeoverSegmented（T13）
#include "connections_panel.h"

namespace clashwin::ui::scene {

// 皮肤/因子持久化（clashwin-skin.json）
struct SkinPrefs {
  std::string skin = "retro-3d";
  std::string theme_mode = "sys";  // sys/light/dark
  float depth = 0.3f;
  float vibrancy = 1.0f;
};
SkinPrefs load_skin_prefs();
void save_skin_prefs(const SkinPrefs& p);

// AccentColor（T17 接 T12 遗留）：DWM ColorizationColor → RGB
std::optional<uint32_t> system_accent_color_rgb();

// ---------- 卡片基类（橙标题 + 面板底） ----------
class PanelCard : public Widget {
 public:
  explicit PanelCard(const std::wstring& title, Widget* parent = nullptr);
  void on_paint(IRenderTarget& rt) override;
  void on_arrange(const RectF& area) override;
  // 内容区布局（子类填子项；area 为卡内容局部）
  virtual void arrange_content(const RectF& content) = 0;
  float content_height() const { return content_h_; }
  void set_content_height(float h) { content_h_ = h; }

 protected:
  Label* title_ = nullptr;
  float content_h_ = 60;
};

// ---------- 流量接管模式卡 ----------
class TakeoverCard : public PanelCard {
 public:
  TakeoverCard();
  void arrange_content(const RectF& content) override;
  TakeoverSegmented* seg_ = nullptr;  // wire_callbacks 接线（Drawer 访问）
  Label* hint_ = nullptr;
  Button* unlock_btn_ = nullptr;      // 流媒体/AI 解锁检测入口
  std::function<void()> on_unlock;
};

// ---------- 分流路由偏好卡 ----------
class RoutingCard : public PanelCard {
 public:
  RoutingCard();
  void arrange_content(const RectF& content) override;
  Segmented* seg_ = nullptr;
  Label* hint_ = nullptr;
  Button* add_rule_btn_ = nullptr;    // ＋添加自定义分流规则
  std::function<void()> on_add_rule;
};

// ---------- 基础设置卡（2×2 开关 + Mixed Port） ----------
class BasicCard : public PanelCard {
 public:
  BasicCard();
  void arrange_content(const RectF& content) override;
  SwitchW* sw_auto_launch_ = nullptr;
  SwitchW* sw_allow_lan_ = nullptr;
  SwitchW* sw_silent_start_ = nullptr;
  SwitchW* sw_allow_ipv6_ = nullptr;
  TextField* port_field_ = nullptr;
  Button* save_btn_ = nullptr;
  Label* labels_[4] = {nullptr, nullptr, nullptr, nullptr};
};

// ---------- 界面外观与物质质感卡 ----------
class ThemeCard : public PanelCard {
 public:
  ThemeCard();
  void arrange_content(const RectF& area) override;
  // 皮肤条：2×3 按钮格（3D/Org/Mod/Fro/Cyber/Mono）
  std::vector<Button*> skin_buttons_;
  Segmented* mode_seg_ = nullptr;   // SYS/LIGHT/DARK
  Slider* depth_slider_ = nullptr;  // 0~2 step 0.1
  Slider* vibrancy_slider_ = nullptr;
  Label* depth_value_ = nullptr;
  Label* vibrancy_value_ = nullptr;
  Label* depth_label_ = nullptr;
  Label* vibrancy_label_ = nullptr;
};

// ---------- 抽屉容器 ----------
class SettingsDrawer : public Stack {
 public:
  explicit SettingsDrawer(Widget* parent = nullptr);
  ~SettingsDrawer() override;

  void set_open(bool open);  // 遮罩+面板显隐（滑入动画 200ms 骨架=直接显隐）
  bool is_open() const { return open_; }

  // 卡访问（测试/接线）
  ProfileImportCard* profile_card() const { return profile_card_; }
  TakeoverCard* takeover_card() const { return takeover_card_; }
  RoutingCard* routing_card() const { return routing_card_; }
  BasicCard* basic_card() const { return basic_card_; }
  ThemeCard* theme_card() const { return theme_card_; }
  ConnectionsPanel* conn_panel() const { return conn_panel_; }
  Widget* panel() const { return panel_; }  // 面板（测试：顶部标题栏留白）

  // 宿主回调（命令链）
  std::function<void(const std::string& patch_key, const nlohmann::json& v)>
      on_patch_verge;
  std::function<void(const std::string& mode)> on_patch_clash_mode;
  std::function<bool(const std::string& url, const std::string& name)>
      on_import_profile;
  std::function<void()> on_open_logs;
  std::function<void()> on_open_help;
  std::function<void()> on_add_rule;
  std::function<void()> on_unlock_check;
  std::function<void(bool open)> on_open_changed;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;  // 遮罩点击关闭

  // 回调接线（宿主 set 回调后调用一次；幂等）
  void wire_callbacks();
  // 恢复上次皮肤/因子偏好（clashwin-skin.json；宿主装配时调用一次）
  void restore_prefs() { apply_skin_prefs(); }
  static std::wstring fmt_factor(float v);

 private:
  void build_cards();
  void apply_skin_prefs();

  Widget* mask_ = nullptr;
  Column* panel_ = nullptr;
  ConnectionsPanel* conn_panel_ = nullptr;
  IconW* close_btn_ = nullptr;
  ProfileImportCard* profile_card_ = nullptr;
  TakeoverCard* takeover_card_ = nullptr;
  RoutingCard* routing_card_ = nullptr;
  BasicCard* basic_card_ = nullptr;
  ThemeCard* theme_card_ = nullptr;
  Button* logs_btn_ = nullptr;
  Button* help_btn_ = nullptr;
  ScrollList* scroller_ = nullptr;
  bool open_ = false;
  float scroll_y_ = 0;
  float content_h_ = 0;
};

}  // namespace clashwin::ui::scene
