#pragma once
// 文件日志器：logs/latest.log，支持早于 init() 的内存缓冲、按 size 轮转（YYYY-MM-DD_HH-MM-SS.log）、
// max_count 淘汰、级别过滤，以及 temp 目录与 auto_log_clean 过期清理。
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace clashwin {

class Logger {
 public:
  static Logger& instance();

  // 以 home_dir 为根建 logs/ 并打开 latest.log；同时清理残留 temp/ 并刷新早期缓冲
  void init(const std::wstring& home_dir);
  // 运行时配置与启动期清理（max_size_kb=0 关闭按大小轮转；auto_clean_mode: 1=1d,2=7d,3=30d,4=90d）
  void configure(uint64_t max_size_kb, size_t max_count,
                 const std::string& level_str, int auto_clean_mode,
                 bool run_cleanup = true);
  // 追加一行：[时间戳][级别] 消息（线程安全）
  void write(const char* level, const std::string& msg);
  void flush();
  void shutdown();

 private:
  struct PendingLine {
    int level = 2;
    std::string text;
  };

  Logger() = default;
  void rotate_if_needed_locked(size_t incoming_bytes);
  void prune_rotated_logs_locked();
  void clean_expired_logs_locked();
  static int parse_level(const char* level);
  static int parse_level_config(const std::string& level_str);
  static int auto_clean_days(int mode);

  void* file_ = nullptr;
  bool inited_ = false;
  std::wstring base_dir_;
  std::wstring logs_dir_;
  std::wstring latest_path_;
  uint64_t current_bytes_ = 0;
  uint64_t max_size_bytes_ = 128ull * 1024ull;
  size_t max_count_ = 40;
  int min_level_ = 2;  // 0=trace,1=debug,2=info,3=warn,4=error,5=off
  int auto_clean_mode_ = 2;
  std::vector<PendingLine> pending_;
};

// 便捷宏：消息由调用方自行拼接（避免 format 注入歧义）
#define CWLOG_INFO(msg) clashwin::Logger::instance().write("INFO", (msg))
#define CWLOG_WARN(msg) clashwin::Logger::instance().write("WARN", (msg))
#define CWLOG_ERROR(msg) clashwin::Logger::instance().write("ERROR", (msg))
#define CWLOG_DEBUG(msg) clashwin::Logger::instance().write("DEBUG", (msg))

}  // namespace clashwin
