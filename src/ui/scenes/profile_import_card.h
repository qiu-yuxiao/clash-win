#pragma once
// 订阅导入卡（T15）：URL 输入 + 导入按钮 + 订阅列表 + 右键动作接口。
// src: pages/_layout/components/profile-import-card.tsx:94-369
//   Box p1 + get3DCardStyle(default)；标题 13px bold；单个 URL TextField；
//   居中 contained primary 按钮 h24 px2 fs12；列表 maxHeight 110 gap4；
//   行 p6/8、radius6、左 3px primary、名称 13px(激活 600)、动作图标 12px、
//   第二行 11px text.secondary、进度条 h2 primary。
// 数据对接：import_profile 命令（ValidationOutcome 四分类通知由命令链路发）。
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "clashwin/config/models.h"
#include "../button.h"
#include "../container.h"
#include "../label.h"
#include "../scroll_list.h"
#include "../textfield.h"
#include "../widget.h"

namespace clashwin::ui::scene {

inline constexpr float kImportCardW = 240;  // ← 抽屉左列 240px（_layout.tsx:1428-1429）

// 行可视高 = 53（src: profile-import-card.tsx:217-366 推导：
//   p6 + 标题行 18 + mt2 + 次行 15 + mt4 + 进度 2 + p6 = 53）
// 步进 = 53 + mb 0.5(4px) + 列表 gap 0.5(4px) = 61
//   （src: :196 Stack gap 0.5 与 :217-238 行 mb 0.5 同时生效）
inline constexpr float kProfileRowH = 61;
inline constexpr float kProfileRowVisualH = 53;
inline constexpr float kProfileListMaxH = 110;  // src: :196 maxHeight 110
// 卡固定部分 = p8 + 标题行20.4 + mb6 + 输入 46 + gap4 + 按钮24 + mb8 + p8 = 124.4
// src: profile-import-card.tsx:145-165 multiline 2 行高（py4 + 2×13px*1.4375 + py4 ≈ 46）
inline constexpr float kProfileCardChromeH = 124.4f;

// 单条订阅行（src: profile-import-card.tsx:217-366）
class ProfileRowWidget : public Widget {
 public:
  void bind(const config::PrfItem& item, bool active);
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;

  const std::string& uid() const { return uid_; }
  const std::string& display_name() const { return name_; }
  bool active() const { return active_; }

  std::function<void()> on_select;
  std::function<void()> on_update;
  std::function<void()> on_delete;
  std::function<void(PointF)> on_context;  // 右键：参数为窗口坐标系锚点

 private:
  std::string uid_;
  std::string name_;
  std::string type_;
  std::string used_;      // 已用 / 总量（第二行左）
  std::string date_;      // 到期日或更新日（第二行右）
  bool active_ = false;
  // src: :205 isHighlighted = isActive || uid === 'L_Direct_Imports'
  bool highlighted_ = false;
  bool has_extra_ = false;
  bool has_line2_ = false;  // remote 有 extra / local 才有第二行（:297-342）
  int progress_ = 0;
  bool has_total_ = false;  // src: :345 total > 0 才显示进度条
  bool hover_row_ = false;
  bool hover_update_ = false;
  bool hover_delete_ = false;
};

// 导入卡（卡片本体；抽屉内纵向布局：标题/URL/按钮/列表）
class ProfileImportCard : public Widget {
 public:
  explicit ProfileImportCard(Widget* parent = nullptr);
  ~ProfileImportCard() override;

  std::string url() const;      // trim 后
  std::string name() const;     // 源码无名称输入，保留接口恒为空
  bool url_valid() const;       // http(s) 前缀粗校验（SSRF 细校验在命令链路）

  // 导入回调（宿主接 CommandService::invoke("import_profile")；四分类
  // 通知已在命令链路 emit）
  std::function<bool(const std::string& url, const std::string& name)>
      on_import;
  std::function<void(const std::string& uid)> on_select_profile;
  std::function<void(const std::string& uid)> on_update_profile;
  std::function<void(const std::string& uid)> on_delete_profile;
  // 右键菜单：uid + 窗口坐标锚点（Mini 菜单锚定鼠标，_layout.tsx:1804）
  std::function<void(const std::string& uid, PointF anchor)> on_context_menu;
  // URL 输入框右键菜单（_layout.tsx:1859-1932 Import Input Context Menu）
  std::function<void(PointF anchor)> on_url_context_menu;

  // 按 uid 查订阅项（菜单 disabled 判定/编辑回填用）
  const config::PrfItem* find_item(const std::string& uid) const;

  // 列表数据（只展示 remote/local；内部 merge/script 等子项不显示）
  void set_profiles(const std::vector<config::PrfItem>& items,
                    const std::string& current_uid);
  void reload_profiles();
  int profile_count() const { return static_cast<int>(profiles_.size()); }
  ScrollList* list() const { return list_; }
  ProfileRowWidget* row_at(int i) const;

  // 源码卡高为固定内容高（flexShrink:0），列表 maxHeight 110（:194-201）
  float preferred_height() const;

  void on_paint(IRenderTarget& rt) override;
  void on_arrange(const RectF& area) override;

  TextField* url_field() const { return url_field_; }
  // 标题色覆盖（缺省 text.primary；retro-3d 暗色为 #2C1F03，:121）
  void set_title_color(const Color4f& c) { title_color_ = c; }

 private:
  void do_import();
  void rebuild_rows();

  TextField* url_field_ = nullptr;
  Button* import_btn_ = nullptr;
  Label* title_ = nullptr;
  ScrollList* list_ = nullptr;
  std::optional<Color4f> title_color_;

  std::vector<config::PrfItem> profiles_;
  std::string current_uid_;
};

}  // namespace clashwin::ui::scene
