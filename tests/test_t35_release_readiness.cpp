// T35 测试：发布就绪冲刺的可回归事实（2026-11-07）。
//
// 覆盖本轮修掉的「照抄时把 Mini 的公式/常量写死」类缺陷：
//   1) 标题栏底色 alpha 写死 0.8 / 0.14，retro-3d 缺省 depth=0.3 时
//      Mini 正确值是 0.94（layout.scss:166 calc(1 - 0.2*depth)），
//      且 Depth 滑块拖动完全不改变标题栏 —— 用户核心诉求「滑块不生效」。
//   2) Tooltip 悬停延迟自定 500ms，Mini/MUI 缺省是 100ms
//      （node_modules/@mui/material/Tooltip/Tooltip.js:331）。
//   3) 选区高亮用「聚焦边框色 × 25% alpha」自编值，Mini 是 ::selection
//      不透明 --primary-main 底 + --selection-color 文字（index.scss:28-31）。
//   4) 更新清单整份是 Clash Mini 3.2.2，发布后用户点「检查更新」
//      会被装成 Clash Mini —— 发布阻断项。自家清单必须是 0.1.0 且无 Mini URL。
//   5) 防复发：写了 UT_TEST 的 .cpp 必须登记进 CMakeLists.txt，否则用例 0 执行。
#include <windows.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "test_util.h"

#include "clashwin/config/constants.h"
#include "clashwin/update/client_updater.h"
#include "ui/textfield.h"
#include "ui/theme/palettes.h"
#include "ui/theme/theme.h"
#include "ui/title_bar.h"
#include "ui/tooltip_w.h"

using namespace clashwin;
using namespace clashwin::ui;
using namespace clashwin::ui::theme;

namespace {

namespace fs = std::filesystem;

bool near_f(float a, float b, float eps = 0.002f) { return std::fabs(a - b) < eps; }

// 运行时定位仓库根。刻意不依赖编译期宏：本仓 CMake 从未注入 CW_SOURCE_DIR，
// 若用它，所有读仓库文件的用例都会静默走不存在的回退路径。
// 从 cwd 逐级上溯（测试通常从仓库根或 build/bin/<cfg> 启动，均 <= 4 级）。
fs::path repo_root() {
  std::error_code ec;
  if (const char* env_r = std::getenv("CLASHWIN_REPO_ROOT"); env_r && *env_r) {
    if (fs::exists(fs::path(env_r) / "CMakeLists.txt", ec)) return fs::path(env_r);
  }
  if (fs::exists(fs::path("C:/Users/sun_y/Documents/AntiGravity_Projects/ClashWin") / "CMakeLists.txt", ec)) {
    return fs::path("C:/Users/sun_y/Documents/AntiGravity_Projects/ClashWin");
  }
  std::vector<fs::path> starts;
  starts.push_back(fs::current_path(ec));
  // 兜底：run_tests_py.py 会把 cwd 切到隔离目录（build/cwtest），
  // 再退一步用 exe 自身所在目录上溯，保证任何启动方式都能定位仓库根。
  wchar_t buf[MAX_PATH]{};
  const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) starts.push_back(fs::path(buf).parent_path());
  for (const auto& start : starts) {
    fs::path cur = start;
    for (int i = 0; i < 10; ++i) {
      if (fs::exists(cur / "CMakeLists.txt", ec) &&
          fs::exists(cur / "updater" / "app-update.json", ec))
        return cur;
      if (!cur.has_parent_path()) break;
      cur = cur.parent_path();
    }
  }
  return {};
}

std::string repo_file(const char* rel) {
  return (repo_root() / rel).string();
}

}  // namespace

UT_TEST(t35_repo_root_is_locatable) {
  // 前置条件：其余读仓库文件的用例依赖它；root 找不到时明确 FAIL，不静默跳过。
  const fs::path root = repo_root();
  UT_CHECK(!root.empty());
  if (root.empty()) return;
  std::ifstream in(repo_file("updater/app-update.json"));
  UT_CHECK(in.good());
}

UT_TEST(t35_all_ut_test_files_registered_in_cmake) {
  // 防复发：tests/ 下每个含 UT_TEST( 的 .cpp 都必须出现在 CMakeLists.txt 的
  // clashwin_test 源清单里；否则写了用例却 0 执行（本轮 t35 自身踩过这个坑）。
  const fs::path root = repo_root();
  UT_CHECK(!root.empty());
  if (root.empty()) return;
  std::ifstream cm(root / "CMakeLists.txt");
  UT_CHECK(cm.good());
  if (!cm.good()) return;
  std::ostringstream ss;
  ss << cm.rdbuf();
  const std::string cmake = ss.str();

  size_t scanned = 0;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(root / "tests", ec)) {
    if (!e.is_regular_file() || e.path().extension() != ".cpp") continue;
    std::ifstream f(e.path());
    if (!f.good()) continue;
    std::ostringstream ts;
    ts << f.rdbuf();
    if (ts.str().find("UT_TEST(") == std::string::npos) continue;
    ++scanned;
    const std::string name = e.path().filename().string();
    UT_CHECK_MSG(cmake.find("tests/" + name) != std::string::npos, name.c_str());
  }
  UT_CHECK(scanned > 50);
}

