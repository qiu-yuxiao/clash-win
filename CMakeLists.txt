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
)
target_include_directories(clashwin_util PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clashwin_util PUBLIC yaml-cpp nlohmann_json shell32)

add_executable(clashwin WIN32
  src/main.cpp
  src/app/app.cpp
  src/clashwin.rc
)

target_link_libraries(clashwin PRIVATE
  clashwin_util               # util + vendored 三件套（PUBLIC 传递）
  d2d1 user32 gdi32 ws2_32    # D2D / 窗口 / Winsock
)

# 本项目源码用 /W4（第三方保持全局 /W3）
target_compile_options(clashwin PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
target_compile_options(clashwin_util PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
# 清单以 .rc 内嵌为唯一来源（Per-Monitor-V2），关闭链接器默认嵌入避免资源冲突
target_link_options(clashwin PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/MANIFEST:NO>)

add_clashwin_copy_resources(clashwin)

# ---------- 单测（T02 util） ----------
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
)
target_link_libraries(clashwin_test PRIVATE clashwin_util)
target_compile_options(clashwin_test PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
