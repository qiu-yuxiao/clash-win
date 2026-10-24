#pragma once
// 简单文件日志器：logs/clashwin.log 追加写，时间戳+级别，线程安全（mutex）
// T01 仅实现门面所需最小集；级别过滤/滚动/max_count 在 T02 按 IVerge 对齐。
#include <string>

namespace clashwin {

class Logger {
 public:
  static Logger& instance();

  // 以 exe 所在目录为根建 logs/ 并打开日志文件；失败则静默降级
  void init(const std::wstring& exe_dir);
  // 追加一行：[时间戳][级别] 消息（线程安全）
  void write(const char* level, const std::string& msg);
  void flush();
  void shutdown();

 private:
  Logger() = default;
  // void* 避免在头文件暴露 windows.h；实际为 HANDLE
  void* file_ = nullptr;
  bool inited_ = false;
};

// 便捷宏：消息由调用方自行拼接（避免 format 注入歧义）
#define CWLOG_INFO(msg) clashwin::Logger::instance().write("INFO", (msg))
#define CWLOG_WARN(msg) clashwin::Logger::instance().write("WARN", (msg))
#define CWLOG_ERROR(msg) clashwin::Logger::instance().write("ERROR", (msg))
#define CWLOG_DEBUG(msg) clashwin::Logger::instance().write("DEBUG", (msg))

}  // namespace clashwin