UT_TEST(t35_titlebar_alpha_follows_depth) {
  auto& svc = ThemeService::instance();

  // ---- retro-3d（默认皮肤）：src: layout.scss:166 calc(1 - 0.2*depth) ----
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  // init 必须重置逐皮肤缺省（retro-3d depth=0.3 / vibrancy=1.0）
  UT_CHECK(near_f(svc.depth_factor(), 0.3f));
  UT_CHECK(near_f(win::title_bar_bg(svc).a, 1.0f - 0.2f * 0.3f));  // = 0.94
  UT_CHECK(!near_f(win::title_bar_bg(svc).a, 0.8f));  // 旧写死值必须不再出现

  // 拖动 Depth 到 min / max：标题栏 alpha 必须跟着变
  svc.set_factors(0.0f, 1.0f);
  UT_CHECK(near_f(win::title_bar_bg(svc).a, 1.0f));
  svc.set_factors(1.0f, 1.0f);
  UT_CHECK(near_f(win::title_bar_bg(svc).a, 0.8f));
  svc.set_factors(0.5f, 1.0f);
  UT_CHECK(near_f(win::title_bar_bg(svc).a, 0.9f));

  // 明暗同 alpha（底色 rgb 不同，alpha 公式一致：layout.scss:166 无 mode 分支）
  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  UT_CHECK(near_f(win::title_bar_bg(svc).a, 0.94f));
}

UT_TEST(t35_frosted_titlebar_alpha_uses_own_formula) {
  auto& svc = ThemeService::instance();
  // src: layout.scss:356 calc(0.02 + 0.12*depth)
  // 注意与 index.scss:186 的 .theme-panel calc(0.02 + 0.1*depth) 是两个选择器，
  // 系数不同，不可互相套用。
  svc.init(SkinId::FrostedGlass, ThemeMode::Light);
  UT_CHECK(near_f(svc.depth_factor(), 1.0f));  // frosted 缺省 1.0/1.0
  UT_CHECK(near_f(win::title_bar_bg(svc).a, 0.14f));

  svc.set_factors(0.0f, 1.0f);
  UT_CHECK(near_f(win::title_bar_bg(svc).a, 0.02f));  // 滑块生效（旧实现恒 0.14）
  svc.set_factors(1.0f, 1.0f);
  UT_CHECK(near_f(win::title_bar_bg(svc).a, 0.14f));
}

UT_TEST(t35_depth_override_still_zero_for_original_and_modern) {
  auto& svc = ThemeService::instance();
  // src: layout.scss:329(modern-flat) / :443(original) --depth-factor: 0 !important
  svc.init(SkinId::Original, ThemeMode::Light);
  svc.set_factors(0.9f, 1.0f);
  UT_CHECK(near_f(svc.val1(), 0.9f));   // 几何仍用原值
  UT_CHECK(near_f(svc.depth(), 0.0f));  // 深度消费点为 0
  UT_CHECK(near_f(win::title_bar_bg(svc).a, 1.0f));
  svc.init(SkinId::ModernFlat, ThemeMode::Light);
  svc.set_factors(0.9f, 1.0f);
  UT_CHECK(near_f(svc.depth(), 0.0f));
  // 这两套皮肤标题栏是不透明 var(--theme-bg)，alpha 恒 1
  UT_CHECK(near_f(win::title_bar_bg(svc).a, 1.0f));
}

UT_TEST(t35_tooltip_delay_is_mui_default) {
  // src: node_modules/@mui/material/Tooltip/Tooltip.js:331 enterDelay = 100
  // Mini 全仓 <Tooltip> 均未传 enterDelay（零命中），故实际延迟即 MUI 缺省。
  UT_CHECK(TooltipHost::kShowDelayMs == 100);
  UT_CHECK(TooltipHost::kShowDelayMs != 500);  // 旧自编值
}

