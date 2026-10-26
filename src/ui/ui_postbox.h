#pragma once
// UiPostbox（T10）：UI 线程信箱——后台线程（WS/EventBus）→ PostMessage 到
// UI 线程执行（回调恒在 UI 线程），UI 订阅 EventBus 的统一入口。
// - 隐藏 message-only 窗口收 WM_APP_DISPATCH，WndProc 逐个执行队列
// - subscribe_ui：EventBus 订阅 + handler 自动转投 UI 线程（§6.1 InvokeThread::Ui）
// - pump()：UI 线程排空（测试用 PeekMessage 循环；生产由消息循环自然驱动）
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

#include <nlohmann/json.hpp>

namespace clashwin::ui {

class UiPostbox {
 public:
  static UiPostbox& instance();

  // EventBus 统一入口：订阅事件，handler 转投 UI 线程执行；返回退订 id
  uint64_t subscribe_ui(const std::string& event,
                        std::function<void(const nlohmann::json&)> ui_handler);
  void unsubscribe(uint64_t id);

  // 任意后台闭包 → UI 线程执行
  void post(std::function<void()> fn);

  // UI 线程排空消息队列（测试驱动；生产由 GetMessage 循环自然处理）
  static void pump();

  size_t pending_for_test();  // 测试检查点（加锁读队列长度）

 private:
  UiPostbox();
  ~UiPostbox();
  UiPostbox(const UiPostbox&) = delete;
  UiPostbox& operator=(const UiPostbox&) = delete;

  struct Impl;
  Impl* impl_;
};

}  // namespace clashwin::ui
