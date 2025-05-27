cmake_minimum_required(VERSION 3.10)
project(jvmti-tools)

set(CMAKE_CXX_STANDARD 11)

find_package(Java REQUIRED)
if (Java_FOUND)
    message(STATUS "java found ${Java_INCLUDE_DIRS}")
    #    include_directories(${OpenCV_INCLUDE_DIRS})
    #    add_executable(myapp main.cpp)
    #    target_link_libraries(myapp ${OpenCV_LIBS})
endif ()
#find_package(JNI REQUIRED)

# 设置Java和JVMTI路径
set(JAVA_HOME "/Users/wuyujie/Library/Java/JavaVirtualMachines/ms-21.0.7/Contents/Home")
#set(JAVA_HOME "~/Library/Java/JavaVirtualMachines/ms-21.0.7/Contents/Home" CACHE STRING "Java SDK路径")
#if(NOT JAVA_HOME)
#    message(FATAL_ERROR "未设置JAVA_HOME环境变量，请通过-DJAVA_HOME=/path/to/jdk指定")
#endif()
message(STATUS "Java Home: ${JAVA_HOME}")
message(STATUS "JNI Include dirs: ${JNI_INCLUDE_DIRS}")
message(STATUS "JNI Libraries: ${JNI_LIBRARIES}")


# 获取平台类型
if (UNIX AND NOT APPLE)
    set(PLATFORM "linux")
elseif (APPLE)
    set(PLATFORM "darwin")
else ()
    set(PLATFORM "windows")
endif ()
message(STATUS "Current platform is ${PLATFORM}")

# 添加JVMTI头文件
#include_directories(${JNI_INCLUDE_DIRS})  # JNI_INCLUDE_DIRS 包含 jni.h 头文件路径
include_directories(
        ${JAVA_HOME}/include
        ${JAVA_HOME}/include/${PLATFORM}  # 根据平台选择：linux/win32/darwin
)

# 添加源文件
add_library(jvmti_example SHARED
        src/agent.cpp
)

# 链接必要的库（Linux/macOS需要）
if (UNIX AND NOT APPLE)
    target_link_libraries(jvmti_example
            ${JAVA_HOME}/lib/server/libjvm.so
    )
elseif (APPLE)
    target_link_libraries(jvmti_example
            ${JAVA_HOME}/lib/server/libjvm.dylib
    )
endif ()

# 设置输出目录和库名称和版本
set_target_properties(jvmti_example PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        PREFIX ""  # 移除lib前缀
        #        VERSION 1.0.0
)

# 可选：指定安装路径
#set(CMAKE_INSTALL_PREFIX "~/test")
#message(STATUS "${CMAKE_INSTALL_PREFIX}")
#message(STATUS "可执行文件目录 ${CMAKE_INSTALL_BINDIR}")
#message(STATUS "库文件目录 ${CMAKE_INSTALL_LIBDIR}")
#message(STATUS "头文件目录 ${CMAKE_INSTALL_INCLUDEDIR}")
#message(STATUS "数据文件目录 ${CMAKE_INSTALL_DATAROOM}")
#message(STATUS "配置文件目录 ${CMAKE_INSTALL_SYSCONFDIR}")

#install(TARGETS jvmti_example
#        LIBRARY DESTINATION lib  # 共享库安装到 lib 目录
#        ARCHIVE DESTINATION lib  # 静态库安装到 lib 目录（如果同时生成）
#        RUNTIME DESTINATION bin  # 可执行文件安装到 bin 目录
#)
