#include "window_state.h"

#include <windows.h>

#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0601
#include <shellscalingapi.h>
#else
#define WINVER_ 0
#endif

#include <chrono>
#include <cmath>
#include <format>
#include <fstream>

#include <nlohmann/json.hpp>

#include "clashwin/config/constants.h"
#include "util/log.h"
#include "util/utf.h"

namespace clashwin::config::window_state {

namespace {

int64_t now_ms() {
  return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

// 读现有文件内容（compact 单行 JSON）
std::optional<std::string> read_file(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::nullopt;
  std::string s((std::istreambuf_iterator<char>(f)),
                std::istreambuf_iterator<char>());
  return s;
}

}  // namespace

// ---------- 0×0 守卫 + DPI 容差（← window_manager.rs:146-178） ----------
bool geometry_valid(double w, double h) {
  // 必须留出 DPI 换算取整余量：150% 下 271 逻辑宽 = 406 物理，回算 270.67 < 271；
  // 严格 < 会把最小宽状态永远判为无效
  return w >= consts::kWindowMinWidth - consts::kGeometryMinEps &&
         h >= consts::kWindowMinHeight - consts::kGeometryMinEps;
}

// ---------- 脱屏判定（← window_manager.rs:246-267） ----------
bool is_position_on_screen(double x, double y) {
  // 枚举全部显示器； monitors 为空 → 视为在屏
  struct Ctx {
    double x, y;
    bool found_any = false;
    bool on_screen = false;
  } ctx{x, y};

  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR, HDC, LPRECT rc, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        c->found_any = true;
        // 物理矩形 → 逻辑判定容差与原版一致：x∈[mx-100, mx+mw-50]、y 同理
        if (c->x >= rc->left - 100.0 && c->x <= rc->right - 50.0 &&
            c->y >= rc->top - 100.0 && c->y <= rc->bottom - 50.0) {
          c->on_screen = true;
          return FALSE;  // 找到即停
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));

  if (!ctx.found_any) return true;
  return ctx.on_screen;
}

// ---------- 直写（← save_window_state_sync） ----------
void save_sync(const std::wstring& path, double w, double h,
               std::optional<double> x, std::optional<double> y) {
  nlohmann::json obj = nlohmann::json::object();
  // 【DPIARCH】内部数值必须整数：整数值以 int 落盘（271 而非 271.0/270.666）
  auto jnum = [](double v) -> nlohmann::json {
    double r = std::round(v);
    if (std::abs(v - r) < 1e-6) return static_cast<int64_t>(r);
    return v;
  };
  obj["width"] = jnum(w);
  obj["height"] = jnum(h);
  if (x) obj["x"] = jnum(*x);
  if (y) obj["y"] = jnum(*y);
  // compact 单行；nlohmann 默认 std::map 键序 = serde_json BTreeMap 字母序
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return;
  std::string s = obj.dump();
  f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

// ---------- 节流落盘器 ----------
WindowStateStore::WindowStateStore(std::wstring path, int64_t throttle_ms)
    : path_(std::move(path)), throttle_ms_(throttle_ms) {}

WindowStateStore::~WindowStateStore() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_started_ && worker_.joinable()) worker_.join();
  // 析构时若仍有待写（程序退出），同步补写终值
  flush_pending();
}

void WindowStateStore::worker_loop() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (true) {
    if (stop_) return;
    if (deadline_ms_ == 0) {
      cv_.wait(lock, [this] { return stop_ || deadline_ms_ != 0; });
      continue;
    }
    // 等到补写时间点（新 deadline 会刷新等待终点）
    cv_.wait_until(lock, std::chrono::system_clock::from_time_t(0) +
                             std::chrono::milliseconds(deadline_ms_));
    if (stop_) return;
    int64_t now = now_ms();
    if (deadline_ms_ != 0 && now >= deadline_ms_) {
      deadline_ms_ = 0;
      lock.unlock();
      flush_pending();
      lock.lock();
    }
  }
}

void WindowStateStore::flush_pending() {
  std::optional<Pending> p;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    p = pending_;
    pending_.reset();
  }
  if (!p) return;
  last_save_ms_.store(now_ms());
  write_ordered(p->seq, p->w, p->h, p->x, p->y);
}

