// dirs 测试：便携/APPDATA 两分支 + 环境变量重定向（绝不写真实 %APPDATA%）
#include "test_util.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <string>

#include "util/dirs.h"
#include "util/utf.h"

using namespace clashwin::util;
using clashwin::utf::utf8_to_utf16;

namespace {

std::wstring temp_root() {
  wchar_t buf[MAX_PATH];
  GetTempPathW(MAX_PATH, buf);
  return std::wstring(buf) + L"clashwin_test_dirs";
}

void set_env(const char* name, const std::wstring& value) {
  _wputenv_s(utf8_to_utf16(name).c_str(), value.c_str());
}

}  // namespace

UT_TEST(dirs_home_override_redirects_everything) {
  // 重定向根目录 → 全部子路径落在临时目录下，绝不碰真实 %APPDATA%
  std::wstring root = temp_root() + L"\\home";
  CreateDirectoryW(temp_root().c_str(), nullptr);
  set_env("CLASHWIN_HOME_OVERRIDE", root);

  UT_CHECK_EQ(dirs::app_home_dir(), root);
  UT_CHECK_EQ(dirs::clash_path(), root + L"\\config.yaml");
  UT_CHECK_EQ(dirs::verge_path(), root + L"\\verge.yaml");
  UT_CHECK_EQ(dirs::profiles_path(), root + L"\\profiles.yaml");
  UT_CHECK_EQ(dirs::clash_mini_path(), root + L"\\clash-win.yaml");
  UT_CHECK_EQ(dirs::clash_mini_check_path(),
              root + L"\\clash-win-check.yaml");
  UT_CHECK_EQ(dirs::window_state_path(), root + L"\\window_state.json");
  UT_CHECK_EQ(dirs::proxy_head_state_path(),
              root + L"\\proxy_head_state.json");
  UT_CHECK_EQ(dirs::app_profiles_dir(), root + L"\\profiles");
  UT_CHECK_EQ(dirs::app_icons_dir(), root + L"\\icons");
  UT_CHECK_EQ(dirs::app_logs_dir(), root + L"\\logs");
  UT_CHECK_EQ(dirs::app_latest_log(), root + L"\\logs\\latest.log");
  UT_CHECK_EQ(dirs::cores_dir(), root + L"\\cores");
  UT_CHECK_EQ(dirs::ruleset_dir(), root + L"\\ruleset");

  set_env("CLASHWIN_HOME_OVERRIDE", L"");
}

UT_TEST(dirs_portable_branch) {
  // 纯函数：便携 home = <exe>\.config\io.github.clash-win.clash-win
  std::wstring fake_exe_dir = temp_root() + L"\\appbin";
  UT_CHECK_EQ(dirs::portable_home_for(fake_exe_dir),
              fake_exe_dir + L"\\.config\\io.github.clash-win.clash-win");

  // 便携标记文件存在 → is_portable（env 强制分支验证；exe 目录不可写故用覆盖）
  set_env("CLASHWIN_PORTABLE_OVERRIDE", L"1");
  UT_CHECK(dirs::is_portable());
  set_env("CLASHWIN_PORTABLE_OVERRIDE", L"0");
  UT_CHECK(!dirs::is_portable());
  set_env("CLASHWIN_PORTABLE_OVERRIDE", L"");
}

UT_TEST(dirs_appdata_branch_shape) {
  // 非便携分支 = %APPDATA%\io.github.clash-win.clash-win（只读拼接，不写盘）
  set_env("CLASHWIN_PORTABLE_OVERRIDE", L"0");
  set_env("CLASHWIN_HOME_OVERRIDE", L"");

  // 测试运行器可整进程重定向 %APPDATA% 根（防污染真实用户配置）
  wchar_t base_buf[MAX_PATH] = {};
  DWORD base_len = GetEnvironmentVariableW(L"CLASHWIN_APPDATA_OVERRIDE", base_buf, MAX_PATH);
  if (base_len > 0 && base_len < MAX_PATH) {
    std::wstring expected = std::wstring(base_buf) + L"\\io.github.clash-win.clash-win";
    UT_CHECK_EQ(dirs::app_home_dir(), expected);
    set_env("CLASHWIN_PORTABLE_OVERRIDE", L"");
    return;
  }

  PWSTR raw = nullptr;
  HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw);
  if (SUCCEEDED(hr)) {
    std::wstring expected =
        std::wstring(raw) + L"\\io.github.clash-win.clash-win";
    CoTaskMemFree(raw);
    UT_CHECK_EQ(dirs::app_home_dir(), expected);
  }
  set_env("CLASHWIN_PORTABLE_OVERRIDE", L"");
}

