#pragma once
// 首次运行新手向导弹窗（T21）：对齐 Clash Mini v3.2.1 首发体验（first-run-guide-dialog.tsx）。
// - 双步流：Step 1（欢迎与订阅链接导入）-> Step 2（就绪仪表盘）
// - 剪贴板自动嗅探：探测 http://, https://, clash:// 并在顶部显示气泡横幅
// - Enter 键全局监听：任意位置按 Enter 即执行自动导入
// - BUG-311 守护释放：is_importing 守卫在状态变更前释放，不阻断后续测速优选
// - BUG-310 自动接管：导入成功后若系统代理与 TUN 均未开启，自动开启系统代理
// - Step 2 节点测速：2.5s 测速超时判定，就绪显示 ⚡ xx ms 绿标，超时展示"🔄 重新测速并优选节点"兜底
// - 暖金 GitHub ⭐ Star 应援卡片 + Wiki 帮助文档链接
// - 严格遵守 Direct2D 局部坐标规则 {0, y}
#include <functional>
#include <string>

#include "ui/widget.h"

namespace clashwin::ui::scene {

enum class FirstRunStep {
  Input,  // STEP 1 / 2
  Ready   // STEP 2 / 2
};

class FirstRunGuideDialog : public Widget {
 public:
  static constexpr const char* kStorageKey = "clash-mini-first-run-completed";

  static bool is_first_run_completed();
  static void set_first_run_completed(bool completed);
  static bool has_real_profile();

  explicit FirstRunGuideDialog(Widget* parent = nullptr);
  ~FirstRunGuideDialog() override = default;

  void open();
  void close();
  bool is_open() const { return open_; }

  FirstRunStep step() const { return step_; }
  void set_step(FirstRunStep s);

  const std::string& url() const { return url_; }
  void set_url(const std::string& u);

  const std::string& detected_clipboard_url() const { return detected_clipboard_url_; }
  void check_clipboard();

  bool is_importing() const { return is_importing_; }
  const std::string& error_message() const { return error_message_; }

  // Step 2 状态
  const std::string& active_node_name() const { return active_node_name_; }
  void set_active_node_name(const std::string& name) { active_node_name_ = name; }

  int active_delay_ms() const { return active_delay_ms_; }
  void set_active_delay_ms(int ms);

  bool proxy_ready() const { return active_delay_ms_ > 0 && active_delay_ms_ < 5000; }
  bool takeover_active() const { return takeover_active_; }
  void set_takeover_active(bool active) { takeover_active_ = active; }

  // 动作回调
  std::function<bool(const std::string& url)> on_import;
  std::function<void(bool)> on_patch_system_proxy;
  std::function<void()> on_retest_nodes;
  std::function<void()> on_open_wiki;
  std::function<void()> on_open_github;
  std::function<void()> on_dismiss;

  void submit_import();
  void finish();

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;

  bool hit_self(PointF local) const override {
    return open_ && Widget::hit_self(local);
  }

  // 几何区域供单测断言
  RectF card_rect() const { return card_rect_; }
  RectF clip_banner_rect() const { return banner_clip_rect_; }
  RectF input_rect() const { return input_rect_; }
  RectF submit_btn_rect() const { return btn_submit_rect_; }
  RectF later_btn_rect() const { return btn_later_rect_; }
  RectF star_card_rect() const { return card_star_rect_; }
  RectF retest_btn_rect() const { return btn_retest_rect_; }

 private:
  bool open_ = false;
  FirstRunStep step_ = FirstRunStep::Input;
  std::string url_;
  std::string detected_clipboard_url_;
  bool is_importing_ = false;
  std::string error_message_;

  std::string active_node_name_ = "香港 01 | BGP [专线]";
  int active_delay_ms_ = 0;  // 0 表示测速中或未测出
  bool takeover_active_ = false;
  bool retesting_ = false;

  RectF card_rect_;
  RectF btn_close_x_rect_;
  RectF banner_clip_rect_;
  RectF input_rect_;
  RectF btn_clear_rect_;
  RectF btn_paste_rect_;
  RectF btn_submit_rect_;
  RectF btn_later_rect_;

  // Step 2 rects
  RectF card_node_rect_;
  RectF btn_retest_rect_;
  RectF card_star_rect_;
  RectF btn_wiki_rect_;
  RectF btn_finish_star_rect_;
  RectF btn_finish_rect_;

  bool hover_close_x_ = false;
  bool hover_banner_ = false;
  bool hover_paste_ = false;
  bool hover_clear_ = false;
  bool hover_submit_ = false;
  bool hover_later_ = false;
  bool hover_retest_ = false;
  bool hover_wiki_ = false;
  bool hover_finish_star_ = false;
  bool hover_finish_ = false;
  bool input_focused_ = false;
};

}  // namespace clashwin::ui::scene
