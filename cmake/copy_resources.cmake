# 构建后把 sidecar/（5 个 exe）与 resources/ 干净同步到目标输出目录（先删旧目录防改名残留堆积）
function(add_clashwin_copy_resources TARGET)
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${TARGET}>/sidecar"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/sidecar" "$<TARGET_FILE_DIR:${TARGET}>/sidecar"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${TARGET}>/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources" "$<TARGET_FILE_DIR:${TARGET}>/resources"
    COMMENT "同步 sidecar/ resources/ 到输出目录"
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
