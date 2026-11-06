#pragma once
// T07 配置校验（← ClashVerge core/validate.rs 全语义）
// - mini-mihomo.exe -t -d <app_dir> -f <config> 子进程，10s 超时（超时 kill 显式回收）
// - 结果四分类 Valid / Invalid{kind,message} / Skipped{reason} / Busy
// - Busy = 已有校验进程在跑（互斥，CoreConfigValidator::try_start 语义）
// - stdout/stderr 收集 + error_keywords（FATA/fatal/Parse config error/level=fatal）
#pragma once

#include "clashwin/core/constants.h"
#include "util/result.h"

#include <atomic>
#include <optional>
#include <string>

namespace clashwin::core::validate {

enum class ValidationStatus { Valid, Invalid, Skipped, Busy };
enum class ValidationErrorKind {
  FileMissing,
  FileRead,
  YamlSyntax,
  YamlMapping,
  ScriptSyntax,
  ScriptMissingMain,
  CoreRejected,
  ProcessTerminated,
  Timeout,
};
enum class ValidationSkipReason { Exiting, Debounced };

const char* to_string(ValidationErrorKind kind);
const char* to_string(ValidationSkipReason reason);
const char* to_string(ValidationStatus status);

// ← validate.rs:39-65 ValidationErrorKind::from_message（小写包含逐条判定）
ValidationErrorKind kind_from_message(const std::string& message);

struct ValidationOutcome {
  ValidationStatus status = ValidationStatus::Valid;
  ValidationErrorKind kind = ValidationErrorKind::CoreRejected;  // Invalid 时有效
  std::string message;
  ValidationSkipReason reason = ValidationSkipReason::Exiting;   // Skipped 时有效

  bool is_valid() const { return status == ValidationStatus::Valid; }
  // ← validate.rs::invalid_from_message：先分类再包结果
  static ValidationOutcome invalid_from_message(const std::string& message);
  static ValidationOutcome invalid(ValidationErrorKind k, const std::string& message);
  static ValidationOutcome skipped(ValidationSkipReason r);
  static ValidationOutcome busy();
  const char* display() const;  // Display 语义（测试输出用）
};

// 纯函数（可测）：← validate.rs:448-481 结果判定。
// exit_code == nullopt（被杀/信号终止）→ ProcessTerminated；非 0 或 stderr 含关键词 → Invalid
// （message = stdout 优先 → stderr → 退出码描述），否则 Valid。
ValidationOutcome judge_validation(const std::optional<int>& exit_code, const std::string& stdout_text,
                                   const std::string& stderr_text);
// ← validate.rs contains_any_keyword：FATA / fatal / Parse config error / level=fatal
bool contains_error_keyword(const std::string& buf);

// 子进程校验（沙箱内 spawn 被拦时返回 err——live 用例据此 SKIP）
struct ValidateOptions {
  std::wstring core_path;  // mini-mihomo.exe 全路径（locate_core_path）
  std::wstring app_dir;    // -d（app_home_dir）
  int timeout_ms = consts::kValidateControlTimeoutMs;
};
Result<ValidationOutcome> validate_config_file(const ValidateOptions& opt,
                                               const std::string& config_path_utf8);

// 互斥校验器（← CoreConfigValidator 单例语义）：try_start/finish 公开供测试
class Validator {
 public:
  static Validator& instance();
  // Busy → 返回 ValidationOutcome::Busy；否则 spawn 子进程校验
  Result<ValidationOutcome> validate(const ValidateOptions& opt, const std::string& config_path);
  bool try_start();
  void finish();

 private:
  std::atomic<bool> processing_{false};
};

// 生成校验副本：copy source → <home>\clash-win-check.yaml（← Config::generate_file(Check)），
// 返回 check 文件路径
Result<std::wstring> make_check_copy(const std::wstring& source);

// ---------- 校验失败通知状态（← validate.rs:527-583）----------
// 校验对象：Runtime=运行配置、Merge=覆写碎片、Script=JavaScript 覆写脚本。
// 同一种错误在不同对象上要给出不同文案（例：YamlSyntax 在 Merge 上是「覆写文件语法错误」，
// 在 Script 上是「脚本文件错误」，在 Runtime 上是「YAML 语法错误」）。
enum class ValidationNoticeTarget { Runtime, Merge, Script };

// ← validate.rs:531-553 const fn notice_key：kind × target → 状态串
const char* notice_status(ValidationErrorKind kind, ValidationNoticeTarget target);

// ← validate.rs:555-583 handle_validation_notice：
//   Invalid → 记 warn 日志 + notice_message(细分状态, msg)
//   Busy    → 只记日志（并发竞争不是真错误，弹窗会误导用户"任务失败"）
//   Skipped → notice_message("config_validate::error", 原因)
//   Valid   → 无动作
void handle_validation_notice(const ValidationOutcome& outcome, ValidationNoticeTarget target,
                              const char* file_type);

}  // namespace clashwin::core::validate
