// T09 notice-service 实现（← notice-service.ts baseShowNotice/hideNotice/snapshot）
#include "events/notice.h"

#include "events/event_names.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

namespace clashwin::events {

namespace {
int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

int default_duration(const std::string& type) {
  // ← notice-service.ts:43-47 DEFAULT_DURATIONS
  if (type == "success") return consts::kNoticeSuccessMs;
  if (type == "error") return consts::kNoticeErrorMs;
  return consts::kNoticeInfoMs;  // info
}
}  // namespace

struct NoticeService::Impl {
  mutable std::mutex mu;
  std::vector<NoticeItem> items;   // 追加序；到期/超限裁剪
  std::vector<int64_t> expire_at;  // 与 items 对齐；0 = 常驻
  uint64_t next_id = 0;
  ChangeCallback cb;

  std::thread sweeper;
  bool sweeper_run = false;
  int64_t clock_offset = 0;  // 测试时钟推进

  int64_t clock() const { return now_ms() + clock_offset; }

  void notify() {
    if (cb) {
      try {
        cb();
      } catch (...) {
      }
    }
  }

  // 惰性裁剪到期项（须持锁）
  void sweep_locked(int64_t t) {
    bool changed = false;
    for (size_t i = 0; i < items.size();) {
      if (expire_at[i] != 0 && t >= expire_at[i]) {
        items.erase(items.begin() + static_cast<long>(i));
        expire_at.erase(expire_at.begin() + static_cast<long>(i));
        changed = true;
      } else {
        ++i;
      }
    }
    if (changed) notify();
  }
};

NoticeService::NoticeService() : impl_(new Impl) {
  impl_->sweeper_run = true;
  impl_->sweeper = std::thread([this]() {
    while (impl_->sweeper_run) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      sweep();
    }
  });
}

NoticeService::~NoticeService() {
  impl_->sweeper_run = false;
  if (impl_->sweeper.joinable()) impl_->sweeper.join();
  delete impl_;
}

NoticeService& NoticeService::instance() {
  static NoticeService svc;
  return svc;
}

uint64_t NoticeService::push(const std::string& type, const std::string& message,
                             std::optional<int> duration_override) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  // ← notice-service.ts:303 resolveDuration：首个 number 覆盖类型缺省
  int duration = duration_override.value_or(default_duration(type));
  uint64_t id = impl_->next_id++;
  // ← notice-service.ts:318-327：≥50 条先裁剪最旧（留出本条空间）
  while (impl_->items.size() >= consts::kNoticeMaxCount) {
    impl_->items.erase(impl_->items.begin());
    impl_->expire_at.erase(impl_->expire_at.begin());
  }
  impl_->items.push_back(NoticeItem{id, type, duration, message});
  impl_->expire_at.push_back(duration > 0 ? impl_->clock() + duration : 0);
  impl_->notify();
  return id;
}

void NoticeService::hide(uint64_t id) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  for (size_t i = 0; i < impl_->items.size(); ++i) {
    if (impl_->items[i].id == id) {
      impl_->items.erase(impl_->items.begin() + static_cast<long>(i));
      impl_->expire_at.erase(impl_->expire_at.begin() + static_cast<long>(i));
      impl_->notify();
      return;
    }
  }
}

std::vector<NoticeItem> NoticeService::snapshot() {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->sweep_locked(impl_->clock());
  return impl_->items;
}

void NoticeService::subscribe(ChangeCallback cb) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->cb = std::move(cb);
}

void NoticeService::unsubscribe() {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->cb = nullptr;
}

void NoticeService::sweep() {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->sweep_locked(impl_->clock());
}

void NoticeService::advance_for_test(int64_t ms) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->clock_offset += ms;
}

}  // namespace clashwin::events
