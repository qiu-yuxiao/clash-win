#pragma once
// 首次运行新手向导弹窗（T21）：按 ClashVerge
// src/pages/_layout/components/first-run-guide-dialog.tsx 源码重写。
// - 卡片：get3DCardStyle(default) 3D 金卡 + paper maxWidth 520 / borderRadius 8
// - 双步流：STEP 1 导入（标题/步骤胶囊/特性胶囊/剪贴板横幅/凹陷输入框/双列按钮）
//   → STEP 2 就绪（绿色仪表卡 + 暖金 Star 卡 + 双列按钮）
// - 无 ✕ 关闭、无“清除”按钮（源码仅遮罩/Esc/稍后/完成出口）
// - 剪贴板嗅探 http(s)/clash://，Enter 全局提交
// - BUG-311 守卫提前释放；BUG-310 导入成功后自动接管系统代理
#include <cstdint>
#include <functional>
#include <string>

#include "ui/widget.h"

namespace clashwin::ui::scene {

enum class FirstRunStep {
  Input,  // STEP 1 / 2
  Ready   // STEP 2 / 2
};

// ← services/delay.ts:33 NODE_DELAY_MAX_MS（与后端判死阈值统一，严禁漂移）
inline constexpr int kReadyDelayMaxMs = 5000;
// src: first-run-guide-dialog.tsx:46 READY_TIMEOUT_MS = 20_000
inline constexpr int64_t kReadyTimeoutMs = 20000;
// src: first-run-guide-dialog.tsx:48 READY_POLL_INTERVAL_MS = 1_500
inline constexpr int64_t kReadyPollIntervalMs = 1500;

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

  // src: first-run-guide-dialog.tsx:169-170 proxyReady（activeDelay > 0 且 < 5000）
  bool proxy_ready() const {
    return active_delay_ms_ > 0 && active_delay_ms_ < kReadyDelayMaxMs;
  }
  // src: first-run-guide-dialog.tsx:173-174 isUnconfirmed
  bool unconfirmed() const;
  bool ready_timed_out() const { return gave_up_; }  // src: :230 setGaveUp(true)
  bool takeover_active() const { return takeover_active_; }
  void set_takeover_active(bool active) { takeover_active_ = active; }
  bool retesting() const { return retesting_; }

  // 就绪计时/轮询驱动（窗口 WM_TIMER 每 250ms 调；src: :228-245）
  void tick(int64_t now_ms);
  // 重测异步结束 → retesting_=false，计时/轮询恢复（src: :203 finally）
  void on_retest_finished();

  // 动作回调
  std::function<bool(const std::string& url)> on_import;
  std::function<void(bool)> on_patch_system_proxy;
  std::function<void()> on_retest_nodes;
  std::function<void()> on_refresh_proxy;  // ← refreshProxy（proxies 上下文重载）
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

  std::string active_node_name_;
  int active_delay_ms_ = -1;  // -1=null 未测出；0=测速失败（src: :143-166）
  bool takeover_active_ = false;
  bool retesting_ = false;
  // src: :228-245 20s 超时 + 1.5s 轮询
  bool gave_up_ = false;
  int64_t ready_started_ms_ = 0;  // 0=未开始计时
  int64_t last_poll_ms_ = 0;
  bool auto_retested_ = false;    // src: :209-212 autoRetestedRef

  RectF card_rect_;
  RectF banner_clip_rect_;
  RectF input_rect_;
  RectF btn_enter_rect_;
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

  bool hover_banner_ = false;
  bool hover_paste_ = false;
  bool hover_submit_ = false;
  bool hover_later_ = false;
  bool hover_retest_ = false;
  bool hover_wiki_ = false;
  bool hover_finish_star_ = false;
  bool hover_finish_ = false;
  bool hover_enter_ = false;
  bool input_focused_ = false;
};

}  // namespace clashwin::ui::scene
