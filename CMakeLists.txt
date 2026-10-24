cmake_minimum_required(VERSION 3.20)

project(ClashWin
    VERSION 0.1.0
    DESCRIPTION "Pure Windows Native Visual C++ Clash Client"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure Unicode and Windows native defs
add_compile_definitions(
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# MSVC specific compiler options
if(MSVC)
    # Enable UTF-8 source and execution charset
    add_compile_options(/utf-8 /permissive- /W4)

    # Static C/C++ Runtime (/MT for Release, /MTd for Debug)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Core & Config sources
set(COMMON_SOURCES
    src/core/core_supervisor.cpp
    src/core/mihomo_client.cpp
    src/core/health_probe.cpp
    src/core/connection_manager.cpp
    src/system/singleton.cpp
    src/system/sysproxy.cpp
    src/system/service_manager.cpp
    src/config/app_config.cpp
    src/config/node_filter.cpp
    src/config/profile_manager.cpp
    src/config/tun_compiler.cpp
    src/config/universal_parser.cpp
    src/config/enhance_chain.cpp
    src/network/unlock_checker.cpp
)

# UI sources
set(UI_SOURCES
    src/main.cpp
    src/window/main_window.hpp
    src/window/main_window.cpp
    src/render/d2d_renderer.hpp
    src/render/d2d_renderer.cpp
    src/render/theme.cpp
)

# Create GUI executable (subsystem:windows)
add_executable(ClashWin WIN32 ${UI_SOURCES} ${COMMON_SOURCES})

# Target include directories
target_include_directories(ClashWin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link Windows Native Direct2D, DirectWrite, WinHTTP, ComCtl, and DWM
target_link_libraries(ClashWin PRIVATE
    d2d1
    dwrite
    dcomp
    dxgi
    dwmapi
    uxtheme
    comctl32
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
)

# Test executable for Phase 4 Slice 1
add_executable(test_slice1_config
    tests/test_slice1_config.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_slice1_config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_slice1_config PRIVATE
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
)

# Test executable for Phase 4 Slice 2 Health Probe
add_executable(test_slice2_health_probe
    tests/test_slice2_health_probe.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_slice2_health_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_slice2_health_probe PRIVATE
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
)

# Test executable for Phase 4 Slice 3 System Integration (TUN, Sysproxy, SCM)
add_executable(test_slice3_system_integration
    tests/test_slice3_system_integration.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_slice3_system_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_slice3_system_integration PRIVATE
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
)

# Test executable for Phase 4 Slice 4 UI Themes & Direct2D
add_executable(test_slice4_ui_themes
    tests/test_slice4_ui_themes.cpp
    src/render/theme.cpp
    src/render/d2d_renderer.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_slice4_ui_themes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_slice4_ui_themes PRIVATE
    d2d1
    dwrite
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
)

# Test executable for Phase 4 Slice 5 Win32 Desktop & Lifecycle
add_executable(test_slice5_tray_lifecycle
    tests/test_slice5_tray_lifecycle.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_slice5_tray_lifecycle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_slice5_tray_lifecycle PRIVATE
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
)

# Test executable for Phase 4 Slice 6 Settings Drawer & 5 Control Cards
add_executable(test_slice6_settings_drawer
    tests/test_slice6_settings_drawer.cpp
    src/render/theme.cpp
    src/render/d2d_renderer.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_slice6_settings_drawer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_slice6_settings_drawer PRIVATE
    d2d1
    dwrite
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
)

# Test executable for Phase 4 Slice 7 Universal Parser & Enhance Chain
add_executable(test_slice7_universal_parser
    tests/test_slice7_universal_parser.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_slice7_universal_parser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_slice7_universal_parser PRIVATE
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
)

# Test executable for Phase 4 Slice 8 Connections Panel & Right-Click Routing
add_executable(test_slice8_connections_panel
    tests/test_slice8_connections_panel.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_slice8_connections_panel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_slice8_connections_panel PRIVATE
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
)

# Test executable for Phase 4 Slice 9 Media Unlock Checker
add_executable(test_slice9_unlock_checker
    tests/test_slice9_unlock_checker.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_slice9_unlock_checker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_slice9_unlock_checker PRIVATE
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
)

# Test executable for Phase 4 Slice 10 Occlusion Model & Micro Traffic Meter
add_executable(test_slice10_occlusion_model
    tests/test_slice10_occlusion_model.cpp
    src/render/theme.cpp
    src/render/d2d_renderer.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_slice10_occlusion_model PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_slice10_occlusion_model PRIVATE
    d2d1
    dwrite
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
)

# Test executable for Phase 4 Slice 11 NSIS Packaging & SCM Service Deployment
add_executable(test_slice11_installer
    tests/test_slice11_installer.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_slice11_installer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_slice11_installer PRIVATE
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
)

# Test executable for Phase 5 Final Acceptance & Benchmarking
add_executable(test_phase5_acceptance
    tests/test_phase5_acceptance.cpp
    src/render/theme.cpp
    src/render/d2d_renderer.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_phase5_acceptance PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_phase5_acceptance PRIVATE
    d2d1
    dwrite
    winhttp
    wininet
    ws2_32
    shcore
    crypt32
    advapi32
    shell32
    psapi
)

# Output directory config
set_target_properties(ClashWin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(test_slice1_config PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(test_slice2_health_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(test_slice3_system_integration PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(test_slice4_ui_themes PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(test_slice5_tray_lifecycle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(test_slice6_settings_drawer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(test_slice7_universal_parser PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(test_slice8_connections_panel PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(test_slice9_unlock_checker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(test_slice10_occlusion_model PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(test_slice11_installer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(test_phase5_acceptance PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
