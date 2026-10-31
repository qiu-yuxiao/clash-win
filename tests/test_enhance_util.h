#pragma once
// T08 测试公共工具：EnhanceInput 构造 / golden 轨重排 diff
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "enhance/enhance.h"
#include "test_util.h"
#include "util/utf.h"

namespace enhance_test {

using namespace clashwin::utf;

inline std::wstring temp_dir(const char* tag) {
  wchar_t buf[MAX_PATH];
  GetTempPathW(MAX_PATH, buf);
  std::wstring root = std::wstring(buf) + utf8_to_utf16(tag);
  CreateDirectoryW(root.c_str(), nullptr);
  return root;
}

inline std::wstring real_home() {
  const char* appdata = std::getenv("APPDATA");
  if (!appdata) return L"";
  return utf8_to_utf16(std::string(appdata)) +
         L"\\io.github.clash-win.clash-win";
}

inline bool dir_exists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

inline std::string read_file_str(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// 顶层键序重排：按 reference 顺序取 ours 键（ours 独有键按原序追加尾部）
inline YAML::Node reorder_top_keys(const YAML::Node& ours,
                                   const YAML::Node& reference) {
  YAML::Node out(YAML::NodeType::Map);
  for (const auto& kv : reference) {
    const YAML::Node v = static_cast<const YAML::Node&>(ours)[kv.first.Scalar()];
    if (v.IsDefined()) out[kv.first.Scalar()] = v;
  }
  for (const auto& kv : ours) {
    const YAML::Node v =
        static_cast<const YAML::Node&>(reference)[kv.first.Scalar()];
    if (!v.IsDefined()) out[kv.first.Scalar()] = kv.second;
  }
  return out;
}

inline std::vector<std::pair<size_t, std::string>> diff_lines(
    const std::string& a, const std::string& b, size_t max_report = 12) {
  std::vector<std::string> la, lb;
  {
    std::istringstream sa(a), sb(b);
    std::string line;
    while (std::getline(sa, line)) la.push_back(line);
    while (std::getline(sb, line)) lb.push_back(line);
  }
  std::vector<std::pair<size_t, std::string>> diffs;
  size_t n = la.size() < lb.size() ? la.size() : lb.size();
  for (size_t i = 0; i < n; ++i) {
    if (la[i] != lb[i]) {
      diffs.push_back({i + 1, "ours: [" + la[i] + "] golden: [" + lb[i] + "]"});
      if (diffs.size() >= max_report) return diffs;
    }
  }
  if (la.size() != lb.size() && diffs.size() < max_report) {
    std::ostringstream os;
    os << "line count: ours=" << la.size() << " golden=" << lb.size();
    diffs.push_back({n + 1, os.str()});
  }
  return diffs;
}

}  // namespace enhance_test
