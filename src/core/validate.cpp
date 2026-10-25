// T07 配置校验实现（对照 ClashVerge core/validate.rs）
#include "core/validate.h"

#include <windows.h>

#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace clashwin::core::validate {

namespace fs = std::filesystem;

const char* to_string(ValidationErrorKind kind) {
  switch (kind) {
    case ValidationErrorKind::FileMissing: return "FileMissing";
    case ValidationErrorKind::FileRead: return "FileRead";
    case ValidationErrorKind::YamlSyntax: return "YamlSyntax";
    case ValidationErrorKind::YamlMapping: return "YamlMapping";
    case ValidationErrorKind::ScriptSyntax: return "ScriptSyntax";
    case ValidationErrorKind::ScriptMissingMain: return "ScriptMissingMain";
    case ValidationErrorKind::CoreRejected: return "CoreRejected";
    case ValidationErrorKind::ProcessTerminated: return "ProcessTerminated";
    case ValidationErrorKind::Timeout: return "Timeout";
  }
  return "CoreRejected";
}

const char* to_string(ValidationSkipReason reason) {
  return reason == ValidationSkipReason::Exiting ? "application is exiting" : "debounced";
}

const char* to_string(ValidationStatus status) {
  switch (status) {
    case ValidationStatus::Valid: return "Valid";
    case ValidationStatus::Invalid: return "Invalid";
    case ValidationStatus::Skipped: return "Skipped";
    case ValidationStatus::Busy: return "Busy";
  }
  return "Valid";
}

ValidationErrorKind kind_from_message(const std::string& message) {
  // ← validate.rs:39-65（to_ascii_lowercase + contains 逐条，判定顺序保持一致）
  std::string lower = message;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
    return static_cast<char>(::tolower(c));
  });
  auto has = [&lower](const char* needle) { return lower.find(needle) != std::string::npos; };

  if (has("file not found")) return ValidationErrorKind::FileMissing;
  if (has("failed to read") || has("\xe6\x97\xa0\xe6\xb3\x95\xe8\xaf\xbb\xe5\x8f\x96")) {
    return ValidationErrorKind::FileRead;  // 无法读取
  }
  if (has("script must contain a main function")) return ValidationErrorKind::ScriptMissingMain;
  if (has("script syntax error")) return ValidationErrorKind::ScriptSyntax;
  if (has("mapping values are not allowed") || has("failed to transform to yaml mapping") ||
      has("failed to apply merge")) {
    return ValidationErrorKind::YamlMapping;
  }
  if (has("yaml syntax error") || has("did not find expected key")) {
    return ValidationErrorKind::YamlSyntax;
  }
  if (has("timeout") || has("\xe8\xb6\x85\xe6\x97\xb6")) {
    return ValidationErrorKind::Timeout;  // 超时
  }
  if (has("terminated") || has("\xe8\xa2\xab\xe7\xbb\x88\xe6\xad\xa2")) {
    return ValidationErrorKind::ProcessTerminated;  // 被终止
  }
  return ValidationErrorKind::CoreRejected;
}

bool contains_error_keyword(const std::string& buf) {
  // ← validate.rs:449 error_keywords
  static const char* kKeywords[] = {"FATA", "fatal", "Parse config error", "level=fatal"};
  for (const char* kw : kKeywords) {
    if (buf.find(kw) != std::string::npos) return true;
  }
  return false;
}

ValidationOutcome ValidationOutcome::invalid(ValidationErrorKind k, const std::string& message) {
  ValidationOutcome o;
  o.status = ValidationStatus::Invalid;
  o.kind = k;
  o.message = message;
  return o;
}

ValidationOutcome ValidationOutcome::invalid_from_message(const std::string& message) {
  return invalid(kind_from_message(message), message);
}

ValidationOutcome ValidationOutcome::skipped(ValidationSkipReason r) {
  ValidationOutcome o;
  o.status = ValidationStatus::Skipped;
  o.reason = r;
  return o;
}

ValidationOutcome ValidationOutcome::busy() {
  ValidationOutcome o;
  o.status = ValidationStatus::Busy;
  return o;
}

const char* ValidationOutcome::display() const {
  switch (status) {
    case ValidationStatus::Valid: return "configuration is valid";
    case ValidationStatus::Invalid: return message.c_str();
    case ValidationStatus::Skipped: return to_string(reason);
    case ValidationStatus::Busy: return "Configuration validation is already running";
  }
  return "";
}

ValidationOutcome judge_validation(const std::optional<int>& exit_code,
                                   const std::string& stdout_text,
                                   const std::string& stderr_text) {
  // ← validate.rs:448-481
  bool has_error = exit_code.value_or(-1) != 0 || contains_error_keyword(stderr_text);
  if (!has_error) return ValidationOutcome{};
  std::string msg;
  if (!stdout_text.empty()) {
    msg = stdout_text;
  } else if (!stderr_text.empty()) {
    msg = stderr_text;
  } else if (exit_code.has_value()) {
    msg = "\xe9\xaa\x8c\xe8\xaf\x81\xe8\xbf\x9b\xe7\xa8\x8b\xe5\xbc\x82\xe5\xb8\xb8\xe9\x80\x80\xe5\x87\xba\xef\xbc\x8c\xe9\x80\x80\xe5\x87\xba\xe7\xa0\x81: " +
          std::to_string(*exit_code);  // 验证进程异常退出，退出码:
  } else {
    msg = "\xe9\xaa\x8c\xe8\xaf\x81\xe8\xbf\x9b\xe7\xa8\x8b\xe8\xa2\xab\xe7\xbb\x88\xe6\xad\xa2";  // 验证进程被终止
  }
  if (!exit_code.has_value()) {
    return ValidationOutcome::invalid(ValidationErrorKind::ProcessTerminated, msg);
  }
  return ValidationOutcome::invalid_from_message(msg);
}

