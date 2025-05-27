#include <jvmti.h>
#include <iostream>

#include "MethodTraceAgent.h"

using namespace std;


static jvmtiEnv *jvmti = nullptr; // 全局JVMTI环境指针
static bool agent_onloaded = false; // 全局标记
static bool agent_unloaded = false; // 全局标记

// 方法进入事件回调
void JNICALL MethodEntry(jvmtiEnv *env, JNIEnv *jni_env, jthread thread, jmethodID method) {
    char *method_name = nullptr;
    char *class_signature = nullptr;
    jclass declaring_class;

    // 获取方法所属类
    env->GetMethodDeclaringClass(method, &declaring_class);

    // 获取类签名
    env->GetClassSignature(declaring_class, &class_signature, nullptr);

    if (strcmp(class_signature, "Lorg/example/TestApp;") == 0) {
        // 获取方法名称
        env->GetMethodName(method, &method_name, nullptr, nullptr);

        // 输出类名和方法名
        std::cout << "方法调用: " << class_signature << "::" << method_name << std::endl;
    }

    // 释放资源
    env->Deallocate(reinterpret_cast<unsigned char *>(method_name));
    env->Deallocate(reinterpret_cast<unsigned char *>(class_signature));
}

// ClassFileLoadHook 回调函数
void JNICALL ClassFileLoadHook(
    jvmtiEnv* jvmti_env,
    JNIEnv* jni_env,
    jclass class_being_redefined,
    jobject loader,
    const char* name,
    jobject protection_domain,
    jint class_data_len,
    const unsigned char* class_data,
    jint* new_class_data_len,
    unsigned char** new_class_data
) {

}

// 代理初始化函数
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    if (agent_onloaded) { return JNI_OK; }
    cout << "Agent_OnLoad(" << vm << ")" << endl;
    // try{
    //     auto* agent = new MethodTraceAgent();
    //     agent->Init(vm);
    //     agent->ParseOptions(options);
    //     agent->AddCapability();
    //     agent->RegisterEvent();
    // } catch (AgentException& e) {
    //     cout << "Error when enter HandleMethodEntry: " << e.what() << " [" << e.ErrCode() << "]";
    //     return JNI_ERR;
    // }
    // 获取JVMTI环境
    jint result = vm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION_1_2);
    if (result != JNI_OK) {
        std::cerr << "获取JVMTI环境失败" << std::endl;
        return JNI_ERR;
    }

    // 设置JVMTI功能
    jvmtiCapabilities capabilities = {};
    capabilities.can_generate_method_entry_events = 1;
    // capabilities.can_generate_all_class_hook_events = 1;
    // capabilities.can_redefine_classes = 1;
    // capabilities.can_retransform_classes = 1;

    if (jvmti->AddCapabilities(&capabilities) != JVMTI_ERROR_NONE) {
        std::cerr << "设置JVMTI功能失败" << std::endl;
        return JNI_ERR;
    }

    // 注册事件回调
    jvmtiEventCallbacks callbacks = {};
    callbacks.MethodEntry = &MethodEntry;

    if (jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks)) != JVMTI_ERROR_NONE) {
        std::cerr << "注册事件回调失败" << std::endl;
        return JNI_ERR;
    }

    // 启用方法进入事件
    if (jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, nullptr) != JVMTI_ERROR_NONE) {
        std::cerr << "启用方法进入事件失败" << std::endl;
        return JNI_ERR;
    }

    std::cout << "JVMTI代理加载成功" << std::endl;
    agent_onloaded = true; // 标记为已初始化
    return JNI_OK;
}

JNIEXPORT jint JNICALL
Agent_OnAttach(JavaVM *vm, char *options, void *reserved) {
    cout << "Agent_OnAttach(" << vm << ")" << endl;
    jvmtiEnv* jvmti = nullptr;
    const jint result = vm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION_1_1);
    if (result != JNI_OK) {
        printf("ERROR: Unable to access JVMTI!\n");
        return JNI_ERR;
    }
    auto err = static_cast<jvmtiError>(0);
    jclass* classes;
    jint count;

    err = jvmti->GetLoadedClasses(&count, &classes);
    if (err) {
        printf("ERROR: JVMTI GetLoadedClasses failed!\n");
    }
    for (int i = 0; i < count; i++) {
        char* sig;
        jvmti->GetClassSignature(classes[i], &sig, nullptr);
        printf("cls sig=%s\n", sig);
    }
    return JNI_OK;
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
    if (agent_unloaded) { return; }
    cout << "Agent_OnUnload(" << vm << ")" << endl;
    agent_unloaded = true;
}
