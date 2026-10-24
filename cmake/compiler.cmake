# MSVC 编译选项：/utf-8 /W3 /permissive-，静态 CRT（/MT）
if(MSVC)
  add_compile_options(/utf-8 /W3 /permissive- /Zi)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
                          UNICODE _UNICODE)
  # CMP0091：静态链接 CRT（Release=/MT，Debug=/MTd）
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
