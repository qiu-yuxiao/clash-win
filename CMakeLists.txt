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

# Source files
set(SOURCES
    src/main.cpp
    src/window/main_window.hpp
    src/window/main_window.cpp
    src/render/d2d_renderer.hpp
    src/render/d2d_renderer.cpp
)

# Create GUI executable (subsystem:windows)
add_executable(ClashWin WIN32 ${SOURCES})

# Target include directories
target_include_directories(ClashWin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
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
    ws2_32
    shcore
)

# Output directory config
set_target_properties(ClashWin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
