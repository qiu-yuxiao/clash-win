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

  // Log tail truncation (caps at max_bytes, trims to newline, prepends notice)
  static std::string tail_truncate_log(const std::string& text, size_t max_bytes);

  // System info collector (version, mode, tun, sniffer, guardian, sysproxy, node count)
  static std::string collect_system_info();

  // A4 WER + 360 read-only crash/whitelist environment report
  static std::string collect_wer_and_av_report();

  // ZIP packaging helper (PKZIP STORE format with CRC32)
  static Result<void> write_zip_file(const std::wstring& zip_path,
                                    const std::vector<ZipEntry>& entries);
};

}  // namespace clashwin::system
