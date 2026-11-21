#pragma once
// 设置抽屉（T17，按 Mini 3.2.2 源码重做，不再用像素反推）：
// 容器 = 内容区全窗覆盖层（_layout.tsx:1403-1424）：flex row、padding 12、gap 12、
//   overflow hidden；无遮罩/无 backdrop（抽屉本身不透明）。
// 左列（_layout.tsx:1428-1437）：flex 0 0 240px、纵向、gap 5、pr 1(=8px)、
//   borderRight 1px theme.palette.divider。宽窗（>285 逻辑）右列 ConnectionsPanel
//   flex:1（_layout.tsx:1519-1533）；窄窗不挂载右列。
// 面板底 theme-panel（index.scss:124-132 / 146-152）：4px double #B8860B、
//   radius 8、135° 3 停靠渐变（light #FFFDE7/#FFF9C4/#FFF59D；
//   dark #1A160E/#2A2413/#17140B）。
// 卡片 get3DCardStyle('default')（button-styles.ts:534-565）：radius 6、
//   135° 4 停靠渐变（light #FFFDE7@0/#FFF9C4@35/#FFF59D@70/#FFE082@100；
//   dark #FFF59D@0/#FBC02D@35/#F57F17@70/#E65100@100）、底缘 bevel 线
//   #B8860B(light)/#755500(dark)、文字 #3C2F0F(light)/#2C1F03(dark)。
// 卡片顺序（_layout.tsx:1440-1516）：订阅导入 → 接管+代理方式合并卡(p:1) →
//   基础设置 → 主题设置。
// 关闭钮（_layout.tsx:1281-1304）：右上 top3 right8、28×28 contained primary、
//   图标 CloseRounded 20px。
// 隐藏件（Mini 无、保留测试兼容）：解锁检测/添加规则/保存/皮肤钮/数值
//   标签/日志/帮助/提示行。
// 命令链不变：接管→patch_verge(enable_system_proxy/enable_tun_mode)；
//   代理方式→rule_fallback+patch_clash_mode；开关→auto_launch/silent/
//   allow-lan/ipv6；Mixed Port→verge_mixed_port（Enter 提交）；
//   主题/滑条→ThemeService+clashwin-skin.json。
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "../button.h"
#include "../container.h"
#include "../context_menu.h"
#include "../dropdown.h"
#include "../icon_w.h"
#include "../label.h"
#include "../overlay_fade.h"
#include "../scroll_list.h"
#include "../segmented.h"
#include "../slider.h"
#include "../switch_w.h"
#include "../textfield.h"
#include "../theme/theme.h"
#include "../widget.h"
#include "profile_import_card.h"
#include "quick_bar.h"  // TakeoverSegmented（T13）
#include "connections_panel.h"

