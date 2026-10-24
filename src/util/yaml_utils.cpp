#include "util/yaml_utils.h"

#include <windows.h>

#include <cstdio>
#include <fstream>

#include "util/utf.h"

namespace clashwin::util {

namespace {

std::string strip_bom(const std::string& s) {
  if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
      static_cast<unsigned char>(s[1]) == 0xBB &&
      static_cast<unsigned char>(s[2]) == 0xBF)
    return s.substr(3);
  return s;
}

bool write_file_binary(const std::wstring& path, const std::string& data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(f);
}

bool read_file_binary_full(const std::wstring& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign((std::istreambuf_iterator<char>(f)),
             std::istreambuf_iterator<char>());
  return true;
}

// ← help.rs::move_file_ex_replace：原子替换
bool move_file_ex_replace(const std::wstring& src, const std::wstring& dst) {
  return MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

}  // namespace

Result<YAML::Node> load_yaml_file(const std::wstring& path) {
  std::string raw;
  if (!read_file_binary_full(path, raw)) {
    Error e;
    e.code = 1;
    e.message = "file not found \"" + utf::utf16_to_utf8(path) + "\"";
    return Result<YAML::Node>::err(std::move(e));
  }
  std::string content = strip_bom(raw);
  try {
    return Result<YAML::Node>::ok(YAML::Load(strip_bom(content)));
  } catch (const YAML::Exception& ex) {
    Error e;
    e.code = 2;
    e.message = "YAML syntax error in " + utf::utf16_to_utf8(path) + ": " +
                ex.what();
    return Result<YAML::Node>::err(std::move(e));
  }
}

Result<std::string> emit_yaml(const YAML::Node& node) {
  try {
    std::string out = YAML::Dump(node);
    if (out.empty() || out.back() != '\n') out += '\n';
    return Result<std::string>::ok(std::move(out));
  } catch (const YAML::Exception& ex) {
    Error e;
    e.code = 3;
    e.message = std::string("failed to serialize yaml: ") + ex.what();
    return Result<std::string>::err(std::move(e));
  }
}

Result<void> save_yaml_file(const std::wstring& path, const YAML::Node& node,
                            const std::string& prefix) {
  auto ser = emit_yaml(node);
  if (!ser.is_ok()) {
    Error e = ser.error();
    return Result<void>::err(std::move(e));
  }
  std::string data = prefix.empty()
                         ? ser.value()
                         : prefix + "\n\n" + ser.value();

  // 内容未变化时跳过写入（← help.rs，减少磁盘 IO）
  std::string existing;
  if (read_file_binary_full(path, existing) && existing == data)
    return Result<void>::ok();

  // 原子写入：tmp + MoveFileExW（← help.rs:82-105）
  size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    Error e;
    e.code = 4;
    e.message = "failed to get parent directory";
    return Result<void>::err(std::move(e));
  }
  wchar_t pid_buf[16];
  std::swprintf(pid_buf, 16, L"%u", static_cast<unsigned>(GetCurrentProcessId()));
  std::wstring tmp = path.substr(0, slash + 1) +
                     path.substr(slash + 1) + L".tmp_" + pid_buf;
  if (!write_file_binary(tmp, data)) {
    Error e;
    e.code = 5;
    e.message = "failed to write temp file \"" + utf::utf16_to_utf8(tmp) + "\"";
    return Result<void>::err(std::move(e));
  }
  if (!move_file_ex_replace(tmp, path)) {
    // ← help.rs：rename 失败把 tmp 留作 .bak
    std::wstring bak = path + L".bak";
    MoveFileExW(tmp.c_str(), bak.c_str(), MOVEFILE_REPLACE_EXISTING);
    Error e;
    e.code = 6;
    e.message = "failed to save file \"" + utf::utf16_to_utf8(path) +
                "\" (atomic rename failed)";
    return Result<void>::err(std::move(e));
  }
  return Result<void>::ok();
}

void deep_merge(YAML::Node& base, const YAML::Node& overlay) {
  if (!overlay.IsMap() || !base.IsMap()) return;  // 仅 map 参与递归
  for (const auto& kv : overlay) {
    // 坑：以 Node 作键的 operator[] 按节点对象身份（is()）比较，不按标量值，
    // 跨文档合并会插出重复键；必须转成标量字符串走标量比较。
    if (!kv.first.IsScalar()) continue;  // 非标量键不属配置域，跳过
    const std::string key = kv.first.Scalar();
    const YAML::Node& ov = kv.second;
    YAML::Node existing = base[key];
    if (existing && existing.IsMap() && ov.IsMap()) {
      deep_merge(existing, ov);
    } else {
      base[key] = ov;  // 覆盖/追加（yaml-cpp 保序语义）
    }
  }
}

void set_key(YAML::Node& map, const std::string& key, const YAML::Node& value) {
  map[key] = value;  // yaml-cpp：旧键原位覆盖，新键尾部追加
}

}  // namespace clashwin::util
