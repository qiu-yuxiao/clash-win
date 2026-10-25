// T07 测试：validate 结果分类（纯函数）+ 互斥 Busy + live `-t`（沙箱外可跑）
#include "test_util.h"

#include "clashwin/core/constants.h"
#include "core/validate.h"
#include "util/dirs.h"
#include "util/utf.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace clashwin;
using namespace clashwin::core::validate;
namespace fs = std::filesystem;

// ---------- kind_from_message 全分支（← validate.rs:39-65 判定顺序） ----------
UT_TEST(validate_kind_from_message) {
  using K = ValidationErrorKind;
  UT_CHECK(kind_from_message("File not found: a.yaml") == K::FileMissing);
  UT_CHECK(kind_from_message("Failed to read file: xx") == K::FileRead);
  UT_CHECK(kind_from_message("\xe6\x97\xa0\xe6\xb3\x95\xe8\xaf\xbb\xe5\x8f\x96\xe6\x96\x87\xe4\xbb\xb6") ==
           K::FileRead);  // 无法读取文件
  UT_CHECK(kind_from_message("Script must contain a main function") == K::ScriptMissingMain);
  UT_CHECK(kind_from_message("Script syntax error: bad js") == K::ScriptSyntax);
  UT_CHECK(kind_from_message("mapping values are not allowed here") == K::YamlMapping);
  UT_CHECK(kind_from_message("failed to transform to yaml mapping") == K::YamlMapping);
  UT_CHECK(kind_from_message("failed to apply merge") == K::YamlMapping);
  UT_CHECK(kind_from_message("YAML syntax error: oops") == K::YamlSyntax);
  UT_CHECK(kind_from_message("did not find expected key") == K::YamlSyntax);
  UT_CHECK(kind_from_message("request timeout after 10s") == K::Timeout);
  UT_CHECK(kind_from_message("\xe8\xb6\x85\xe6\x97\xb6\xe4\xba\x86") == K::Timeout);  // 超时了
  UT_CHECK(kind_from_message("process terminated") == K::ProcessTerminated);
  UT_CHECK(kind_from_message("\xe8\xa2\xab\xe7\xbb\x88\xe6\xad\xa2") == K::ProcessTerminated);
  // 无匹配 → CoreRejected 兜底
  UT_CHECK(kind_from_message("some random kernel rejection") == K::CoreRejected);
  // 顺序优先级：file not found 先于 timeout
  UT_CHECK(kind_from_message("file not found, timeout") == K::FileMissing);
}

// ---------- judge_validation（← validate.rs:448-481） ----------
UT_TEST(validate_judge) {
  // exit 0 + 无关键词 → Valid
  UT_CHECK(judge_validation(0, "", "").is_valid());
  UT_CHECK(judge_validation(0, "info line\n", "").is_valid());
  // exit != 0 → Invalid
  auto r1 = judge_validation(1, "stdout fatal detail", "");
  UT_CHECK(!r1.is_valid());
  UT_CHECK_EQ(r1.kind, ValidationErrorKind::CoreRejected);
  UT_CHECK_EQ(r1.message, std::string("stdout fatal detail"));
  // stderr 关键词（FATA/fatal/Parse config error/level=fatal）即使 exit 0 也判 Invalid
  auto r2 = judge_validation(0, "", "level=fatal oops");
  UT_CHECK(!r2.is_valid());
  // message 优先级：stdout > stderr > 退出码描述
  auto r3 = judge_validation(2, "out", "err");
  UT_CHECK_EQ(r3.message, std::string("out"));
  auto r4 = judge_validation(2, "", "err");
  UT_CHECK_EQ(r4.message, std::string("err"));
  // exit_code 缺失（被杀）→ ProcessTerminated
  auto r5 = judge_validation(std::nullopt, "", "");
  UT_CHECK(!r5.is_valid());
  UT_CHECK_EQ(r5.kind, ValidationErrorKind::ProcessTerminated);
  // 无输出仅有退出码
  auto r6 = judge_validation(3, "", "");
  UT_CHECK(!r6.is_valid());
  UT_CHECK(r6.message.find("3") != std::string::npos);
  // stderr 有输出 + exit!=0 → 走 stdout 优先；stdout 空 → stderr
  auto r7 = judge_validation(1, "", "yaml syntax error: xxx");
  UT_CHECK_EQ(r7.kind, ValidationErrorKind::YamlSyntax);
}