namespace clashwin::ui::scene {

class EditProfileModal;
class DeleteProfileConfirmModal;

// src: src/pages/_layout/utils/settings-guards.ts:7-21 BUG-385
inline bool should_skip_mixed_port_blur_save(bool is_open,
                                             bool skip_requested,
                                             bool focus_on_close_btn) {
  return !is_open || skip_requested || focus_on_close_btn;
}

// 皮肤/因子持久化（clashwin-skin.json）
// 因子按皮肤分别存储（← use-skin-controls.ts:13-24 readFactor：
//   localStorage['clash-mini-{skin}-val1' / '-val2']）。
//   只有 retro-3d 有 legacy 键（'clash-mini-depth-factor' /
//   '-vibrancy-factor'，即下方的 depth/vibrancy 顶层字段）；
//   其余 5 套皮肤无历史值时缺省 1.0/1.0（不是 retro 的 0.3/1.0）。
struct SkinPrefs {
  std::string skin = "retro-3d";
  std::string theme_mode = "dark";  // sys/light/dark；src: verge.rs:345 缺省 "dark"
  float depth = 0.3f;                // legacy（仅 retro-3d 生效）
  float vibrancy = 1.0f;             // legacy（仅 retro-3d 生效）
  // 逐皮肤因子：skin → (val1, val2)
  std::map<std::string, std::pair<float, float>> factors;
};
SkinPrefs load_skin_prefs();
void save_skin_prefs(const SkinPrefs& p);
uint64_t skin_prefs_save_count();
void reset_skin_prefs_save_count();
// 取某皮肤的因子（未保存时按 skin_default_factors 缺省；retro-3d 回落 legacy 顶层字段）
std::pair<float, float> skin_factors(const SkinPrefs& p, const std::string& skin);
// 写入某皮肤的因子
void set_skin_factors(SkinPrefs& p, const std::string& skin, float depth,
                      float vibrancy);

// AccentColor（T17 接 T12 遗留）：DWM ColorizationColor → RGB
std::optional<uint32_t> system_accent_color_rgb();

// ---------- 流量接管模式段（标题 + TakeoverSegmented） ----------
class TakeoverCard : public Widget {
 public:
  TakeoverCard();
  void on_arrange(const RectF& area) override;
  TakeoverSegmented* seg_ = nullptr;  // wire_callbacks 接线（Drawer 访问）
  Label* title_ = nullptr;
  Label* hint_ = nullptr;             // Mini 无（隐藏）
};

// ---------- 代理方式段（标题 + Segmented） ----------
class RoutingCard : public Widget {
 public:
  RoutingCard();
  void on_arrange(const RectF& area) override;
  Segmented* seg_ = nullptr;
  Label* title_ = nullptr;
  Label* hint_ = nullptr;             // Mini 无（隐藏）
  Button* add_rule_btn_ = nullptr;    // Mini 无（隐藏）
  std::function<void()> on_add_rule;
};

// ---------- 基础设置（2×2 开关 + Mixed Port） ----------
class BasicCard : public Widget {
 public:
  BasicCard();
  void on_arrange(const RectF& area) override;
  HCURSOR cursor(const PointF& local) const override;
  bool on_mouse(const MouseEvent& e) override;
  SwitchW* sw_auto_launch_ = nullptr;
  SwitchW* sw_allow_lan_ = nullptr;
  SwitchW* sw_silent_start_ = nullptr;
  SwitchW* sw_allow_ipv6_ = nullptr;
  TextField* port_field_ = nullptr;
  Button* save_btn_ = nullptr;        // Mini 无（Enter 提交；隐藏）
  // 当前生效端口（sync_basic 写入）：保存失败/占用时把输入框回滚到它
  // ← use-minimal-settings.ts:96/:106 setMixedPortVal(currentPort())
  int port_current_ = 0;
  Label* title_ = nullptr;
  Label* sniffer_label_ = nullptr;
  Segmented* sniffer_seg_ = nullptr;
  bool tun_enabled_ = false;
  void set_tun_enabled(bool tun);
  Label* port_label_ = nullptr;
  Label* labels_[4] = {nullptr, nullptr, nullptr, nullptr};
};

// ---------- 主题三段 + Depth/Vibrancy 滑条 ----------
class ThemeCard : public Widget {
 public:
  ThemeCard();
  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;  // 页脚上方 dashed 分隔线
  HCURSOR cursor(const PointF& local) const override;
  bool on_mouse(const MouseEvent& e) override;
  std::vector<Button*> skin_buttons_;  // Mini 无（隐藏保留）
  Label* title_ = nullptr;             // 13px bold（theme-settings-card.tsx:156-167）
  Segmented* mode_seg_ = nullptr;      // 系统/浅色/深色
  Button* logs_btn_ = nullptr;         // 调试日志钮（theme-settings-card.tsx:427-448）
  // src: theme-settings-card.tsx:365-373/414-421 深度 0–1.0、Vibrancy 0–5.0，step .1
  Slider* depth_slider_ = nullptr;     // 0~1.0 step 0.1
  Slider* vibrancy_slider_ = nullptr;  // 0~5.0 step 0.1
  Label* depth_value_ = nullptr;       // 12px bold 数值（源码显示 .toFixed(1)）
  Label* vibrancy_value_ = nullptr;
  Label* depth_label_ = nullptr;       // 13px
  Label* vibrancy_label_ = nullptr;
  Label* copyright_ = nullptr;         // 11px text.secondary
  Label* email_ = nullptr;             // 10px primary.main
  bool footer_hover_ = false;
  bool email_hover_ = false;
};

// ---------- 帮助弹窗（271px 左锚定 Modal Dialog） ----------
// src: src/pages/_layout/components/help-menu-button.tsx:125-273
class HelpDialog : public Widget {
 public:
  explicit HelpDialog(Widget* parent = nullptr);

  void open(const std::vector<MenuItem>& items, std::function<void(int)> on_sel);
  void close();
  bool is_open() const { return open_; }

  RectF card_rect() const { return card_rect_; }
  RectF menu_rect() const { return card_rect_; }
  RectF close_btn_rect() const { return btn_close_rect_; }

  std::function<void()> on_dismiss;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;

  bool hit_self(PointF local) const override {
    return open_ && local.x < std::min(theme::kPopupPaneWidth, bounds().w) &&
           Widget::hit_self(local);
  }
  bool a11y_subtree_visible() const override { return open_ && visible() && !a11y_hidden(); }
  bool a11y_is_modal() const override { return open_; }
  A11yRole a11y_default_role() const override { return A11yRole::Dialog; }
  std::wstring a11y_default_name() const override;

