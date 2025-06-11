//
// Created by WuYujie on 2025-06-09.
//
#include <jvmti.h>

class AgentProxy {
    static bool initialized;
    JavaVM *vm;

public:
    AgentProxy();

    static bool isInitialized() {
        return initialized;
    }

    static void Initialization(JavaVM *vm, char *options, const bool attach = false) {
        // 检查是否已初始化
        if (initialized) {
            printf("Agent already initialized, skipping re-initialization\n");
            return;
        } else {
            // 执行初始化逻辑
            printf("Initializing agent for the first time\n");

            // 这里放置你的初始化代码
            // ...

            // 设置标志为已初始化
            initialized = true;
        }

        if (attach) {
            // 执行附加逻辑
        }
    }

    static void Shutdown(JavaVM *vm) {
    }
};

bool AgentProxy::initialized = false;

// 代理随 JVM 初始化启动逻辑
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    AgentProxy::Initialization(vm, options);
    return JNI_OK;
}

// 代理动态附加处理逻辑
JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM *vm, char *options, void *reserved) {
    AgentProxy::Initialization(vm, options, true);
    return JNI_OK;
}

// 代理随 JVM 卸载关闭逻辑
JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
    AgentProxy::Shutdown(vm);
}
