# 构建后把 sidecar/（5 个 exe）与 resources/ 拷贝到目标输出目录
function(add_clashwin_copy_resources TARGET)
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/sidecar" "$<TARGET_FILE_DIR:${TARGET}>/sidecar"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources" "$<TARGET_FILE_DIR:${TARGET}>/resources"
    COMMENT "拷贝 sidecar/ resources/ 到输出目录"
  )
endfunction()

# T10 i18n：assets/locales/（13 语言 JSON）拷到输出目录（exe 同级 locales/）
function(add_clashwin_copy_locales TARGET)
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/locales" "$<TARGET_FILE_DIR:${TARGET}>/locales"
    COMMENT "拷贝 locales/ 到输出目录"
  )
endfunction()
