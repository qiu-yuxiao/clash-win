#include "util/dirs.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdlib>

#include "util/log.h"

namespace clashwin::util::dirs {

namespace {

std::wstring join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (!b.empty() && (b[0] == L'\\' || b[0] == L'/')) return a + b;
  return a + L"\\" + b;
}

std::wstring env_or_empty(const char* name) {
  char* v = nullptr;
  size_t len = 0;
  _dupenv_s(&v, &len, name);
  std::wstring out;
  if (v) {
    out.assign(v, v + (len > 0 ? len - 1 : 0));  // 去掉结尾 NUL
    std::free(v);
  }
  return out;
}

// %APPDATA%（FOLDERID_RoamingAppData，← Tauri path().data_dir() Windows 语义）
std::wstring roaming_appdata() {
  PWSTR raw = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw)))
    return {};
  std::wstring out(raw);
  CoTaskMemFree(raw);
  return out;
}

bool file_exists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES &&
         !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

namespace {

bool g_portable_cached = false;
bool g_portable = false;

bool file_exists(const std::wstring& path);  // 见下

void detect_portable() {
  // 调试覆盖每次读取（测试进程内可切换分支）；真实探测只做一次
  std::wstring ov = env_or_empty("CLASHWIN_PORTABLE_OVERRIDE");
  if (ov == L"1") {
    g_portable = true;
    return;
  }
  if (ov == L"0") {
    g_portable = false;
    return;
  }
  if (!g_portable_cached) {
    // ← dirs.rs:26 init_portable_flag：仅当 <exe>\.config\PORTABLE 存在才启用便携模式
    // （单有 .config 目录不算——便携数据目录本身就在 .config 下，不能反推开关）
    std::wstring portable_file = join(join(exe_dir(), L".config"), L"PORTABLE");
    DWORD attr_file = GetFileAttributesW(portable_file.c_str());
    g_portable = (attr_file != INVALID_FILE_ATTRIBUTES &&
                  !(attr_file & FILE_ATTRIBUTE_DIRECTORY));
    g_portable_cached = true;
  }
}

// ← help.rs 的 create_dir_all 语义（Rust 侧多函数隐含多级创建）
void create_dirs_all(const std::wstring& path) {
  if (path.empty()) return;
  std::wstring cur;
  size_t i = 0;
  if (path.size() >= 2 && path[1] == L':') {  // 盘符
    cur = path.substr(0, 2);
    i = 2;
  }
  while (i < path.size()) {
    size_t next = path.find_first_of(L"\\/", i);
    if (next == std::wstring::npos) next = path.size();
    if (next > i) {
      if (cur.empty())
        cur = path.substr(0, next);
      else
        cur += L"\\" + path.substr(i, next - i);
      CreateDirectoryW(cur.c_str(), nullptr);  // 已存在则无害
    }
    i = next + 1;
  }
}

}  // namespace

void init_portable_flag() { detect_portable(); }

bool is_portable() {
  detect_portable();
  return g_portable;
}

void reset_portable_cache_for_testing() {
  g_portable_cached = false;
  g_portable = false;
}

std::wstring exe_dir() {
  wchar_t exe_path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  std::wstring dir(exe_path);
  size_t pos = dir.find_last_of(L"\\/");
  if (pos == std::wstring::npos) return {};
  return dir.substr(0, pos);
}

std::wstring portable_home_for(const std::wstring& exe_directory) {
  // ← dirs.rs:44-47：<exe>\.config\APP_ID
  return join(join(exe_directory, L".config"), kAppId);
}

std::wstring app_home_dir() {
  // ← dirs.rs::app_home_dir
  std::wstring home_override = env_or_empty("CLASHWIN_HOME_OVERRIDE");
  if (!home_override.empty()) return home_override;  // 测试重定向，绝不碰真实目录

  if (is_portable()) return portable_home_for(exe_dir());

  // 测试隔离钩子：整进程把 %APPDATA% 根重定向（即便某个用例误清 HOME_OVERRIDE，
  // 也绝不会写真实用户配置）；生产运行不设置该变量。
  std::wstring appdata_override = env_or_empty("CLASHWIN_APPDATA_OVERRIDE");
  std::wstring data = appdata_override.empty() ? roaming_appdata() : appdata_override;
  if (data.empty()) {
    CWLOG_ERROR("Failed to get the app home directory");
    return {};
  }
  return join(data, kAppId);
}

std::wstring app_resources_dir() {
  // ← dirs.rs::app_resources_dir（Tauri resource_dir 等价于 exe 同级）
  return join(exe_dir(), L"resources");
}

std::wstring app_profiles_dir() { return join(app_home_dir(), L"profiles"); }
std::wstring app_icons_dir() { return join(app_home_dir(), L"icons"); }
std::wstring app_logs_dir() { return join(app_home_dir(), L"logs"); }
std::wstring app_latest_log() { return join(app_logs_dir(), L"latest.log"); }
std::wstring clash_path() { return join(app_home_dir(), kClashConfig); }
std::wstring verge_path() { return join(app_home_dir(), kVergeConfig); }
std::wstring profiles_path() { return join(app_home_dir(), kProfileYaml); }
std::wstring service_path() {
  return join(app_resources_dir(), L"clash-verge-service.exe");
}
std::wstring clash_mini_path() { return join(app_home_dir(), L"clash-win.yaml"); }
std::wstring clash_mini_check_path() {
  return join(app_home_dir(), L"clash-win-check.yaml");
}
std::wstring window_state_path() {
  return join(app_home_dir(), L"window_state.json");
}
std::wstring proxy_head_state_path() {
  return join(app_home_dir(), L"proxy_head_state.json");
}
std::wstring cores_dir() { return join(app_home_dir(), L"cores"); }
std::wstring ruleset_dir() { return join(app_home_dir(), L"ruleset"); }
std::wstring sidecar_core_path() { return join(exe_dir(), L"mini-mihomo.exe"); }

std::wstring sidecar_log_dir() {
  std::wstring dir = join(app_logs_dir(), L"sidecar");
  create_dirs_all(dir);
  return dir;
}

std::wstring service_log_dir() {
  std::wstring dir = join(app_logs_dir(), L"service");
  create_dirs_all(dir);
  return dir;
}

std::wstring clash_latest_log() {
  // ← dirs.rs::clash_latest_log（RunningMode 判定在 T07 接入，此处按非 Service）
  // 注意：调用方（core 层）已知运行模式；这里保持与 dirs.rs 相同的两分支形态。
  return join(sidecar_log_dir(), L"sidecar_latest.log");
}

std::wstring ipc_path() { return L"\\\\.\\pipe\\mini-mihomo"; }

}  // namespace clashwin::util::dirs
