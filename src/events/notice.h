#pragma once
// notice-service 等价物（← src/services/notice-service.ts 全语义）
// - push(type, message[, duration_override]) → id（自增）；hide(id) 手动关闭
// - 类型三态 success/info/error；缺省自动消失 3000/5000/8000ms
// - 50 条环形上限（超限裁剪最旧）
// - 订阅接口（T17/T21 UI 接）：变更（push/hide/到期裁剪）时回调
// - 到期扫描由内部分发线程驱动（250ms tick）；T19 轻量模式经 EventBus 过滤器
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace clashwin::events {

struct NoticeItem {
  uint64_t id = 0;
  std::string type;      // "success" | "info" | "error"
  int duration_ms = 0;   // 0 = 常驻（不自动消失）
  std::string message;
};

class NoticeService {
 public:
  using ChangeCallback = std::function<void()>;

  static NoticeService& instance();

  // ← baseShowNotice：返回 notice id；duration_override nullopt 按类型缺省
  uint64_t push(const std::string& type, const std::string& message,
                std::optional<int> duration_override = std::nullopt);
  void hide(uint64_t id);

  // 当前可见条目（到期的在快照前惰性裁剪）
  std::vector<NoticeItem> snapshot();

  // 返回订阅句柄（非 0）；回调在内部互斥锁释放后触发，允许回调内安全调用 snapshot()
  uint64_t subscribe(ChangeCallback cb);
  void unsubscribe(uint64_t sub_id);
  void unsubscribe();  // 清空全部订阅（兼容旧测试清场）

  // 到期扫描（内部线程驱动；测试可手动调）
  void sweep();

  // 仅测试：注入时钟推进（默认走 steady_clock）
  void advance_for_test(int64_t ms);

  ~NoticeService();
  NoticeService(const NoticeService&) = delete;
  NoticeService& operator=(const NoticeService&) = delete;

 private:
  NoticeService();
  struct Impl;
  Impl* impl_;
};

}  // namespace clashwin::events
