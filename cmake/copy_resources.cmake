# 构建后把 sidecar/、resources/ 与 Clash Mini 对等运行布局同步到目标输出目录
function(add_clashwin_copy_resources TARGET)
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${TARGET}>/sidecar"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/sidecar" "$<TARGET_FILE_DIR:${TARGET}>/sidecar"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${TARGET}>/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources" "$<TARGET_FILE_DIR:${TARGET}>/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/sidecar/win-mihomo.exe"
            "$<TARGET_FILE_DIR:${TARGET}>/win-mihomo.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/sidecar/clash-verge-service.exe"
            "$<TARGET_FILE_DIR:${TARGET}>/resources/clash-verge-service.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/sidecar/clash-verge-service-install.exe"
            "$<TARGET_FILE_DIR:${TARGET}>/resources/clash-verge-service-install.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/sidecar/clash-verge-service-uninstall.exe"
            "$<TARGET_FILE_DIR:${TARGET}>/resources/clash-verge-service-uninstall.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/sidecar/enableLoopback.exe"
            "$<TARGET_FILE_DIR:${TARGET}>/resources/enableLoopback.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/用户必读.txt"
            "$<TARGET_FILE_DIR:${TARGET}>/用户必读.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/轻量模式防误杀指南.txt"
            "$<TARGET_FILE_DIR:${TARGET}>/轻量模式防误杀指南.txt"
    COMMENT "同步 sidecar/ resources/ 及内核服务布局到输出目录"
  )
endfunction()

# T10 i18n：assets/locales/（13 语言 JSON）干净同步到输出目录（exe 同级 locales/）
function(add_clashwin_copy_locales TARGET)
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${TARGET}>/locales"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/locales" "$<TARGET_FILE_DIR:${TARGET}>/locales"
    COMMENT "同步 locales/ 到输出目录"
  )
endfunction()
