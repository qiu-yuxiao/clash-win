// UiPostbox 实现（T10）：message-only 窗口 + 互斥队列 + EventBus 转投。
#include "ui_postbox.h"

#include <windows.h>

#include <atomic>
#include <unordered_map>
#include <vector>

#include "events/event_bus.h"
#include "util/log.h"

namespace clashwin::ui {

namespace {
constexpr UINT kWmDispatch = WM_APP + 20;  // WM_APP_DISPATCH（§1.3）
constexpr wchar_t kWndClass[] = L"ClashWinUiPostboxCls";
}  // namespace

struct UiPostbox::Impl {
  HWND hwnd = nullptr;
  std::mutex mu;
  std::deque<std::function<void()>> queue;
  std::atomic<uint64_t> next_id{1};
  std::unordered_map<uint64_t, uint64_t> bus_ids;  // postbox id → bus id
};

UiPostbox::UiPostbox() : impl_(new Impl) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
    if (msg == kWmDispatch) {
      auto* impl = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      if (impl) {
        // WM_APP_DISPATCH：排空队列（逐个执行；执行中新增项同帧继续处理）
        std::deque<std::function<void()>> batch;
        {
          std::lock_guard<std::mutex> lk(impl->mu);
          batch.swap(impl->queue);
        }
        while (!batch.empty()) {
          auto fn = std::move(batch.front());
          batch.pop_front();
          fn();
        }
      }
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  };
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kWndClass;
  RegisterClassExW(&wc);
  // HWND_MESSAGE：message-only 窗口（不可见，仅收消息）
  impl_->hwnd = CreateWindowExW(0, kWndClass, L"ClashWinUiPostbox", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
  SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(impl_));
}

UiPostbox::~UiPostbox() {
  if (impl_->hwnd) DestroyWindow(impl_->hwnd);
  delete impl_;
}

UiPostbox& UiPostbox::instance() {
  static UiPostbox box;
  return box;
}

void UiPostbox::post(std::function<void()> fn) {
  bool need_wake = false;
  {
    std::lock_guard<std::mutex> lk(impl_->mu);
    need_wake = impl_->queue.empty();
    impl_->queue.push_back(std::move(fn));
  }
  if (impl_->hwnd && need_wake) PostMessageW(impl_->hwnd, kWmDispatch, 0, 0);
}

uint64_t UiPostbox::subscribe_ui(
    const std::string& event,
    std::function<void(const nlohmann::json&)> ui_handler) {
  // 经 shared_ptr 稳定持有 handler；EventBus 回调（可能在后台线程）→ post
  auto stable = std::make_shared<std::function<void(const nlohmann::json&)>>(
      std::move(ui_handler));
  uint64_t bus_id = events::EventBus::instance().subscribe(
      event, [stable](const nlohmann::json& payload) {
        instance().post([stable, payload]() { (*stable)(payload); });
      });
  uint64_t id = impl_->next_id.fetch_add(1);
  impl_->bus_ids.emplace(id, bus_id);
  return id;
}

void UiPostbox::unsubscribe(uint64_t id) {
  auto it = impl_->bus_ids.find(id);
  if (it == impl_->bus_ids.end()) return;
  events::EventBus::instance().unsubscribe(it->second);
  impl_->bus_ids.erase(it);
}

size_t UiPostbox::pending_for_test() {
  std::lock_guard<std::mutex> lk(impl_->mu);
  return impl_->queue.size();
}

void UiPostbox::pump() {
  // 排空当前线程消息队列直到无 kDispatch 待处理
  MSG msg;
  while (PeekMessageW(&msg, nullptr, kWmDispatch, kWmDispatch, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

}  // namespace clashwin::ui
