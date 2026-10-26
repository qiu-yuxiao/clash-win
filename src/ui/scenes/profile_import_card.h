#pragma once
// 订阅导入卡（T15）：URL 输入 + 导入按钮 + 名称（可选）+ 右键动作接口。
// ← census §2/§3：profile-import-card（订阅导入+列表+右键菜单）位于设置抽屉
//   左列 240px 首位（T17 抽屉容器承载本卡）；右键菜单动作（更新/编辑/删除/
//   置顶）经回调接口暴露，菜单位于 Profile 卡（React ContextMenu 语义）。
// 数据对接：import_profile 命令（ValidationOutcome 四分类通知由命令链路发）。
#include <functional>
#include <optional>
#include <string>

#include "../button.h"
#include "../container.h"
#include "../label.h"
#include "../textfield.h"
#include "../widget.h"

namespace clashwin::ui::scene {

inline constexpr float kImportCardW = 240;  // ← 抽屉左列 240px 固定宽

// 导入卡（卡片本体；抽屉内纵向布局的一个 88px 高块）
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
  // 右键菜单动作接口（更新/编辑/删除/置顶——宿主接对应命令）
  std::function<void(const std::string& action, const std::string& uid)>
      on_context_action;

  void set_message(const std::wstring& msg, bool error);
  void on_paint(IRenderTarget& rt) override;
  void on_arrange(const RectF& area) override;

  TextField* url_field() const { return url_field_; }
  TextField* name_field() const { return name_field_; }
  // 标题色覆盖（缺省跟随主题 primary_text；抽屉场景设橙——T17）
  void set_title_color(const Color4f& c) { title_color_ = c; }

 private:
  void do_import();

  TextField* url_field_ = nullptr;
  TextField* name_field_ = nullptr;
  Button* import_btn_ = nullptr;
  Label* title_ = nullptr;
  Label* message_ = nullptr;
  bool msg_error_ = false;
  std::optional<Color4f> title_color_;
};

}  // namespace clashwin::ui::scene
