#pragma once
// Result<T,Error>：C++20 无 std::expected 的轻量替代（← 05文档 §1.3/§9.3）
// 约定：Error.code=0 视为成功语义占位，非 0 即失败；禁止抛异常穿越模块边界。
#include <string>
#include <utility>
#include <variant>

namespace clashwin {

struct Error {
  int code = 0;          // 0=未设置；失败分类码后续任务对齐 mihomo/error.rs
  std::string message;
};

template <typename T>
class Result {
 public:
  static Result ok(T v) { return Result(std::in_place_index<0>, std::move(v)); }
  static Result err(Error e) { return Result(std::in_place_index<1>, std::move(e)); }

  bool is_ok() const { return v_.index() == 0; }
  bool is_err() const { return v_.index() != 0; }
  const T& value() const { return std::get<0>(v_); }
  T& value() { return std::get<0>(v_); }
  const Error& error() const { return std::get<1>(v_); }

 private:
  template <size_t I, typename V>
  Result(std::in_place_index_t<I> tag, V&& v)
      : v_(tag, std::forward<V>(v)) {}
  std::variant<T, Error> v_;
};

// void 特化
template <>
class Result<void> {
 public:
  static Result ok() { return Result(); }
  static Result err(Error e) {
    Result r;
    r.ok_ = false;
    r.e_ = std::move(e);
    return r;
  }

  bool is_ok() const { return ok_; }
  bool is_err() const { return !ok_; }
  const Error& error() const { return e_; }

 private:
  Result() = default;
  bool ok_ = true;
  Error e_;
};

}  // namespace clashwin
