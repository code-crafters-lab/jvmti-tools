# ----------------------
# 定义显示系统信息的选项
# ----------------------
option(JVMTI_TOOLS_ENABLE_SYSTEM_INFO "Show system information during configuration" ON)
option(JVMTI_TOOLS_ENABLE_LOG "JVMTI 工具启用日志" ON)
option(JVMTI_TOOLS_LINK_JVM_LIBRARY "JVMTI 是否链接到 JVM" OFF)

# ----------------------
# 系统信息收集函数
# ----------------------
function(collect_system_info)
    # 计算指针位数（字节数 * 8）
    math(EXPR POINTER_SIZE_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
    message(STATUS "==================== 系统信息 ====================")
    message(STATUS "操作系统: ${CMAKE_SYSTEM}")
    message(STATUS "系统位数: ${POINTER_SIZE_BITS} 位")
    message(STATUS "处理器架构: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
    message(STATUS "CMake 版本: ${CMAKE_VERSION}")
    message(STATUS "=================================================")
endfunction()

