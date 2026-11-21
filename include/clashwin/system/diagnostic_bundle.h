#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "util/result.h"

namespace clashwin::system {

class DiagnosticBundle {
 public:
  struct ZipEntry {
    std::string rel_path;  // Must use '/'
    std::string content;
  };

  // Main command implementation: returns absolute path to generated .zip
  static Result<std::string> export_diagnostic_bundle();

  // Pure redaction functions (fully testable)
  static std::string redact_log_text(const std::string& text);
  static std::string redact_yaml_text(const std::string& yaml_text);

  // BUG-416：WER 事件块 / CrashDumps 文件名命中判定（入参须已小写）。
  // WER 额外命中带空格的「clash win」显示名；转储文件名不含空格形态。
  static bool is_clash_wer_target(const std::string& lower);
  static bool is_clash_dump_target(const std::string& lower);

  // Log tail truncation (caps at max_bytes, trims to newline, prepends notice)
  static std::string tail_truncate_log(const std::string& text, size_t max_bytes);

  // System info collector (version, mode, tun, sniffer, sysproxy, node count)
  static std::string collect_system_info();

  // A4 WER + 360 read-only crash/whitelist environment report
  static std::string collect_wer_and_av_report();

  // ZIP packaging helper (PKZIP STORE format with CRC32)
  static Result<void> write_zip_file(const std::wstring& zip_path,
                                    const std::vector<ZipEntry>& entries);
};

}  // namespace clashwin::system