namespace {
// 子进程运行 mini-mihomo -t，收集 stdout/stderr，10s 超时显式 kill（← validate.rs:371-446
// 进程泄漏修复：手动 spawn + 收集 + 超时 kill，不用丢弃句柄的 output()）
Result<ValidationOutcome> run_core_check(const ValidateOptions& opt,
                                         const std::string& config_path_utf8) {
  std::wstring cmd = L"\"" + opt.core_path + L"\" -t -d \"" + opt.app_dir + L"\" -f \"" +
                     utf::utf8_to_utf16(config_path_utf8) + L"\"";
  CWLOG_INFO("validating config: " + config_path_utf8);

  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
  if (!::CreatePipe(&out_r, &out_w, &sa, 0) || !::CreatePipe(&err_r, &err_w, &sa, 0)) {
    return Result<ValidationOutcome>::err(
        {2320, "CreatePipe failed, gle=" + std::to_string(::GetLastError())});
  }
  ::SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  ::SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = out_w;
  si.hStdError = err_w;
  PROCESS_INFORMATION pi{};
  if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi)) {
    ::CloseHandle(out_r);
    ::CloseHandle(out_w);
    ::CloseHandle(err_r);
    ::CloseHandle(err_w);
    DWORD gle = ::GetLastError();
    CWLOG_ERROR("validate CreateProcessW failed, gle=" + std::to_string(gle));
    return Result<ValidationOutcome>::err({2321, "CreateProcessW(mini-mihomo -t) failed, gle=" +
                                                     std::to_string(gle)});
  }
  ::CloseHandle(pi.hThread);
  ::CloseHandle(out_w);
  ::CloseHandle(err_w);

  // 收集输出（读端句柄读线程阻塞；超时 kill 后 EOF 自然收尾）
  std::string stdout_buf, stderr_buf;
  auto drain = [&](HANDLE h, std::string& into) {
    char buf[4096];
    DWORD got = 0;
    while (::ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) {
      into.append(buf, got);
    }
  };
  std::thread tout([&]() { drain(out_r, stdout_buf); });
  std::thread terr([&]() { drain(err_r, stderr_buf); });

  bool timed_out = false;
  DWORD wait = ::WaitForSingleObject(pi.hProcess, static_cast<DWORD>(opt.timeout_ms));
  if (wait == WAIT_TIMEOUT) {
    timed_out = true;
    CWLOG_WARN("validation process timed out, killing it");
    ::TerminateProcess(pi.hProcess, 1);
  }
  std::optional<int> exit_code;
  {
    DWORD code = 0;
    if (::GetExitCodeProcess(pi.hProcess, &code) && code != STILL_ACTIVE) {
      exit_code = static_cast<int>(code);
    }
  }
  ::CloseHandle(pi.hProcess);
  tout.join();
  terr.join();
  ::CloseHandle(out_r);
  ::CloseHandle(err_r);

  if (timed_out) {
    // ← validate.rs:442-445：超时返回 Err（上层按错误处理，非四分类）
    return Result<ValidationOutcome>::err(
        {2322, "Validation process timed out after " + std::to_string(opt.timeout_ms / 1000) +
                   " seconds"});
  }

  CWLOG_INFO("-------- validation result: exit=" +
             (exit_code ? std::to_string(*exit_code) : std::string("killed")) +
             " stdout=" + std::to_string(stdout_buf.size()) + "B stderr=" +
             std::to_string(stderr_buf.size()) + "B");
  auto outcome = judge_validation(exit_code, stdout_buf, stderr_buf);
  return Result<ValidationOutcome>::ok(std::move(outcome));
}
}  // namespace

Result<ValidationOutcome> validate_config_file(const ValidateOptions& opt,
                                               const std::string& config_path_utf8) {
  // ← validate.rs:301-304 文件存在性
  if (!fs::exists(utf::utf8_to_utf16(config_path_utf8))) {
    return Result<ValidationOutcome>::ok(ValidationOutcome::invalid_from_message(
        "File not found: " + config_path_utf8));
  }
  if (opt.core_path.empty() || !fs::exists(opt.core_path)) {
    return Result<ValidationOutcome>::err({2323, "core not found for validation"});
  }
  return run_core_check(opt, config_path_utf8);
}

Validator& Validator::instance() {
  static Validator v;
  return v;
}

bool Validator::try_start() {
  // ← validate.rs:128-134（swap 互斥）
  bool expected = false;
  return processing_.compare_exchange_strong(expected, true);
}

void Validator::finish() { processing_.store(false); }

Result<ValidationOutcome> Validator::validate(const ValidateOptions& opt,
                                              const std::string& config_path) {
  if (!try_start()) {
    return Result<ValidationOutcome>::ok(ValidationOutcome::busy());
  }
  struct Fin {
    Validator* v;
    ~Fin() { v->finish(); }
  } fin{this};
  return validate_config_file(opt, config_path);
}

Result<std::wstring> make_check_copy(const std::wstring& source) {
  auto dst = clashwin::util::dirs::clash_mini_check_path();
  std::error_code ec;
  fs::copy_file(source, dst, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    return Result<std::wstring>::err({2324, "copy to clash-mini-check.yaml failed: " + ec.message()});
  }
  return Result<std::wstring>::ok(dst);
}

}  // namespace clashwin::core::validate