 private:
  bool open_ = false;
  OverlayFade fade_;
  std::vector<MenuItem> items_;
  std::function<void(int)> on_select_;
  std::vector<RectF> item_rects_;
  std::vector<float> div_ys_;
  RectF card_rect_{};
  RectF title_rect_{};
  RectF content_rect_{};
  RectF btn_close_rect_{};
  int hover_idx_ = -1;
  bool hover_close_ = false;
  bool pressed_close_ = false;
};

// ---------- 抽屉容器 ----------
class SettingsDrawer : public Stack {
 public:
  explicit SettingsDrawer(Widget* parent = nullptr);
  ~SettingsDrawer() override;

  void set_open(bool open);  // 面板显隐（无遮罩，源码为条件渲染）
  bool is_open() const { return open_; }
  void set_titlebar_hidden(bool hidden);
  bool titlebar_hidden() const { return titlebar_hidden_; }
  void detach_modals_to(Widget* host_root);
  bool bottom_row_visible_ = false;

  // 卡访问（测试/接线）
  ProfileImportCard* profile_card() const { return profile_card_; }
  TakeoverCard* takeover_card() const { return takeover_card_; }
  RoutingCard* routing_card() const { return routing_card_; }
  BasicCard* basic_card() const { return basic_card_; }
  ThemeCard* theme_card() const { return theme_card_; }
  ConnectionsPanel* conn_panel() const { return conn_panel_; }
  Widget* panel() const { return panel_; }

  // 宿主回调（命令链）
  std::function<void(const std::string& patch_key, const nlohmann::json& v)>
      on_patch_verge;
  std::function<void(const std::string& fb, bool open_add_url_after)>
      on_routing_change_async;
  // 流量接管模式切换（← src/pages/_layout/hooks/use-proxy-mode-actions.ts:92-140 handleTakeoverModeChange）：
  //   宿主负责 isTunModeAvailable 判定 / 试装 service / patchVerge / Notice；
  //   返回 false → 分段不回弹不改态（quick_bar.cpp:635-642 门）+ 宿主已 push 错误。
  //   未接线时回落到 on_patch_verge 旧语义（单测）。
  std::function<bool(const std::string& mode)> on_takeover_change;
  // Mixed Port 保存（← use-minimal-settings.ts:87-107 handleSavePort）：
  // 占用检查 → 同时写内核 mixed-port 与 verge verge_mixed_port →
  // 返回 false 表示失败/被占用，调用方把输入框回滚到当前端口
  std::function<bool(int port)> on_save_port;
  std::function<void(const std::string& mode)> on_patch_clash_mode;
  std::function<bool(const std::string& url, const std::string& name)>
      on_import_profile;
  std::function<void(const std::string& url, const std::string& name,
                     std::function<void(bool ok)> done)>
      on_import_profile_async;
  std::function<void(const std::string& uid)> on_select_profile;
  std::function<void(const std::string& uid)> on_update_profile;
  std::function<void(const std::string& uid)> on_delete_profile;
  // 订阅编辑保存：patch_profile（← src/pages/_layout/hooks/use-profile-actions.ts:412-435）
  std::function<bool(const std::string& uid, const std::string& name,
                     const std::string& url, int interval_mins)>
      on_patch_profile;
  std::function<void()> on_open_logs;
  std::function<void()> on_open_help;
  // 帮助菜单「检查软件更新 / 检查内核更新」→ 宿主（Mini: use-client-update.ts:32-54
  //   / use-core-update.ts:64-86，未设则退回 HelpMenu::execute_action）
  std::function<void()> on_check_app_update;
  std::function<void()> on_check_core_update;
  std::function<void()> on_add_rule;
  std::function<void(bool open)> on_open_changed;
  // BUG-358: 滑条拖动期 200ms 防抖落盘宿主定时器回调（← use-skin-controls.ts:4）
  std::function<void(uint32_t delay_ms)> on_arm_skin_persist_timer;
  std::function<void()> on_cancel_skin_persist_timer;

  void flush_pending_skin_prefs();
  bool has_pending_skin_prefs() const { return skin_prefs_dirty_; }

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;  // 无遮罩：点面板外不关闭（源码）
  // 关闭态不参与命中（宿主常驻可见，保证 Stack 布局能铺满内容区）
  bool hit_self(PointF local) const override {
    return open_ && Widget::hit_self(local);
  }
  bool a11y_subtree_visible() const override {
    return open_ && visible() && !a11y_hidden();
  }
  A11yRole a11y_default_role() const override { return A11yRole::Pane; }
  std::wstring a11y_default_name() const override;