void WindowStateStore::on_geometry_change(double w, double h,
                                          std::optional<double> x,
                                          std::optional<double> y) {
  if (!geometry_valid(w, h)) return;
  uint64_t seq = seq_.fetch_add(1) + 1;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = Pending{seq, w, h, x, y};
    // 启动尾随补写线程（懒启动）
    if (!worker_started_) {
      worker_started_ = true;
      worker_ = std::thread([this] { worker_loop(); });
    }
    int64_t last = last_save_ms_.load();
    int64_t now = now_ms();
    int64_t elapsed = now - last;
    if (elapsed >= throttle_ms_) {
      deadline_ms_ = 0;  // 立即写
    } else {
      // 尾随补写：只保留最新一版，到点落终值
      int64_t wait = throttle_ms_ - elapsed;
      deadline_ms_ = now + wait;
      cv_.notify_all();
      return;
    }
  }
  flush_pending();
}

void WindowStateStore::save_immediately(double w, double h,
                                        std::optional<double> x,
                                        std::optional<double> y) {
  if (!geometry_valid(w, h)) return;
  uint64_t seq = seq_.fetch_add(1) + 1;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.reset();  // 清空待写槽，避免旧值稍后覆盖
    last_save_ms_.store(now_ms());
  }
  write_ordered(seq, w, h, x, y);
}

bool WindowStateStore::write_ordered(uint64_t seq, double w, double h,
                                     std::optional<double> x,
                                     std::optional<double> y) {
  // 比对版本号：比已写版本更旧 → 迟到的旧值，直接丢弃
  std::lock_guard<std::mutex> lock(write_mutex_);
  if (seq <= written_seq_.load()) return false;
  write_file(w, h, x, y);
  written_seq_.store(seq);
  return true;
}

void WindowStateStore::flush_pending_for_test() { flush_pending(); }

bool WindowStateStore::wait_pending_flush(int64_t timeout_ms) {
  int64_t start = now_ms();
  while (now_ms() - start < timeout_ms) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!pending_) return true;
    }
    Sleep(5);
  }
  return false;
}

void WindowStateStore::write_file(double w, double h, std::optional<double> x,
                                  std::optional<double> y) {
  save_sync(path_, w, h, x, y);
}

// ---------- 恢复（← restore_window_state） ----------
std::optional<SavedWindowState> restore(const std::wstring& path) {
  auto content = read_file(path);
  if (!content) return std::nullopt;
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(*content);
  } catch (...) {
    return std::nullopt;
  }
  if (!j.is_object()) return std::nullopt;
  SavedWindowState st;
  if (!j.contains("width") || !j.contains("height")) return std::nullopt;
  if (!j["width"].is_number() || !j["height"].is_number()) return std::nullopt;
  st.width = j["width"].get<double>();
  st.height = j["height"].get<double>();
  if (j.contains("x") && j["x"].is_number()) st.x = j["x"].get<double>();
  if (j.contains("y") && j["y"].is_number()) st.y = j["y"].get<double>();

  if (!geometry_valid(st.width, st.height)) return std::nullopt;
  st.width = std::clamp(st.width, consts::kWindowMinWidth,
                        consts::kWindowMaxWidth);
  st.height = std::clamp(st.height, consts::kWindowMinHeight,
                         consts::kWindowMaxHeight);
  // 默认尺寸 271×680（源码 resolve/window.rs:18-19 DEFAULT_WIDTH/HEIGHT）。
  //   历史版本在 150% DPI 下把 680 取整成 681（恢复后物理 1022，比默认 1020 多 2px）；
  //   1px 级取整残留按默认值归一，避免把旧值一直带下去。
  constexpr double kRoundTolerance = 1.5;
  if (st.height > consts::kWindowDefaultHeight &&
      st.height <= consts::kWindowDefaultHeight + kRoundTolerance) {
    st.height = consts::kWindowDefaultHeight;
  }
  if (st.width > consts::kWindowDefaultWidth &&
      st.width <= consts::kWindowDefaultWidth + kRoundTolerance) {
    st.width = consts::kWindowDefaultWidth;
  }
  // 位置脱屏（拔插显示器）→ 重置为默认居中（x/y 空）
  if (st.x && st.y && !is_position_on_screen(*st.x, *st.y)) {
    CWLOG_WARN("saved window position off-screen, reset to default center");
    st.x = std::nullopt;
    st.y = std::nullopt;
  }
  return st;
}

}  // namespace clashwin::config::window_state
