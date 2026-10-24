#pragma once
// T04 管道 HTTP/WS 客户端层 · 公共类型（← 05文档 §3.3 / 02§6）
// 错误分类枚举供 T05（mihomo 客户端）/T07（内核生命周期）做降级判定：
//   PipeNotFound → 管道未就绪（内核未启动/服务未装），可重试等待；
//   Busy         → ERROR_PIPE_BUSY 重试耗尽（BUG-171/172 语义）；
//   Timeout      → 请求超时（含池等待超时）；
//   Protocol     → HTTP/1.1 或 WS 帧解析失败；
//   PoolFull     → 连接池耗尽且等待超时（拒绝策略 Timeout）；
//   Other        → 其余 I/O 错误。
#include <map>
#include <string>

namespace clashwin::mihomo {

enum class HttpErrc : int {
  PipeNotFound = 1001,
  Busy = 1002,
  Timeout = 1003,
  Protocol = 1004,
  PoolFull = 1005,
  Other = 1006,
};

const char* errc_name(HttpErrc e);

struct HttpRequest {
  std::string method;
  std::string path;
  std::map<std::string, std::string> headers;
  std::string body;
};

struct HttpResponse {
  int status = 0;
  std::map<std::string, std::string> headers;  // 键统一小写
  std::string body;

  bool is_success() const { return status >= 200 && status < 300; }
  // 大小写不敏感取头
  const std::string* header(const char* name) const {
    std::string key = name;
    for (auto& c : key) c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    auto it = headers.find(key);
    return it == headers.end() ? nullptr : &it->second;
  }
};

}  // namespace clashwin::mihomo