  // 回调接线（宿主 set 回调后调用一次；幂等）
  void wire_callbacks();
  // 恢复上次皮肤/因子偏好（clashwin-skin.json；宿主装配时调用一次）
  void restore_prefs() { apply_skin_prefs(); }
  // 用 verge theme_mode（Mini 的单一事实源）回填主题：system/light/dark。
  // 只同步外观，不触发 patch 回调（避免启动期回写环）。
  void sync_theme_mode(const std::wstring& mode);
  // 主题切换后重着色静态标签（Label 缓存 style.color，不重排则旧色残留）
  void restyle_cards();
  // 底部行六格两态重刷（← src/pages/_layout/components/settings-drawer.tsx:425-487 选中格查当前皮肤）
  void refresh_skin_cells();
  // 皮肤选择（主题卡隐藏钮与底部行共用）
  void select_skin(int i);
  // 皮肤切换/恢复后同步滑块量程（max 逐皮肤）、标签与当前值
  void sync_skin_sliders(theme::SkinId skin, float depth, float vibrancy);

  // 底部行（_layout.tsx:1260-1476）：窗口高 ≥831 DIP 才显示；
  //   左 0..80 = ? 帮助、80..177.5 = Language、177.5..640 = 6 个皮肤格
  Button* bottom_help_ = nullptr;
  Button* lang_btn_ = nullptr;
  ContextMenu* lang_menu_ = nullptr;  // 语言下拉（13 项）
  HelpDialog* help_dialog_ = nullptr;  // 帮助与工具 Modal Dialog
  bool app_checking_ = false;   // F-D05：app 更新检查进行中
  bool core_checking_ = false;  // F-D05：内核更新检查进行中
  HelpDialog* help_menu() const { return help_dialog_; }

  // F-D05：检查更新进行中禁用对应菜单项（对齐 Mini
  //   help-menu-button.tsx:192,200 disabled={clientCheckLoading/coreCheckLoading}）
  void set_app_update_checking(bool checking);
  void set_core_update_checking(bool checking);
  EditProfileModal* edit_profile_modal() const { return edit_profile_modal_; }
  DeleteProfileConfirmModal* delete_confirm_modal() const {
    return delete_confirm_modal_;
  }
  void request_delete_profile(const std::string& uid);
  std::vector<Button*> skin_row_;     // Trump-3D/Original/Modern/Frosted/Cyberpunk/Monochrome
  bool bottom_row_visible() const { return bottom_row_visible_; }
  // 启动期用 verge 实际值回填抽屉控件（不触发 patch，避免启动回写）
  void sync_takeover(bool sys_proxy, bool tun);
  void sync_routing(const std::string& rule_fallback);
  void sync_basic(bool auto_launch, bool silent, bool allow_lan, bool ipv6,
                  int mixed_port,
                  std::optional<bool> tun_sniffer = std::nullopt);
  static std::wstring fmt_factor(float v);

 private:
  void apply_skin_prefs();
  void open_profile_menu(const std::string& uid, PointF anchor);
  void open_url_menu(PointF anchor);  // URL 输入框右键（5 项编辑命令）
  void open_help_menu();
  void open_edit_profile(const std::string& uid);
  std::string edit_profile_uid_;

  Widget* mask_ = nullptr;      // 保留但恒隐藏（源码无 backdrop）
  Widget* panel_ = nullptr;     // 全窗覆盖层（theme-panel 底 + 4px double 边）
  // 卡片流式布局矩形（on_arrange 计算；局部坐标）
  RectF profile_rect_, combo_rect_, basic_rect_, theme_rect_, left_rect_;
  ConnectionsPanel* conn_panel_ = nullptr;
  IconW* close_btn_ = nullptr;
  ProfileImportCard* profile_card_ = nullptr;
  TakeoverCard* takeover_card_ = nullptr;
  RoutingCard* routing_card_ = nullptr;
  BasicCard* basic_card_ = nullptr;
  ThemeCard* theme_card_ = nullptr;
  Button* logs_btn_ = nullptr;   // Mini 无（隐藏）
  Button* help_btn_ = nullptr;   // Mini 无（隐藏）
  ContextMenu* profile_menu_ = nullptr;      // 订阅行右键菜单（_layout.tsx:1806-1856）
  ContextMenu* url_menu_ = nullptr;          // URL 输入右键菜单（_layout.tsx:1859-1932）
  EditProfileModal* edit_profile_modal_ = nullptr;  // 编辑订阅（layout-dialogs.tsx:119-245）
  DeleteProfileConfirmModal* delete_confirm_modal_ = nullptr;  // BUG-390 删除二次确认
  bool open_ = false;
  bool titlebar_hidden_ = false;
  bool skip_port_blur_save_ = false;
  bool close_btn_pressed_ = false;
  bool skin_prefs_dirty_ = false;
  std::string pending_skin_name_;
  float pending_depth_ = 0.3f;     // src: src/pages/_layout/hooks/use-skin-controls.ts:45
  float pending_vibrancy_ = 1.0f;  // src: src/pages/_layout/hooks/use-skin-controls.ts:49
  bool has_last_persisted_ = false;
  std::string last_persisted_skin_;
  float last_persisted_depth_ = 0.0f;
  float last_persisted_vibrancy_ = 0.0f;
};

}  // namespace clashwin::ui::scene
