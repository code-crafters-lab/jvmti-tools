cmake_minimum_required(VERSION 3.10)
project(jvmti-tools)

# 获取系统信息
# 计算指针位数（字节数 * 8）
math(EXPR POINTER_SIZE_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
message(STATUS "系统: ${CMAKE_SYSTEM} 位数: ${POINTER_SIZE_BITS} 位 处理器架构: ${CMAKE_HOST_SYSTEM_PROCESSOR}")

set(CMAKE_CXX_STANDARD 11)
set(JVMTI_TOOLS_LIB_NAME agent-tools)

set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "ON" FORCE)

# 设置 JAVA_HOME 路径
set(JAVA_HOME "$ENV{JAVA_HOME}" CACHE PATH "Java SDK 路径")
if (APPLE)
    set(JAVA_HOME "/Users/wuyujie/Library/Java/JavaVirtualMachines/ms-21.0.7/Contents/Home")
endif ()
if (NOT JAVA_HOME)
    message(FATAL_ERROR "未设置 JAVA_HOME 环境变量")
endif ()
message(STATUS "JAVA_HOME: ${JAVA_HOME}")

# 获取平台类型
if (UNIX AND NOT APPLE)
    set(PLATFORM "linux")
elseif (APPLE)
    set(PLATFORM "darwin")
elseif (WIN32)
    set(PLATFORM "win32")
else ()
    message(FATAL_ERROR "不支持的操作系统")
endif ()

message(STATUS "Current platform is ${PLATFORM}")

# 添加JVMTI头文件
include_directories(
        ${JAVA_HOME}/include
        ${JAVA_HOME}/include/${PLATFORM}  # 根据平台选择：linux/win32/darwin
)

message(STATUS ${JVMTI_TOOLS_LIB_NAME})
# 根据系统类型设置编译选项
if (WIN32)
    # Windows 平台设置
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
    set(CMAKE_SHARED_LIBRARY_PREFIX "")  # 移除 lib 前缀
    set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")

    # 根据位数设置
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        message("配置 64 位 DLL")
        add_definitions(-D_WIN64)
    else ()
        message("配置 32 位 DLL")
        add_definitions(-D_WIN32)
    endif ()
elseif (UNIX)
    # Linux/macOS 平台设置
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    set(CMAKE_SHARED_LIBRARY_PREFIX "")  # 移除 lib 前缀
endif ()

# 添加源文件
add_library(${JVMTI_TOOLS_LIB_NAME} SHARED
        src/agent.cpp
)

# 链接必要的库（Linux/macOS需要）
# todo 需要用到再使用
if (UNIX AND NOT APPLE)
#    target_link_libraries(${JVMTI_TOOLS_LIB_NAME} ${JAVA_HOME}/lib/server/libjvm.so)
elseif (APPLE)
#    target_link_libraries(${JVMTI_TOOLS_LIB_NAME} ${JAVA_HOME}/lib/server/libjvm.dylib)
elseif (WIN32)
#    target_link_libraries(${JVMTI_TOOLS_LIB_NAME} ${JAVA_HOME}/lib/jvm.lib)
endif ()

# 设置输出目录和库名称和版本
set_target_properties(${JVMTI_TOOLS_LIB_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# 可选：指定安装路径
#set(CMAKE_INSTALL_PREFIX "~/test")
message(STATUS "安装目录前缀 ${CMAKE_INSTALL_PREFIX}")
message(STATUS "可执行文件目录 ${CMAKE_INSTALL_BINDIR}")
message(STATUS "库文件目录 ${CMAKE_INSTALL_LIBDIR}")
message(STATUS "头文件目录 ${CMAKE_INSTALL_INCLUDEDIR}")
message(STATUS "数据文件目录 ${CMAKE_INSTALL_DATAROOM}")
message(STATUS "配置文件目录 ${CMAKE_INSTALL_SYSCONFDIR}")

install(TARGETS ${JVMTI_TOOLS_LIB_NAME}
        LIBRARY DESTINATION lib  # 共享库安装到 lib 目录
        ARCHIVE DESTINATION lib  # 静态库安装到 lib 目录（如果同时生成）
        RUNTIME DESTINATION bin  # 可执行文件安装到 bin 目录
)