UT_TEST(dirs_log_dirs_create_under_override) {
  std::wstring root = temp_root() + L"\\logsroot";
  CreateDirectoryW(temp_root().c_str(), nullptr);
  set_env("CLASHWIN_HOME_OVERRIDE", root);

  std::wstring sidecar = dirs::sidecar_log_dir();
  UT_CHECK_EQ(sidecar, root + L"\\logs\\sidecar");
  if (GetFileAttributesW(sidecar.c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::printf("  [diag-dirs] root=%S home=%S sidecar=%S attr-err=%lu tmp=%S\n",
                root.c_str(), dirs::app_home_dir().c_str(), sidecar.c_str(),
                GetLastError(), temp_root().c_str());
    std::printf("  [diag-dirs] parent-exists=%d\n",
                GetFileAttributesW(temp_root().c_str()) != INVALID_FILE_ATTRIBUTES);
    std::wstring logs = root + L"\\logs";
    std::printf("  [diag-dirs] lvl root=%d logs=%d sidecar=%d\n",
                GetFileAttributesW(root.c_str()) != INVALID_FILE_ATTRIBUTES,
                GetFileAttributesW(logs.c_str()) != INVALID_FILE_ATTRIBUTES,
                GetFileAttributesW(sidecar.c_str()) != INVALID_FILE_ATTRIBUTES);
    SetLastError(0);
    BOOL ok = CreateDirectoryW(root.c_str(), nullptr);
    std::printf("  [diag-dirs] direct mkdir ok=%d err=%lu\n", (int)ok,
                GetLastError());
    // 多路径探针：定位 360/行为防护的拦截范围
    for (const wchar_t* p : {L"C:\\Users\\Public\\clashwin_probe",
                             L"C:\\clashwin_probe",
                             L"C:\\Users\\sun_y\\clashwin_probe",
                             L"C:\\Users\\sun_y\\Documents\\clashwin_probe"}) {
      SetLastError(0);
      BOOL r = CreateDirectoryW(p, nullptr);
      DWORD e = GetLastError();
      std::printf("  [diag-probe] %S ok=%d err=%lu\n", p, (int)r, e);
    }
  }
  UT_CHECK(GetFileAttributesW(sidecar.c_str()) != INVALID_FILE_ATTRIBUTES);

  std::wstring service = dirs::service_log_dir();
  UT_CHECK_EQ(service, root + L"\\logs\\service");
  UT_CHECK(GetFileAttributesW(service.c_str()) != INVALID_FILE_ATTRIBUTES);

  UT_CHECK_EQ(dirs::clash_latest_log(),
              root + L"\\logs\\sidecar\\sidecar_latest.log");

  set_env("CLASHWIN_HOME_OVERRIDE", L"");
}

UT_TEST(dirs_fixed_paths) {
  // ← dirs.rs::ipc_path / service_path
  UT_CHECK_EQ(dirs::ipc_path(), L"\\\\.\\pipe\\mini-mihomo-win");
  std::wstring res = dirs::app_resources_dir();
  UT_CHECK_EQ(dirs::service_path(), res + L"\\clash-verge-service.exe");
  // sidecar 兜底内核：<exe>\mini-mihomo.exe
  std::wstring exe_dir = dirs::exe_dir();
  UT_CHECK_EQ(dirs::sidecar_core_path(), exe_dir + L"\\mini-mihomo.exe");
}
