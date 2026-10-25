#pragma once
// window_state.json 读写 —— 复刻 ClashVerge utils/window_manager.rs:107-305：
// 【1s 节流 + 尾随补写】间隔内连续几何变化不逐帧落盘，但最后一版一定会落盘
//   （纯前沿节流会丢「拖拽停止前那一段」，拖完立即退出会回退到中途尺寸）；
// 【版本号防乱序】每次变化领递增序号，写入方在锁内比对，迟到旧值直接丢弃；
// 【0×0 守卫 + DPI 容差】destroy 触发的 0×0 与 150% 缩放取整误差不落盘；
// 【脱屏重置】恢复时位置脱离全部显示器可视范围 → x/y 置空（默认居中）。
// JSON 形态：单行 compact，键按字母序（serde_json BTreeMap / nlohmann
// std::map 行为一致）：{"height":..,"width":..,"x":..,"y":..}
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace clashwin::config::window_state {

struct SavedWindowState {
  double width = 0;
  double height = 0;
  std::optional<double> x;
  std::optional<double> y;
};

// 几何可信判定（0×0 守卫 + DPI 取整容差；← is_geometry_valid）
bool geometry_valid(double w, double h);

// (x,y) 是否落在任一已连接显示器可视范围内（逻辑像素；← is_position_on_screen）
bool is_position_on_screen(double x, double y);

// 节流落盘器（每进程窗口主实例一个）
class WindowStateStore {
 public:
  // throttle_ms 默认 1000（← GEOMETRY_SAVE_INTERVAL_MS）；测试可注入短间隔
  explicit WindowStateStore(std::wstring path, int64_t throttle_ms = 1000);
  ~WindowStateStore();

  WindowStateStore(const WindowStateStore&) = delete;
  WindowStateStore& operator=(const WindowStateStore&) = delete;

  // ← save_window_state_on_geometry_change：节流 + 尾随补写入口
  void on_geometry_change(double w, double h, std::optional<double> x,
                          std::optional<double> y);

  // ← save_window_state_immediately：跳过节流立即写（窗口销毁前）；
  // 同时清空待写槽，避免节流任务稍后用旧值覆盖
  void save_immediately(double w, double h, std::optional<double> x,
                        std::optional<double> y);

  // ← write_geometry_ordered：按版本号顺序落盘；seq <= 已写版本 → 丢弃。
  // 公开供单测直测乱序拒绝语义
  bool write_ordered(uint64_t seq, double w, double h, std::optional<double> x,
                     std::optional<double> y);

  // 测试钩子：等待尾随补写任务完成（等待 pending 槽清空，最多 ms 超时）
  bool wait_pending_flush(int64_t timeout_ms);

  // 测试钩子：立即冲刷待写槽（与 worker 到点的尾随补写同一路径 flush_pending，
  // 但不经真实时间窗——节流用例据此与负载调度解耦，flaky 根治）
  void flush_pending_for_test();

  int64_t last_save_ms() const { return last_save_ms_.load(); }
  uint64_t written_seq() const { return written_seq_.load(); }

 private:
  void flush_pending();
  void write_file(double w, double h, std::optional<double> x,
                  std::optional<double> y);
  void worker_loop();

  std::wstring path_;
  int64_t throttle_ms_;
  std::thread worker_;
  bool worker_started_ = false;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  // 待写槽（版本号, 宽, 高, x, y）；只保留最新一版
  struct Pending {
    uint64_t seq = 0;
    double w = 0, h = 0;
    std::optional<double> x, y;
  };
  std::optional<Pending> pending_;
  bool stop_ = false;
  int64_t deadline_ms_ = 0;  // 尾随补写时间点

  std::atomic<int64_t> last_save_ms_{0};
  std::atomic<uint64_t> seq_{0};         // 几何版本号（每次变化递增）
  std::atomic<uint64_t> written_seq_{0}; // 已落盘最大版本号
  std::mutex write_mutex_;               // 串行化文件写入
};

// ← restore_window_state：读 window_state.json；无效尺寸 → nullopt；
// 有效尺寸钳制 [MIN,MAX]；位置脱屏 → x/y 置空（默认居中）
std::optional<SavedWindowState> restore(const std::wstring& path);

// 直接落盘（← save_window_state_sync：干净 {width,height,x?,y?} 对象）
void save_sync(const std::wstring& path, double w, double h,
               std::optional<double> x, std::optional<double> y);

}  // namespace clashwin::config::window_state
