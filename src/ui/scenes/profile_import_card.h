#pragma once
// 订阅导入卡（T15）：URL 输入 + 导入按钮 + 名称（可选）+ 右键动作接口。
// ← census §2/§3：profile-import-card（订阅导入+列表+右键菜单）位于设置抽屉
//   左列 240px 首位（T17 抽屉容器承载本卡）；右键菜单动作（更新/编辑/删除/
//   置顶）经回调接口暴露，菜单位于 Profile 卡（React ContextMenu 语义）。
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

inline constexpr float kImportCardW = 240;  // ← 抽屉左列 240px 固定宽
inline constexpr float kProfileRowH = 44;   // 列表行高（名称 + 流量/时间 + 动作）

// 单条订阅行（← profile-import-card.tsx 列表项）：
// 左键选中；右侧 ↻ 更新 / ✕ 删除；右键由宿主弹 ContextMenu。
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
  std::function<void()> on_context;

 private:
  std::string uid_;
  std::string name_;
  std::string type_;
  std::string sub_line_;
  bool active_ = false;
  bool has_extra_ = false;
  int progress_ = 0;
  bool hover_update_ = false;
  bool hover_delete_ = false;
};

// 导入卡（卡片本体；抽屉内纵向布局：标题/URL/名称/列表/消息）
class ProfileImportCard : public Widget {
 public:
  explicit ProfileImportCard(Widget* parent = nullptr);
  ~ProfileImportCard() override;

  // 输入读取/校验
  std::string url() const;      // trim 后
  std::string name() const;     // 可选名
  bool url_valid() const;       // http(s) 前缀粗校验（SSRF 细校验在命令链路）

  // 导入回调（宿主接 CommandService::invoke("import_profile")；四分类
  // 通知已在命令链路 emit）——返回 false 时卡片置错误提示
  std::function<bool(const std::string& url, const std::string& name)>
      on_import;
  // 订阅列表交互（宿主接 select/update/delete 命令与 ContextMenu）
  std::function<void(const std::string& uid)> on_select_profile;
  std::function<void(const std::string& uid)> on_update_profile;
  std::function<void(const std::string& uid)> on_delete_profile;
  // action="menu"：宿主在鼠标位置弹右键菜单；其它 action 由宿主扩展
  std::function<void(const std::string& action, const std::string& uid)>
      on_context_action;

  void set_message(const std::wstring& msg, bool error);
  // 列表数据（只展示 remote/local；内部 merge/script 等子项不显示）
  void set_profiles(const std::vector<config::PrfItem>& items,
                    const std::string& current_uid);
  void reload_profiles();
  int profile_count() const { return static_cast<int>(profiles_.size()); }
  ScrollList* list() const { return list_; }
  ProfileRowWidget* row_at(int i) const;

  void on_paint(IRenderTarget& rt) override;
  void on_arrange(const RectF& area) override;

  TextField* url_field() const { return url_field_; }
  TextField* name_field() const { return name_field_; }
  // 标题色覆盖（缺省跟随主题 primary_text；抽屉场景设橙——T17）
  void set_title_color(const Color4f& c) { title_color_ = c; }

 private:
  void do_import();
  void rebuild_rows();

  TextField* url_field_ = nullptr;
  TextField* name_field_ = nullptr;
  Button* import_btn_ = nullptr;
  Label* title_ = nullptr;
  Label* message_ = nullptr;
  ScrollList* list_ = nullptr;
  bool msg_error_ = false;
  std::optional<Color4f> title_color_;

  std::vector<config::PrfItem> profiles_;
  std::string current_uid_;
};

}  // namespace clashwin::ui::scene
