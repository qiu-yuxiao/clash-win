cmake_minimum_required(VERSION 3.20)
project(clashwin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 所有产物统一输出到 build/bin/<config>/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(compiler)        # MSVC 选项（须在第三方目标之前，令其同样生效）
include(deps)            # third_party：nlohmann/yaml-cpp/quickjs
include(copy_resources)  # 构建后拷贝 sidecar/ resources/

# util 静态库（T02：全部基础工具 + T01 的 log/utf）
add_library(clashwin_util STATIC
  src/util/log.cpp
  src/util/utf.cpp
  src/util/base64.cpp
  src/util/ed25519.cpp
  src/util/blake2b.cpp
  src/util/str_utils.cpp
  src/util/uuid.cpp
  src/util/dirs.cpp
  src/util/parse_traffic.cpp
  src/util/speed.cpp
  src/util/json_utils.cpp
  src/util/yaml_utils.cpp
  src/util/node.cpp
)
target_include_directories(clashwin_util PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_util PUBLIC yaml-cpp nlohmann_json shell32 ws2_32)

# config 配置体系（T03：持久化层 + serde 兼容序列化 + Draft + 迁移；
# T06 扩展：from_url 订阅下载 + from_url_direct 直连导入 + SSRF 校验）
add_library(clashwin_config STATIC
  src/config/yaml_serde.cpp
  src/config/verge.cpp
  src/config/clash.cpp
  src/config/profiles.cpp
  src/config/prfitem.cpp
  src/config/ssrf.cpp
  src/config/migrate.cpp
  src/config/window_state.cpp
  src/config/proxy_head_state.cpp
  src/config/unlock_cache.cpp
  src/config/config.cpp
  src/system/unlock_checker.cpp
)
target_include_directories(clashwin_config PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_config PUBLIC clashwin_util clashwin_enhance clashwin_system shcore ws2_32)
target_compile_options(clashwin_config PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# 网络层（T06：WinHTTP 同步 GET/POST + 代理三态 + 订阅头解析 + 轻量 URL 解析）
# 系统域（T18：sysproxy WinINET 注册表语义 + guard_monitor 防篡改/内核守护；T19：tray 系统托盘）
add_library(clashwin_system STATIC
  src/system/net_http.cpp
  src/system/sysproxy.cpp
  src/system/guard_monitor.cpp
  src/system/tray.cpp
  src/system/signals.cpp
  src/system/service_runner.cpp
  src/system/coexist.cpp
  src/system/guardian.cpp
  src/system/network_info.cpp
  src/system/icon_cache.cpp
  src/system/resources.cpp
)
target_include_directories(clashwin_system PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_system PUBLIC clashwin_util winhttp wininet rasapi32 shell32 user32 ws2_32)
target_compile_options(clashwin_system PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# 通用 URI 解析器（T06）+ enhance 配置流水线（T08：enhance/merge/seq/field/
# chain/tun/mini_agreements/dns_defaults + QuickJS js_engine）
add_library(clashwin_enhance STATIC
  src/enhance/uri_list.cpp
  src/enhance/field.cpp
  src/enhance/merge.cpp
  src/enhance/seq.cpp
  src/enhance/tun.cpp
  src/enhance/chain.cpp
  src/enhance/mini_agreements.cpp
  src/enhance/dns_defaults.cpp
  src/enhance/js_engine.cpp
  src/enhance/enhance.cpp
  src/config/runtime_state.cpp
)
target_include_directories(clashwin_enhance PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_enhance PUBLIC clashwin_util qjs nlohmann_json)
target_compile_options(clashwin_enhance PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# mihomo 管道通信层（T04：HTTP/1.1 over named pipe + WS + clash-verge-service IPC；
# T05：数据模型 + MihomoClient + WS 四通道订阅管理）
add_library(clashwin_mihomo STATIC
  src/mihomo/pipe_http.cpp
  src/mihomo/pipe_ws.cpp
  src/mihomo/service_ipc.cpp
  src/mihomo/models.cpp
  src/mihomo/mihomo_client.cpp
  src/mihomo/ws_manager.cpp
)
target_include_directories(clashwin_mihomo PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_mihomo PUBLIC clashwin_util nlohmann_json)
target_compile_options(clashwin_mihomo PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# 内核生命周期（T07：Sidecar 启停/残留清扫/重启恢复/热应用 + 服务模式 SCM/IPC + 校验）
add_library(clashwin_core STATIC
  src/core/core_manager.cpp
  src/core/service_mode.cpp
  src/core/validate.cpp
  src/core/timer.cpp
  src/core/delay_manager.cpp
  src/core/auto_select.cpp
  src/core/quick_routing.cpp
  src/core/autostart.cpp
  src/core/monitor.cpp
  src/core/core_updater.cpp
)
target_include_directories(clashwin_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_core PUBLIC clashwin_config clashwin_mihomo shell32 bcrypt)
target_compile_options(clashwin_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
target_link_libraries(clashwin_system PUBLIC clashwin_core clashwin_mihomo)

# 事件总线 + CommandService 门面（T09：事件名/防抖节流/notice + 命令注册表 44 项）
add_library(clashwin_cmd STATIC
  src/events/event_bus.cpp
  src/events/notice.cpp
  src/cmd/cmd_support.cpp
  src/cmd/command_service.cpp
  src/cmd/cmd_profile.cpp
  src/cmd/cmd_clash.cpp
  src/cmd/cmd_verge.cpp
  src/cmd/cmd_system.cpp
  src/cmd/cmd_lightweight.cpp
  src/cmd/cmd_service.cpp
  src/cmd/cmd_network.cpp
  src/cmd/cmd_app.cpp
  src/update/client_updater.cpp
  src/system/diagnostic_bundle.cpp
)
target_include_directories(clashwin_cmd PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_cmd PUBLIC clashwin_core clashwin_enhance clashwin_system iphlpapi)
target_compile_options(clashwin_cmd PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# i18n（T10：13 语言加载 / fallback=zh / 归一 / 插值 / 热切换）
add_library(clashwin_i18n STATIC
  src/i18n/i18n.cpp
)
target_include_directories(clashwin_i18n PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_i18n PUBLIC clashwin_util nlohmann_json)
target_compile_options(clashwin_i18n PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# UI 渲染核心（T10：D2D 双后端 / Widget 树 / 容器 / 控件族 / UiPostbox）
add_library(clashwin_ui STATIC
  src/ui/d2d_context.cpp
  src/ui/widget.cpp
  src/ui/container.cpp
  src/ui/label.cpp
  src/ui/button.cpp
  src/ui/icon_w.cpp
  src/ui/segmented.cpp
  src/ui/switch_w.cpp
  src/ui/textfield.cpp
  src/ui/slider.cpp
  src/ui/dropdown.cpp
  src/ui/checkbox_w.cpp
  src/ui/scroll_list.cpp
  src/ui/context_menu.cpp
  src/ui/modal.cpp
  src/ui/overlay_fade.cpp
  src/ui/snackbar_w.cpp
  src/ui/notice_handler.cpp
  src/ui/tooltip_w.cpp
  src/ui/canvas_graph.cpp
  src/ui/text_viewer.cpp
  src/ui/ui_postbox.cpp
  src/ui/theme/palettes.cpp
  src/ui/theme/theme.cpp
  src/ui/scenes/connection_data.cpp
  src/ui/scenes/connections_view.cpp
  src/ui/scenes/connections_panel.cpp
  src/ui/scenes/connection_detail_dialog.cpp
  src/ui/scenes/add_url_dialog.cpp
  src/ui/scenes/unlock_dialog.cpp
  src/ui/scenes/first_run_guide_dialog.cpp
  src/ui/scenes/utility_modals.cpp
  src/ui/scenes/node_data.cpp
  src/ui/scenes/quick_bar.cpp
  src/ui/scenes/node_list.cpp
  src/ui/scenes/profile_import_card.cpp
  src/ui/scenes/traffic_pane.cpp
  src/ui/scenes/settings_drawer.cpp
  src/ui/breakpoints.cpp
  src/ui/title_bar.cpp
  src/ui/main_scene.cpp
  src/ui/main_window.cpp
  src/ui/help_menu.cpp
)
target_include_directories(clashwin_ui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_ui PUBLIC clashwin_util clashwin_cmd clashwin_i18n clashwin_system
  d2d1 dwrite windowscodecs user32)
target_compile_options(clashwin_ui PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# app 应用聚合与生命周期/单实例/深链（T19）
add_library(clashwin_app STATIC
  src/app/lifecycle.cpp
  src/app/singleton.cpp
  src/app/scheme.cpp
)
target_include_directories(clashwin_app PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_app PUBLIC
  clashwin_util clashwin_config clashwin_cmd clashwin_system clashwin_core ws2_32
)
target_compile_options(clashwin_app PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)

add_executable(clashwin WIN32
  src/main.cpp
  src/app/app.cpp
  src/clashwin.rc
)

target_link_libraries(clashwin PRIVATE
  clashwin_app                # T19 生命周期 / 单实例 / 深链
  clashwin_util               # util + vendored 三件套（PUBLIC 传递）
  clashwin_config             # T03 配置体系
  clashwin_mihomo             # T04 管道通信层
  clashwin_core               # T07 内核生命周期
  clashwin_cmd                # T09 事件总线 + 命令门面
  clashwin_i18n               # T10 i18n
  clashwin_ui                 # T10 UI 渲染核心
  d2d1 dwrite windowscodecs user32 gdi32 ws2_32 shell32 bcrypt  # D2D/DWrite/WIC / 窗口 / Winsock / Shell / CNG
)

# 本项目源码用 /W4（第三方保持全局 /W3）
target_compile_options(clashwin PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
target_compile_options(clashwin_util PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
# 清单以 .rc 内嵌为唯一来源（Per-Monitor-V2），关闭链接器默认嵌入避免资源冲突
target_link_options(clashwin PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/MANIFEST:NO>)

add_clashwin_copy_resources(clashwin)
add_clashwin_copy_locales(clashwin)

# ---------- 单测（T02 util + T03 config） ----------
add_executable(clashwin_test
  tests/test_main.cpp
  tests/test_base64.cpp
  tests/test_blake2b.cpp
  tests/test_minisign_verify.cpp
  tests/test_str_utils.cpp
  tests/test_uuid.cpp
  tests/test_dirs.cpp
  tests/test_parse_traffic.cpp
  tests/test_speed.cpp
  tests/test_json_utils.cpp
  tests/test_yaml_utils.cpp
  tests/test_debounce.cpp
  tests/test_config_template.cpp
  tests/test_config_clash.cpp
  tests/test_config_migrate.cpp
  tests/test_config_profiles.cpp
  tests/test_config_draft.cpp
  tests/test_config_window_state.cpp
  tests/test_config_states.cpp
  tests/test_pipe_http.cpp
  tests/test_pipe_ws.cpp
  tests/test_service_ipc.cpp
  tests/test_mihomo_models.cpp
  tests/test_mihomo_client_mock.cpp
  tests/test_mihomo_live.cpp
  tests/test_uri_list.cpp
  tests/test_ssrf.cpp
  tests/test_net_http.cpp
  tests/test_enhance_field.cpp
  tests/test_enhance_seq_merge.cpp
  tests/test_enhance_tun.cpp
  tests/test_enhance_mini_agreements.cpp
  tests/test_enhance_js_engine.cpp
  tests/test_enhance_golden.cpp
  tests/test_core_commandline.cpp
  tests/test_core_kill.cpp
  tests/test_core_validate.cpp
  tests/test_core_lifecycle.cpp
  tests/test_core_service.cpp
  tests/test_core_live.cpp
  tests/test_events.cpp
  tests/test_cmd_registry.cpp
  tests/test_cmd_smoke.cpp
  tests/test_ui_layout.cpp
  tests/test_ui_widgets.cpp
  tests/test_fd11_mui_icons.cpp
  tests/test_ui_hit.cpp
  tests/test_ui_dirty.cpp
  tests/test_ui_scroll.cpp
  tests/test_ui_offscreen.cpp
  tests/test_ui_postbox.cpp
  tests/test_i18n.cpp
  tests/test_ui_window_state.cpp
  tests/test_ui_window_persist.cpp
  tests/test_theme_core.cpp
  tests/test_theme_widgets.cpp
  tests/test_theme_switch.cpp
  tests/test_theme_factors.cpp
  tests/test_theme_switch_diag.cpp
  tests/test_scene_node.cpp
  tests/test_scene_traffic.cpp
  tests/test_t15_profile.cpp
  tests/test_t16_delay.cpp
  tests/test_t17_drawer.cpp
  tests/test_root_layout.cpp
  tests/test_t18_sysproxy.cpp
  tests/test_t19_tray.cpp
  tests/test_t20_connections.cpp
  tests/test_t21_modals.cpp
  tests/test_t22_monitor_autostart.cpp
  tests/test_t23_quit_recovery.cpp
  tests/test_t24_updater_diagnostics.cpp
  tests/test_t25_p1_finalization.cpp
  tests/test_t26_delta_parity.cpp
  tests/test_t27_monitor_heal.cpp
  tests/test_t28_bolt_subset.cpp
  tests/test_t29_skin_font.cpp
  tests/test_t30_slider_thumb_inset.cpp
  tests/test_t31_interactive_states.cpp
  tests/test_t32_hover_pixels.cpp
  tests/test_t33_skin_lookup.cpp
  tests/test_t34_hit_chain.cpp
  tests/test_t35_release_readiness.cpp
  tests/test_t36_m11_challenge.cpp
  tests/test_t37_pixel_and_popup_parity.cpp
  tests/test_t38_notice_parity.cpp
  tests/test_t39_guardian.cpp
  tests/test_coexist.cpp
)
target_link_libraries(clashwin_test PRIVATE clashwin_app clashwin_util clashwin_config clashwin_mihomo clashwin_system clashwin_enhance clashwin_core clashwin_cmd clashwin_ui clashwin_i18n shell32 bcrypt)
target_compile_options(clashwin_test PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
add_clashwin_copy_resources(clashwin_test)
add_clashwin_copy_locales(clashwin_test)

# ---------- NSIS 安装包（可选目标，不影响默认构建） ----------
if(WIN32)
  find_program(CLASHWIN_MAKENSIS makensis
    HINTS
      "$ENV{LOCALAPPDATA}/tauri/NSIS"
      "$ENV{ProgramFiles}/NSIS"
      "$ENV{ProgramFiles\(x86\)}/NSIS"
  )
  if(CLASHWIN_MAKENSIS)
    if(CMAKE_CONFIGURATION_TYPES)
      set(CLASHWIN_INSTALLER_CONFIG "$<CONFIG>")
    else()
      set(CLASHWIN_INSTALLER_CONFIG "Release")
    endif()
    add_custom_target(installer
      COMMAND "${CMAKE_COMMAND}" -E echo "Running NSIS installer build..."
      COMMAND powershell -NoProfile -ExecutionPolicy Bypass
              -File "${CMAKE_CURRENT_SOURCE_DIR}/installer/build_installer.ps1"
              -Configuration "${CLASHWIN_INSTALLER_CONFIG}"
              -Makensis "${CLASHWIN_MAKENSIS}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      DEPENDS clashwin
      USES_TERMINAL
      COMMENT "Build ClashWin NSIS installer into dist/"
    )
    message(STATUS "已启用 installer 目标（makensis: ${CLASHWIN_MAKENSIS}）")
  else()
    message(STATUS "未找到 makensis，installer 目标未启用；可直接运行 installer/build_installer.ps1")
  endif()
endif()
