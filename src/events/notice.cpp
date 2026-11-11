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
  uint64_t next_id = 1;
  uint64_t next_sub_id = 1;
  std::vector<std::pair<uint64_t, ChangeCallback>> subs;

  std::thread sweeper;
  bool sweeper_run = false;
  int64_t clock_offset = 0;  // 测试时钟推进

  int64_t clock() const { return now_ms() + clock_offset; }

  std::vector<ChangeCallback> collect_callbacks_locked() const {
    std::vector<ChangeCallback> out;
    out.reserve(subs.size());
    for (const auto& [_, cb] : subs) {
      if (cb) out.push_back(cb);
    }
    return out;
  }

  static void invoke_callbacks(const std::vector<ChangeCallback>& cbs) {
    for (const auto& cb : cbs) {
      try {
        cb();
      } catch (...) {
      }
    }
  }

  // 惰性裁剪到期项（须持锁；严禁在锁内触发回调，防止 UI 线程同步调 snapshot() 自死锁）
  bool sweep_locked(int64_t t) {
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
    return changed;
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
  uint64_t id = 0;
  std::vector<ChangeCallback> cbs;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    // ← notice-service.ts:303 resolveDuration：首个 number 覆盖类型缺省
    int duration = duration_override.value_or(default_duration(type));
    id = impl_->next_id++;
    // ← notice-service.ts:318-327：≥50 条先裁剪最旧（留出本条空间）
    while (impl_->items.size() >= consts::kNoticeMaxCount) {
      impl_->items.erase(impl_->items.begin());
      impl_->expire_at.erase(impl_->expire_at.begin());
    }
    impl_->items.push_back(NoticeItem{id, type, duration, message});
    impl_->expire_at.push_back(duration > 0 ? impl_->clock() + duration : 0);
    cbs = impl_->collect_callbacks_locked();
  }
  Impl::invoke_callbacks(cbs);
  return id;
}

void NoticeService::hide(uint64_t id) {
  std::vector<ChangeCallback> cbs;
  bool erased = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    for (size_t i = 0; i < impl_->items.size(); ++i) {
      if (impl_->items[i].id == id) {
        impl_->items.erase(impl_->items.begin() + static_cast<long>(i));
        impl_->expire_at.erase(impl_->expire_at.begin() + static_cast<long>(i));
        erased = true;
        cbs = impl_->collect_callbacks_locked();
        break;
      }
    }
  }
  if (erased) Impl::invoke_callbacks(cbs);
}

std::vector<NoticeItem> NoticeService::snapshot() {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->sweep_locked(impl_->clock());
  return impl_->items;
}

uint64_t NoticeService::subscribe(ChangeCallback cb) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  const uint64_t id = impl_->next_sub_id++;
  impl_->subs.emplace_back(id, std::move(cb));
  return id;
}

void NoticeService::unsubscribe(uint64_t sub_id) {
  if (sub_id == 0) return;
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->subs.erase(
      std::remove_if(impl_->subs.begin(), impl_->subs.end(),
                     [sub_id](const auto& p) { return p.first == sub_id; }),
      impl_->subs.end());
}

void NoticeService::unsubscribe() {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->subs.clear();
}

void NoticeService::sweep() {
  std::vector<ChangeCallback> cbs;
  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    changed = impl_->sweep_locked(impl_->clock());
    if (changed) cbs = impl_->collect_callbacks_locked();
  }
  if (changed) Impl::invoke_callbacks(cbs);
}

void NoticeService::advance_for_test(int64_t ms) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->clock_offset += ms;
}

}  // namespace clashwin::events
