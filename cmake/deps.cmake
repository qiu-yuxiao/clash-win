# third_party vendored 依赖注册（三项均为源码落库，无 vcpkg）

# 1) nlohmann/json：单头接口库（third_party/nlohmann/json.hpp → #include <nlohmann/json.hpp>）
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# 2) yaml-cpp：静态库（关闭测试/工具/安装）
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/yaml-cpp)

# 3) QuickJS（quickjs-ng）：静态库，不编 CLI/示例/安装；EXCLUDE_FROM_ALL 避免连带编测试目标
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/quickjs EXCLUDE_FROM_ALL)
