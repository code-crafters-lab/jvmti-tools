cmake_minimum_required(VERSION 3.10)

include(cmake/common.cmake)

project(jvmti-tools VERSION 1.0 LANGUAGES CXX)


# ----------------------
# cmake 默认配置
# ----------------------

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_FETCHCONTENT_QUIET OFF CACHE BOOL "FetchContent QUIET")
set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "ON" FORCE)

# ----------------------
# JVMTI_TOOLS 配置
# ----------------------
#set(JVMTI_TOOLS_ENABLE_SYSTEM_INFO OFF)
set(JVMTI_TOOLS_LIB_NAME agent)

# ----------------------
# 条件输出系统信息
# ----------------------
if (JVMTI_TOOLS_ENABLE_SYSTEM_INFO)
    collect_system_info()
endif ()

include(FetchContent)

# ----------------------
# 日志源码获取
# ----------------------
if (JVMTI_TOOLS_ENABLE_LOG)
    # 可选：禁用不必要的构建选项
    set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    # 确保异步日志支持
    set(SPDLOG_BUILD_SHARED ON CACHE BOOL "" FORCE)
    set(SPDLOG_ENABLE_PCH ON CACHE BOOL "" FORCE)  # 预编译头加速
    FetchContent_Declare(
            spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG v1.15.3
            GIT_PROGRESS TRUE
    )

    if (NOT openjdk_POPULATED)
        message(STATUS "正在下载配置 spdlog ...")
        FetchContent_MakeAvailable(spdlog)
    endif ()
endif ()


# ----------------------
#  java
# ----------------------

# 设置 JAVA_HOME 路径
set(JAVA_HOME "$ENV{JAVA_HOME}" CACHE PATH "Java SDK 路径")
if (NOT JAVA_HOME)
    # 查找 Java 环境（默认查找 JRE，如需 JDK 需指定 COMPONENTS Development
    find_package(Java COMPONENTS Development Runtime)

    # 检查是否找到
    if (Java_FOUND)
        message(STATUS "Java Version: ${Java_VERSION}")
        get_filename_component(JAVA_EXECUTABLE_DIR ${Java_JAVA_EXECUTABLE} DIRECTORY)  # 获取 bin 目录路径
        get_filename_component(JAVA_HOME ${JAVA_EXECUTABLE_DIR} DIRECTORY)             # 向上一级目录即为 JAVA_HOME
    endif ()

endif ()

if (JAVA_HOME)
    # 查找 JVM 库文件，对于 Windows，通常在 JDK 的 lib 目录或 lib/server 目录下
    set(JVM_LIB_PATH "${JAVA_HOME}/lib" CACHE PATH "JVM 库路径")
    find_library(JVM_LIBRARY
            NAMES jvm
            PATHS ${JVM_LIB_PATH} ${JVM_LIB_PATH}/server
            NO_DEFAULT_PATH
    )
    if (JVM_LIBRARY AND JVMTI_TOOLS_LINK_JVM_LIBRARY)
        # 根据配置是否链接 JVM 库,默认不链接
#        target_link_libraries(${JVMTI_TOOLS_LIB_NAME} PUBLIC ${JVM_LIBRARY})
    endif ()
elseif ()
    message(FATAL_ERROR "未设置 JAVA_HOME 环境变量")
endif ()

# 获取平台类型
if (UNIX AND NOT APPLE)
    set(PLATFORM "linux")
elseif (APPLE)
    set(PLATFORM "darwin")
elseif (WIN32)
    set(PLATFORM "win32")
    target_compile_definitions(${JVMTI_TOOLS_LIB_NAME} PRIVATE _WIN32)
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
endif ()

# 添加源文件
add_library(${JVMTI_TOOLS_LIB_NAME} SHARED src/agent.cpp)
add_library(data-guard SHARED src/DataGuard.h src/DataGuard.cpp)

#target_link_libraries(${JVMTI_TOOLS_LIB_NAME} PRIVATE spdlog::spdlog)

# 配置安装路径前缀（可选）
if (WIN32)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install")
else ()
    set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/install")
endif ()

#message(STATUS "安装目录前缀 ${CMAKE_INSTALL_PREFIX}")
#message(STATUS "可执行文件目录 ${CMAKE_INSTALL_BINDIR}")
#message(STATUS "库文件目录 ${CMAKE_INSTALL_LIBDIR}")
#message(STATUS "头文件目录 ${CMAKE_INSTALL_INCLUDEDIR}")
#message(STATUS "数据文件目录 ${CMAKE_INSTALL_DATAROOM}")
#message(STATUS "配置文件目录 ${CMAKE_INSTALL_SYSCONFDIR}")

install(TARGETS ${JVMTI_TOOLS_LIB_NAME} data-guard
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}  # 动态库安装路径（Linux/macOS为lib，Windows为bin）
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}  # 可执行文件或 DLL 安装路径（Windows）
        #        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}  # 静态库路径（若有）
)

if (APPLE)
    install(TARGETS ${JVMTI_TOOLS_LIB_NAME} data-guard
            #            LIBRARY DESTINATION ${JAVA_HOME}/lib
            LIBRARY DESTINATION /Users/wuyujie/Project/opensource/jvmti-demo
    )
endif ()


# 添加自定义清理目标
add_custom_target(clean_install
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_INSTALL_PREFIX}
        COMMENT "清理安装目录"
)

#add_subdirectory(data-guard)
#add_subdirectory(utils)