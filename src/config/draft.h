#pragma once
// Draft<T> 草稿-提交包装（← crates/clash-verge-draft/src/lib.rs 全语义对齐）
// 语义：
// - committed（已提交）与 optional draft（草稿）双轨；
// - data_arc()  → 已提交快照；latest_arc() → 草稿若存在否则已提交；
// - edit_draft(f) → 无草稿则以已提交为基底克隆一份草稿，f 内可变编辑；
// - apply()   → 草稿提交为已提交并清除；
// - discard() → 丢弃草稿（回滚），已提交保持不变；
// - with_data_modify(f) → 读已提交克隆本地修改后原位替换（乐观锁校验）。
// 线程安全：内部 mutex（对应 Rust parking_lot::RwLock + Arc 快照）。
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include "util/result.h"

namespace clashwin::config {

template <typename T>
class Draft {
 public:
  using Shared = std::shared_ptr<const T>;

  explicit Draft(T initial)
      : committed_(std::make_shared<T>(std::move(initial))) {}

  // ← data_arc：已提交数据快照（零拷贝，仅 clone 智能指针）
  Shared data_arc() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return committed_;
  }

  // ← latest_arc：草稿若存在返回草稿，否则已提交
  Shared latest_arc() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return draft_ ? Shared(std::shared_ptr<const T>(draft_)) : committed_;
  }

  // ← edit_draft：通过闭包可变编辑草稿（延迟克隆：无草稿时从已提交克隆）
  void edit_draft(const std::function<void(T&)>& f) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!draft_) draft_ = std::make_shared<T>(*committed_);
    f(*draft_);
  }

  // ← apply：草稿提交为已提交并清除
  void apply() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (draft_) {
      committed_ = std::move(draft_);
      draft_.reset();
    }
  }

  // ← discard：丢弃草稿（已提交不变 → 回滚语义）
  void discard() {
    std::lock_guard<std::mutex> lock(mutex_);
    draft_.reset();
  }

  bool has_draft() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return draft_ != nullptr;
  }

  // ← with_data_modify：以已提交克隆为本地副本修改，成功后原位替换；
  // 乐观锁：modify 期间 committed 被他人替换 → 失败（对照 Rust Arc::ptr_eq）
  Result<void> with_data_modify(
      const std::function<Result<void>(T&)>& modify) {
    std::shared_ptr<T> local;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      local = std::make_shared<T>(*committed_);
    }
    Result<void> r = modify(*local);
    if (!r.is_ok()) return r;
    std::lock_guard<std::mutex> lock(mutex_);
    committed_ = std::move(local);
    return Result<void>::ok();
  }

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<const T> committed_;
  std::shared_ptr<T> draft_;  // nullopt 语义：无草稿
};

}  // namespace clashwin::config
