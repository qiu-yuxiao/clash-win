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
)
target_include_directories(clashwin_config PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_config PUBLIC clashwin_util clashwin_enhance clashwin_system shcore ws2_32)
target_compile_options(clashwin_config PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# 网络层（T06：WinHTTP 同步 GET/POST + 代理三态 + 订阅头解析 + 轻量 URL 解析）
add_library(clashwin_system STATIC
  src/system/net_http.cpp
)
target_include_directories(clashwin_system PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_system PUBLIC clashwin_util winhttp)
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
)
target_include_directories(clashwin_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_core PUBLIC clashwin_config clashwin_mihomo shell32)
target_compile_options(clashwin_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)

add_executable(clashwin WIN32
  src/main.cpp
  src/app/app.cpp
  src/clashwin.rc
)

target_link_libraries(clashwin PRIVATE
  clashwin_util               # util + vendored 三件套（PUBLIC 传递）
  clashwin_config             # T03 配置体系
  clashwin_mihomo             # T04 管道通信层
  clashwin_core               # T07 内核生命周期
  d2d1 user32 gdi32 ws2_32    # D2D / 窗口 / Winsock
)

# 本项目源码用 /W4（第三方保持全局 /W3）
target_compile_options(clashwin PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
target_compile_options(clashwin_util PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
# 清单以 .rc 内嵌为唯一来源（Per-Monitor-V2），关闭链接器默认嵌入避免资源冲突
target_link_options(clashwin PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/MANIFEST:NO>)

add_clashwin_copy_resources(clashwin)

# ---------- 单测（T02 util + T03 config） ----------
add_executable(clashwin_test
  tests/test_main.cpp
  tests/test_base64.cpp
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
)
target_link_libraries(clashwin_test PRIVATE clashwin_util clashwin_config clashwin_mihomo clashwin_system clashwin_enhance clashwin_core)
target_compile_options(clashwin_test PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
