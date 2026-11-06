// T03 门禁测试⑤：window_state 1s 节流 + 尾随补写 + 版本号防乱序 +
// 0×0 守卫 + 脱屏居中（对照 window_manager.rs 常量与语义）
#include <windows.h>

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "clashwin/config/constants.h"
#include "config/window_state.h"
#include "util/str_utils.h"
#include "test_util.h"
#include "util/utf.h"

using namespace clashwin;
using namespace clashwin::config;
using namespace clashwin::util;
using clashwin::utf::utf8_to_utf16;
using clashwin::utf::utf16_to_utf8;
using namespace clashwin::config::consts;

namespace {

std::wstring temp_path(const char* tag) {
  wchar_t buf[MAX_PATH];
  GetTempPathW(MAX_PATH, buf);
  CreateDirectoryW((std::wstring(buf) + L"clashwin_t03").c_str(), nullptr);
  std::wstring dir = std::wstring(buf) + L"clashwin_t03\\" + utf::utf8_to_utf16(tag);
  CreateDirectoryW(dir.c_str(), nullptr);
  DeleteFileW((dir + L"\\window_state.json").c_str());
  return dir + L"\\window_state.json";
}

std::optional<nlohmann::json> read_json(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::nullopt;
  std::string s((std::istreambuf_iterator<char>(f)),
                std::istreambuf_iterator<char>());
  try {
    return nlohmann::json::parse(s);
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace

UT_TEST(t03_ws_geometry_valid_guard) {
  UT_CHECK(!window_state::geometry_valid(0, 0));    // 0×0（destroy 事件）
  UT_CHECK(!window_state::geometry_valid(100, 500));
  UT_CHECK(window_state::geometry_valid(271, 126));
  // DPI 取整容差：270.67 < 271 但在容差内有效
  UT_CHECK(window_state::geometry_valid(270.67, 126.0));
  UT_CHECK(!window_state::geometry_valid(270.5 - 1.0, 126.0));  // 超出容差
}

UT_TEST(t03_ws_throttle_and_trailing_write) {
  std::wstring path = temp_path("ws_throttle");
  // 节流窗 60s + flush_pending_for_test 直接驱动尾随冲刷：
  // 真实时间不再参与判定（此前 60ms 窗在 CPU 负载下会被调度间隙击穿 → flaky）
  window_state::WindowStateStore store(path, 60000);

  // 首次变化：elapsed >= interval → 立即落盘
  store.on_geometry_change(300, 500, 10.0, 20.0);
  auto j0 = read_json(path);
  UT_CHECK(j0.has_value());
  if (j0) {
    UT_CHECK_EQ((*j0)["width"].get<double>(), 300.0);
    UT_CHECK_EQ((*j0)["height"].get<double>(), 500.0);
    UT_CHECK_EQ((*j0)["x"].get<double>(), 10.0);
  }

  // 节流窗口内连续两次变化：文件保持 A，最后一版进入尾随补写
  store.on_geometry_change(400, 600, 11.0, 21.0);
  store.on_geometry_change(500, 700, 12.0, 22.0);
  auto j1 = read_json(path);
  UT_CHECK(j1.has_value());
  UT_CHECK_EQ((*j1)["width"].get<double>(), 300.0);  // 未被中间帧覆盖（节流）

  // 尾随补写：最后一版最终落盘（丢中间帧、不丢终值）
  store.flush_pending_for_test();
  auto j2 = read_json(path);
  UT_CHECK(j2.has_value());
  UT_CHECK_EQ((*j2)["width"].get<double>(), 500.0);
  UT_CHECK_EQ((*j2)["height"].get<double>(), 700.0);
  UT_CHECK_EQ((*j2)["x"].get<double>(), 12.0);
  UT_CHECK_EQ((*j2)["y"].get<double>(), 22.0);
}

UT_TEST(t03_ws_version_ordering_rejects_stale) {
  std::wstring path = temp_path("ws_order");
  window_state::WindowStateStore store(path, 1000);
  // 显式版本号写入：新版本生效
  UT_CHECK(store.write_ordered(10, 300, 500, 1.0, 2.0));
  auto j = read_json(path);
  UT_CHECK(j && (*j)["width"].get<double>() == 300.0);
  // 旧版本号 → 拒绝（迟到的旧值不能覆盖新值）
  UT_CHECK(!store.write_ordered(9, 888, 888, 8.0, 8.0));
  j = read_json(path);
  UT_CHECK(j && (*j)["width"].get<double>() == 300.0);
  // 相同版本号 → 拒绝
  UT_CHECK(!store.write_ordered(10, 777, 777, 7.0, 7.0));
  j = read_json(path);
  UT_CHECK(j && (*j)["width"].get<double>() == 300.0);
  // 更新版本 → 接受
  UT_CHECK(store.write_ordered(11, 400, 600, 3.0, 4.0));
  j = read_json(path);
  UT_CHECK(j && (*j)["width"].get<double>() == 400.0);
  UT_CHECK_EQ(store.written_seq(), 11u);
}

UT_TEST(t03_ws_zero_size_and_immediate_save) {
  std::wstring path = temp_path("ws_zero");
  window_state::WindowStateStore store(path, 60000);  // 60s 窗：负载免疫
  store.on_geometry_change(300, 500, 1.0, 2.0);
  UT_CHECK(store.wait_pending_flush(2000));

  // 0×0（destroy 触发）不落盘
  store.on_geometry_change(0, 0, 0.0, 0.0);
  auto j = read_json(path);
  UT_CHECK(j && (*j)["width"].get<double>() == 300.0);

  // save_immediately：跳过节流立即写 + 清空待写槽
  store.on_geometry_change(310, 510, 1.0, 2.0);  // 进入节流窗口（pending）
  store.save_immediately(320, 520, 5.0, 6.0);
  auto j2 = read_json(path);
  UT_CHECK(j2 && (*j2)["width"].get<double>() == 320.0);
  UT_CHECK(j2 && (*j2)["x"].get<double>() == 5.0);
  // 待写槽已清空：尾随补写不会用 310 覆盖 320
  UT_CHECK(store.wait_pending_flush(2000));
  j2 = read_json(path);
  UT_CHECK(j2 && (*j2)["width"].get<double>() == 320.0);
}

UT_TEST(t03_ws_restore_clamp_and_offscreen_center) {
  std::wstring path = temp_path("ws_restore");
  // 正常保存
  window_state::save_sync(path, 300.5, 700.25, 2286.66, 530.66);
  auto st = window_state::restore(path);
  UT_CHECK(st.has_value());
  if (st) {
    UT_CHECK_EQ(st->width, 300.5);
    UT_CHECK_EQ(st->height, 700.25);
    UT_CHECK(st->x && st->y);
  }

  // 越界钳制 [271,640] × [126,860]（值需先过 geometry_valid 最低限）
  window_state::save_sync(path, 1000, 900, 0.0, 0.0);
  auto st2 = window_state::restore(path);
  UT_CHECK(st2.has_value());
  if (st2) {
    UT_CHECK_EQ(st2->width, 640.0);
    UT_CHECK_EQ(st2->height, 860.0);
  }

  // 脱屏位置 → x/y 置空（默认居中）
  window_state::save_sync(path, 300, 700, -50000.0, 5.0);
  auto st3 = window_state::restore(path);
  UT_CHECK(st3.has_value());
  if (st3) {
    UT_CHECK(!st3->x.has_value());
    UT_CHECK(!st3->y.has_value());
  }

  // 0×0 文件 → restore 返回空
  window_state::save_sync(path, 0, 0, 0.0, 0.0);
  UT_CHECK(!window_state::restore(path).has_value());
  // 缺失文件 → nullopt
  UT_CHECK(!window_state::restore(path + L".nonexistent").has_value());
}

UT_TEST(t03_ws_position_on_screen) {
  // 主屏原点附近必然在屏
  UT_CHECK(window_state::is_position_on_screen(100.0, 100.0));
  // 远超所有屏范围 → 脱屏
  UT_CHECK(!window_state::is_position_on_screen(-100000.0, 100.0));
  UT_CHECK(!window_state::is_position_on_screen(100.0, 100000.0));
}

UT_TEST(t03_ws_save_sync_json_shape) {
  // 形态：compact 单行；serde_json BTreeMap 字母序（height,width,x,y）
  std::wstring path = temp_path("ws_shape");
  window_state::save_sync(path, 271.3333333333333, 860.0, 2286.6666666666665, 530.6666666666666);
  std::ifstream f(path, std::ios::binary);
  std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  // 与实机 window_state.json 单行形态一致
  UT_CHECK(s.find('\n') == std::string::npos);
  UT_CHECK(s.find("\"height\":860") != std::string::npos);  // 整值 double 写为 int（jnum 整数化）
  UT_CHECK(s.find("\"width\":271.3333333333333") != std::string::npos);
  // x/y 缺省时不写键
  window_state::save_sync(path, 300, 500, std::nullopt, std::nullopt);
  std::ifstream f2(path, std::ios::binary);
  std::string s2((std::istreambuf_iterator<char>(f2)), std::istreambuf_iterator<char>());
  UT_CHECK(s2.find("\"x\"") == std::string::npos);
  UT_CHECK(s2.find("\"y\"") == std::string::npos);
}
