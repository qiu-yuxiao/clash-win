#pragma once
// 首次运行新手向导弹窗（T21）：按 ClashVerge
// src/pages/_layout/components/first-run-guide-dialog.tsx 源码重写。
// - 卡片：get3DCardStyle(default) 3D 金卡 + paper maxWidth 520 / borderRadius 8
// - 双步流：STEP 1 导入（标题/步骤胶囊/特性胶囊/剪贴板横幅/凹陷输入框/双列按钮）
//   → STEP 2 就绪（绿色仪表卡 + 暖金 Star 卡 + 双列按钮）
// - 无 ✕ 关闭、无“清除”按钮（源码仅遮罩/Esc/稍后/完成出口）
// - 剪贴板嗅探 http(s)/clash://，Enter 全局提交
// - BUG-311 守卫提前释放；BUG-310 导入成功后自动接管系统代理
#include <memory>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ui/context_menu.h"
#include "ui/overlay_fade.h"
#include "ui/textfield.h"
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
  // ★ ❌9「稍后配置」：点「稍后」完成引导后，宿主自动滑开左侧设置抽屉指引订阅卡位置
  //   （← Mini first-run-guide-dialog.tsx:469-473 handleDismissLater 自动滑开抽屉）。
  std::function<void()> on_dismiss_later;

  void submit_import();
  void finish();

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;

  bool is_ready_step() const { return step_ == FirstRunStep::Ready; }

  // 订阅链接 TextField 右键 → 编辑命令菜单（← Mini handleContextMenu :711）
  void open_url_menu(PointF anchor);

  bool hit_self(PointF local) const override {
    return open_ && Widget::hit_self(local);
  }
  Widget* hit_test(PointF global) override {
    if (!open_) return nullptr;
    return Widget::hit_test(global);
  }

  // 几何区域供单测断言
  RectF card_rect() const { return card_rect_; }
  RectF clip_banner_rect() const { return banner_clip_rect_; }
  RectF input_rect() const { return input_rect_; }
  // 三大特性胶囊的实际矩形（折行后可多于一行；测试断言不重叠用）
  const std::vector<RectF>& pill_rects() const { return pill_rects_; }
  RectF submit_btn_rect() const { return btn_submit_rect_; }
  RectF later_btn_rect() const { return btn_later_rect_; }
  RectF star_card_rect() const { return card_star_rect_; }
  RectF retest_btn_rect() const { return btn_retest_rect_; }
  RectF wiki_btn_rect() const { return btn_wiki_rect_; }
  float star_title_h() const { return star_title_h_; }
  float star_body_h() const { return star_body_h_; }
  TextField* url_field() const { return url_field_; }
  ContextMenu* url_menu() const { return url_menu_; }

 private:
  bool open_ = false;
  OverlayFade fade_;  // ← MUI Dialog 缺省 Fade（enter 225 / leave 195）
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
  // 引导文案实际折行高度：日/德等长文案在 271 宽下折成 4~5 行，
  // 固定 kIntroH(41.6=2 行) 会让特性胶囊压在文字上。
  // Mini 是 HTML 流式排版，高度自然增长 → 这里按实际测量值参与 y 链。
  float intro_h_ = 41.6f;
  // 三大特性胶囊：flex 1 1 auto + flexWrap:wrap + gap 6（src: :607-644），
  // 窄窗（271）下日/德文案放不下一行 → 折成多行，行内均摊剩余宽度
  std::vector<RectF> pill_rects_;
  float pills_h_ = 22.2f;
  // 胶囊折行排版（返回总高，并填充 pill_rects_）
  float layout_pills(float pills_y, float inner_x, float inner_w);
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
  float warn_y_ = 0.0f;  // 接管提示绘制 y（随绿色卡内部块流式定位，不再依赖重测按钮）
  float status_h_ = 37.5f;      // src: first-run-guide-dialog.tsx:909 动态测量折行高
  float star_title_h_ = 19.6f;  // src: first-run-guide-dialog.tsx:1041 动态测量折行高
  float star_body_h_ = 58.2f;   // src: first-run-guide-dialog.tsx:1052 动态测量折行高

  bool hover_banner_ = false;
  bool hover_paste_ = false;
  bool hover_submit_ = false;
  bool hover_later_ = false;
  bool hover_retest_ = false;
  bool hover_wiki_ = false;
  bool hover_finish_star_ = false;
  bool hover_finish_ = false;
  bool hover_enter_ = false;

  bool pressed_banner_ = false;
  bool pressed_paste_ = false;
  bool pressed_submit_ = false;
  bool pressed_later_ = false;
  bool pressed_retest_ = false;
  bool pressed_wiki_ = false;
  bool pressed_finish_star_ = false;
  bool pressed_finish_ = false;
  bool pressed_enter_ = false;

  float banner_h_ = 31.4f;

  // 订阅链接：复用 Win 端 TextField（← Mini :698-732 真实 MUI TextField
  //   autoFocus/fullWidth/value+onChange/onContextMenu/endAdornment 回车图标）
  TextField* url_field_ = nullptr;
  // 订阅链接右键编辑菜单（剪切/复制/粘贴/全选/清空，← Mini :1859-1932）
  ContextMenu* url_menu_ = nullptr;
};

}  // namespace clashwin::ui::scene
