//
// Created by WuYujie on 2025-06-09.
//
#include <jvmti.h>

class AgentProxy {
private:
    static JavaVM *vm;
    static bool initialized;

public:
    AgentProxy();

    static void Initialization(JavaVM *vm, char *options, bool attach = false);

    static void Shutdown(JavaVM *vm);
};

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