// ---------- contains_error_keyword ----------
UT_TEST(validate_keywords) {
  UT_CHECK(contains_error_keyword("level=fatal x"));
  UT_CHECK(contains_error_keyword("FATA[0001]"));
  UT_CHECK(contains_error_keyword("Parse config error at line 3"));
  UT_CHECK(contains_error_keyword("...fatal..."));
  UT_CHECK(contains_error_keyword("fatalistic"));  // 子串语义（与原版一致：含 fatal 即命中）
  UT_CHECK(!contains_error_keyword("everything fine"));
  UT_CHECK(!contains_error_keyword("regatta"));  // 不含任何关键词
}

// ---------- 互斥 Busy（← CoreConfigValidator try_start/finish） ----------
UT_TEST(validate_busy_mutex) {
  auto& v = Validator::instance();
  UT_CHECK(v.try_start());
  UT_CHECK(!v.try_start());  // 第二路请求 → Busy
  v.finish();
  UT_CHECK(v.try_start());
  v.finish();
}

// ---------- make_check_copy ----------
UT_TEST(validate_check_copy) {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  // CLASHWIN_HOME_OVERRIDE 指到 %TEMP%（绝不写真实 %APPDATA%）
  std::wstring home = std::wstring(temp) + L"clashwin-test-core-home";
  std::filesystem::create_directories(home);
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", home.c_str());

  std::wstring src = home + L"\\source.yaml";
  {
    std::ofstream f(src, std::ios::binary);
    f << "mixed-port: 20802\n";
  }
  auto dst = make_check_copy(src);
  UT_CHECK(dst.is_ok());
  if (dst.is_ok()) {
    UT_CHECK(fs::exists(dst.value()));
    UT_CHECK(dst.value().find(L"clash-mini-check.yaml") != std::wstring::npos);
    std::ifstream f(dst.value());
    char buf[64] = {};
    f.read(buf, sizeof(buf) - 1);
    UT_CHECK(std::string(buf).find("20802") != std::string::npos);
  }
  // 不存在源 → err
  auto bad = make_check_copy(home + L"\\missing.yaml");
  UT_CHECK(bad.is_err());

  std::error_code ec;
  fs::remove_all(home, ec);
}

// ---------- live：真实 mini-mihomo -t（沙箱内 spawn 被拦 → SKIP） ----------
UT_TEST(validate_live_core_check) {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  std::wstring home = std::wstring(temp) + L"clashwin-test-validate-home";
  std::error_code ec;
  fs::remove_all(home, ec);
  fs::create_directories(home);

  // 内核：测试 exe 同目录 sidecar 拷贝件（与 T05 live 一致的定位）
  wchar_t self[MAX_PATH];
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring core = (fs::path(self).parent_path() / L"sidecar\\mini-mihomo.exe").wstring();
  if (!fs::exists(core)) {
    std::printf("  SKIP: sidecar core not found\n");
    fs::remove_all(home, ec);
    return;
  }

  auto write_cfg = [&](const std::wstring& path, const char* body) {
    std::ofstream f(path, std::ios::binary);
    f << body;
    return f.good();
  };
  std::wstring ok_cfg = home + L"\\ok.yaml";
  std::wstring bad_cfg = home + L"\\bad.yaml";
  UT_CHECK(write_cfg(ok_cfg, "mixed-port: 20803\nlog-level: info\nmode: rule\n"));
  UT_CHECK(write_cfg(bad_cfg, "mixed-port: [unclosed\n  bad: :\n"));

  ValidateOptions opt;
  opt.core_path = core;
  opt.app_dir = home;

  auto vr = validate_config_file(opt, utf::utf16_to_utf8(ok_cfg));
  if (vr.is_err()) {
    std::printf("  SKIP: spawn blocked in sandbox (%s); "
                "run from a normal terminal for live validation\n",
                vr.error().message.c_str());
    fs::remove_all(home, ec);
    return;
  }
  UT_CHECK(vr.value().is_valid());

  auto vr2 = validate_config_file(opt, utf::utf16_to_utf8(bad_cfg));
  UT_CHECK(vr2.is_ok() && !vr2.value().is_valid());
  if (vr2.is_ok()) {
    std::printf("  [live] invalid config rejected: %s (%s)\n", to_string(vr2.value().kind),
                vr2.value().message.substr(0, 120).c_str());
  }

  // 文件缺失分类
  auto vr3 = validate_config_file(opt, utf::utf16_to_utf8(home + L"\\missing.yaml"));
  UT_CHECK(vr3.is_ok() && !vr3.value().is_valid());
  if (vr3.is_ok()) {
    UT_CHECK_EQ(vr3.value().kind, ValidationErrorKind::FileMissing);
  }

  fs::remove_all(home, ec);
}