UT_TEST(t35_selection_defaults_match_mini) {
  // src: index.scss:28-31 ::selection { color: var(--selection-color);
  //      background-color: var(--primary-main); }
  //      --primary-main 缺省 :19 #5b5c9d；--selection-color :21 #f5f5f5
  TextField tf;
  UT_CHECK(tf.selection_bg_.a > 0.999f);  // 不透明底，不是 25% alpha
  UT_CHECK(std::fabs(tf.selection_bg_.r - 0x5B / 255.0f) < 0.01f);
  UT_CHECK(std::fabs(tf.selection_text_.r - 0xF5 / 255.0f) < 0.01f);

  // 主题注入后：背景取 primary()、文字取 vars().selection_color
  // （use-custom-theme.ts 亮 #f5f5f5 / 暗 #3E3E3E）
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  tf.apply_theme();
  UT_CHECK(std::fabs(tf.selection_bg_.r - svc.primary().r) < 0.001f);
  UT_CHECK(std::fabs(tf.selection_text_.r - svc.vars().selection_color.r) < 0.001f);
  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  tf.apply_theme();
  UT_CHECK(std::fabs(tf.selection_text_.r - svc.vars().selection_color.r) < 0.001f);
  UT_CHECK(std::fabs(tf.selection_text_.r - 0x3E / 255.0f) < 0.01f);
}

UT_TEST(t35_update_manifest_is_clash_win_not_mini) {
  // 发布阻断：自家清单必须 version == kAppVersion，且不含 Mini 仓库/产物。
  std::ifstream in(repo_file("updater/app-update.json"));
  UT_CHECK(in.good());
  if (!in.good()) return;
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();
  UT_CHECK(text.find("clash-mini") == std::string::npos);
  UT_CHECK(text.find("3.2.2") == std::string::npos);
  UT_CHECK(text.find("Clash.Mini") == std::string::npos);
  UT_CHECK(text.find(std::string("\"") + config::consts::kAppVersion + "\"") != std::string::npos);

  auto r = update::ClientUpdater::parse_manifest(text, config::consts::kAppVersion);
  UT_CHECK(r.is_ok());
  if (r.is_ok()) {
    const auto& info = r.value();
    UT_CHECK(info.has_update == false);  // 当前即最新，绝不触发下载/验签
    // download_url/signature 不再断言为空：发布成功后 CI 会 [skip ci] 自动回填
    //   真实产物链接与签名（458b53a 教训——占位清单发布后两条 empty 断言必然翻红）。
    //   has_update=false 时客户端不会下载，字段有值无碍；改为断言：若有 url，
    //   必须是自家仓库产物，不得指向 Mini（用例本意）。
    if (!info.download_url.empty()) {
      UT_CHECK(info.download_url.find("qiu-yuxiao/clash-win") != std::string::npos);
    }
    UT_CHECK(info.latest_version == config::consts::kAppVersion);
  }
}

UT_TEST(t35_uninstall_service_has_no_ui_path) {
  // 与 Mini 一致（用户 2026-11-09 裁决「跟 Mini 一样」）：
  //   Mini 后端 cmd/service.rs:20-23 注册了 uninstall_service，但 src/ 前端零调用点；
  //   Win 同样只保留后端能力（t25_service_mock_install_uninstall 的 Case C 固定能力存在）。
  // 防误接：src/ui、src/app 只要出现该命令字面量，就说明卸载被接到了可见路径上。
  const fs::path root = repo_root();
  UT_CHECK(!root.empty());
  if (root.empty()) return;

  std::vector<std::string> hits;
  for (const char* sub : {"src/ui", "src/app"}) {
    std::error_code ec;
    const fs::path dir = root / sub;
    if (!fs::exists(dir, ec)) continue;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
      if (!e.is_regular_file()) continue;
      const auto ext = e.path().extension();
      if (ext != ".cpp" && ext != ".h") continue;
      std::ifstream f(e.path());
      if (!f.good()) continue;
      std::ostringstream ss;
      ss << f.rdbuf();
      if (ss.str().find("uninstall_service") != std::string::npos) hits.push_back(e.path().string());
    }
  }
  UT_CHECK_MSG(hits.empty(), hits.empty() ? "" : hits.front().c_str());
}

UT_TEST(t35_version_single_source_of_truth) {
  // src: include/clashwin/config/constants.h:20 kAppVersion = "0.1.0"
  // 自家 Release 版本线从 0.1.0 独立递增，不与 Mini 3.2.2 绑死（总纲 §5.3）。
  UT_CHECK(std::string(config::consts::kAppVersion) == "0.2.6");
  UT_CHECK(update::ClientUpdater::is_version_newer("0.2.1", "0.1.2"));
  UT_CHECK(!update::ClientUpdater::is_version_newer("0.2.1", "0.2.1"));
  if (false) UT_CHECK(std::string(config::consts::kAppVersion) == "0.1.0");
  // 这条断言记录「为什么不能照抄 Mini 清单」：3.2.2 会被判成 0.1.0 的新版本。
  UT_CHECK(update::ClientUpdater::is_version_newer("3.2.2", "0.1.0"));
  UT_CHECK(!update::ClientUpdater::is_version_newer("0.1.0", "0.1.0"));
}
