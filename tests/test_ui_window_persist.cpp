// T11 测试：几何持久化接线（window_state.json 节流落盘 + 恢复钳制）
// 与 last-non-large-size 落盘（WindowProvider 持久化键等价物）。
#include <windows.h>

#include <filesystem>
#include <fstream>

#include "clashwin/config/constants.h"
#include "test_util.h"
#include "ui/main_window.h"

using namespace clashwin::ui::win;
namespace wstate = clashwin::config::window_state;

namespace {
std::wstring temp_dir() {
  wchar_t p[MAX_PATH];
  GetTempPathW(MAX_PATH, p);
  return std::wstring(p) + L"clashwin_t11_test";
}
}  // namespace

UT_TEST(t11_geometry_throttled_save_and_restore) {
  std::wstring dir = temp_dir();
  std::filesystem::create_directories(dir);
  std::wstring path = dir + L"\\window_state.json";
  std::error_code ec;
  std::filesystem::remove(path, ec);

  // 短节流（100ms）便于测试
  {
    wstate::WindowStateStore store(path, 100);
    // 拖动序列：间隔内只落盘一次，最后一版必落（尾随补写）
    store.on_geometry_change(271, 680, 10, 20);
    store.on_geometry_change(300, 600, 10, 20);
    store.on_geometry_change(400, 500, 30, 40);
    UT_CHECK(store.wait_pending_flush(2000));
  }
  auto st = wstate::restore(path);
  UT_CHECK(st.has_value());
  UT_CHECK(st->width == 400);  // 终值而非中间帧
  UT_CHECK(st->height == 500);
  UT_CHECK(st->x && *st->x == 30);
  UT_CHECK(st->y && *st->y == 40);

  // 无效几何（0×0 守卫）不落盘：新 store 写 0×0 → 文件不变
  std::filesystem::remove(path, ec);
  {
    wstate::WindowStateStore store(path, 10);
    store.save_immediately(0, 0, std::nullopt, std::nullopt);
    store.flush_pending_for_test();
  }
  UT_CHECK(!std::filesystem::exists(path));

  // 恢复钳制：保存超 MAX → restore 钳到 [MIN,MAX]（← restore_window_state）
  wstate::save_sync(path, 1000.0, 2000.0, std::nullopt, std::nullopt);
  st = wstate::restore(path);
  UT_CHECK(st.has_value());
  UT_CHECK(st->width == 640);
  UT_CHECK(st->height == 860);

  std::filesystem::remove_all(dir, ec);
}

UT_TEST(t11_restore_offscreen_position_reset) {
  std::wstring dir = temp_dir();
  std::filesystem::create_directories(dir);
  std::wstring path = dir + L"\\window_state.json";
  // 脱屏位置（远超任一显示器右界）→ x/y 置空（默认居中）
  wstate::save_sync(path, 271.0, 680.0, 999999.0, 999999.0);
  auto st = wstate::restore(path);
  UT_CHECK(st.has_value());
  UT_CHECK(!st->x.has_value() || !wstate::is_position_on_screen(*st->x, *st->y));
  { std::error_code ec2; std::filesystem::remove_all(dir, ec2); }
}

UT_TEST(t11_last_non_large_size_roundtrip) {
  std::wstring dir = temp_dir();
  std::filesystem::create_directories(dir);
  std::wstring path = dir + L"\\last_non_large_size.json";
  LargeModeController::save(path, 300.0, 550.0);
  auto v = LargeModeController::load(path);
  UT_CHECK(v.has_value());
  UT_CHECK(v->first == 300.0);
  UT_CHECK(v->second == 550.0);
  // 无文件 → nullopt（回退默认 271×680，← toggleMaximize 启动分支）
  auto none = LargeModeController::load(dir + L"\\nope.json");
  UT_CHECK(!none.has_value());
  { std::error_code ec2; std::filesystem::remove_all(dir, ec2); }
}
