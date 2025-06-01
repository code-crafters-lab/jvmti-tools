cmake_minimum_required(VERSION 3.10)
project(jvmti-tools)

# 获取系统信息
# 计算指针位数（字节数 * 8）
math(EXPR POINTER_SIZE_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
message(STATUS "系统: ${CMAKE_SYSTEM} 位数: ${POINTER_SIZE_BITS} 位 处理器架构: ${CMAKE_HOST_SYSTEM_PROCESSOR}")

set(CMAKE_CXX_STANDARD 11)
set(JVMTI_TOOLS_LIB_NAME agent)

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

# 根据平台添加 JVMTI 头文件
include_directories(
        ${JAVA_HOME}/include
        ${JAVA_HOME}/include/${PLATFORM}  # 根据平台选择：linux/win32/darwin
)

# 根据系统类型设置编译选项
if (WIN32)
    # Windows 平台设置
    #    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
    set(CMAKE_SHARED_LIBRARY_PREFIX "")  # 移除 lib 前缀
    set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")

    # 根据位数设置
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(STATUS "配置 64 位 DLL")
#        add_definitions(-D_WIN64)
#        add_definitions(-DARCH_64BIT)
    else ()
        message(STATUS "配置 32 位 DLL")
#        add_definitions(-D_WIN32)
#        add_definitions(-DARCH_32BIT)
    endif ()
  elseif (UNIX)
    # Linux/macOS 平台设置
    #    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    set(CMAKE_SHARED_LIBRARY_PREFIX "")  # 移除 lib 前缀
endif ()

# 添加源文件
add_library(${JVMTI_TOOLS_LIB_NAME} SHARED src/agent.cpp)
add_library(data-guard SHARED src/DataGuard.h src/DataGuard.cpp)

# 查找 JVM 库文件，对于 Windows，通常在 JDK 的 lib 目录或 lib/server 目录下
set(JVM_LIB_PATH "${JAVA_HOME}/lib" CACHE PATH "JVM 库路径")
find_library(JVM_LIBRARY
        NAMES jvm
        PATHS ${JVM_LIB_PATH} ${JVM_LIB_PATH}/server
        NO_DEFAULT_PATH
)

if (JVM_LIBRARY)
    message(STATUS "找到 JVM 库: ${JVM_LIBRARY}")
    # 链接 JVM 库
#    target_link_libraries(${JVMTI_TOOLS_LIB_NAME} ${JVM_LIBRARY})
else ()
    message(FATAL_ERROR "未找到 JVM 库文件，请检查 JDK 路径和 JVM_LIB_PATH 设置")
endif ()


if (UNIX AND NOT APPLE)
    #    target_link_libraries(${JVMTI_TOOLS_LIB_NAME} ${JAVA_HOME}/lib/server/libjvm.so)
elseif (APPLE)
    #    target_link_libraries(${JVMTI_TOOLS_LIB_NAME} ${JAVA_HOME}/lib/server/libjvm.dylib)
elseif (WIN32)
    #    target_link_libraries(${JVMTI_TOOLS_LIB_NAME} ${JAVA_HOME}/lib/jvm.lib)
endif ()

# 设置输出目录和库名称和版本
#set_target_properties(${JVMTI_TOOLS_LIB_NAME} PROPERTIES
#        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
#)

# 配置安装路径前缀（可选）
if (WIN32)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install")
#    set(CMAKE_INSTALL_PREFIX "${JAVA_HOME}")
else ()
    set(CMAKE_INSTALL_PREFIX "/usr/local")
endif ()

message(STATUS "安装目录前缀 ${CMAKE_INSTALL_PREFIX}")
message(STATUS "可执行文件目录 ${CMAKE_INSTALL_BINDIR}")
message(STATUS "库文件目录 ${CMAKE_INSTALL_LIBDIR}")
message(STATUS "头文件目录 ${CMAKE_INSTALL_INCLUDEDIR}")
message(STATUS "数据文件目录 ${CMAKE_INSTALL_DATAROOM}")
message(STATUS "配置文件目录 ${CMAKE_INSTALL_SYSCONFDIR}")

install(TARGETS ${JVMTI_TOOLS_LIB_NAME} data-guard
        LIBRARY DESTINATION lib  # 动态库安装路径（Linux/macOS）
        #        ARCHIVE DESTINATION lib  # 静态库安装路径（Linux/macOS）（如果同时生成）
        RUNTIME DESTINATION bin  # 可执行文件或 DLL 安装路径（Windows）
)

#install(TARGETS data-guard LIBRARY DESTINATION lib RUNTIME DESTINATION bin)

# 添加自定义清理目标
add_custom_target(clean_install
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_INSTALL_PREFIX}
        COMMENT "清理安装目录"
)