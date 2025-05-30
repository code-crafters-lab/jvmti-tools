#include <jvmti.h>
#include <iostream>
#include <cstring>
#include <classfile_constants.h>

using namespace std;

static jvmtiEnv *jvmti = nullptr; // 全局JVMTI环境指针
static JNIEnv *jni = nullptr; // 全局JNI环境指针
static bool agent_onloaded = false; // 全局标记
static bool agent_unloaded = false; // 全局标记

bool startsWith(const std::string &str, const std::string &prefix) {
    if (str.length() < prefix.length()) {
        return false;
    }
    return str.substr(0, prefix.length()) == prefix;
}

// 目标类和方法的信息
static const char *target_class_name = "DataGuard";
static const char *target_method_name = "encrypt";
static const char *target_method_signature = "([B)[B";


// 替换后的 native 方法实现
JNIEXPORT jbyteArray JNICALL encrypt(JNIEnv *, jclass, jbyteArray) {
    printf("JVMTI: 替换后的 encrypt 方法被调用\n");
    return nullptr;
}

// 替换后的 native 方法实现
JNIEXPORT jbyteArray JNICALL decrypt(JNIEnv *, jclass, jbyteArray) {
    printf("JVMTI: 替换后的 decrypt 方法被调用\n");
    return nullptr;
}


// ClassFileLoadHook 回调函数
void JNICALL ClassFileLoadHook(
    jvmtiEnv *jvmti_env,
    JNIEnv *jni_env,
    jclass class_being_redefined,
    jobject loader,
    const char *name,
    jobject protection_domain,
    jint class_data_len,
    const unsigned char *class_data,
    jint *new_class_data_len,
    unsigned char **new_class_data
) {
    // jvmti_env->SetNativeMethodPrefix("native_");
    // 定义方法映射表
    // constexpr JNINativeMethod methods[] = {
    //     {"encrypt", "([B)[B", (void *) encrypt},
    //     {"decrypt", "([B)[B", (void *) decrypt}
    // };
    //
    // // 注册方法到Java类
    // jni_env->RegisterNatives(class_being_redefined, methods, 2);
    printf("Native methods registered dynamically\n");
}

// 方法进入事件回调
void MethodEntry(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method) {
    char *method_name = nullptr;
    char *class_signature = nullptr;
    jclass declaring_class;

    // 获取方法所属类
    jvmti_env->GetMethodDeclaringClass(method, &declaring_class);

    // 获取类签名
    jvmti_env->GetClassSignature(declaring_class, &class_signature, nullptr);

    if (strcmp(class_signature, "Lorg/example/TestApp;") == 0 || startsWith(class_signature, "Lcom/grapecity")) {
        // 获取方法名称
        jvmti_env->GetMethodName(method, &method_name, nullptr, nullptr);

        // 输出类名和方法名
        std::cout << "方法调用: " << class_signature << "::" << method_name << std::endl;
    }

    // 释放资源
    jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(method_name));
    jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(class_signature));
}

void CompiledMethodLoadCallback(jvmtiEnv *jvmti_env, jmethodID method, jint code_size, const void *code_addr,
                                jint map_length,
                                const jvmtiAddrLocationMap *map,
                                const void *compile_info) {
    char *class_signature = nullptr;
    char *method_name = nullptr;
    char *method_signature = nullptr;
    jclass method_class;
    jint class_modifiers;
    jint method_modifiers;

    // 获取方法所在的类
    jvmti_env->GetMethodDeclaringClass(method, &method_class);

    // 获取类签名
    jvmti_env->GetClassSignature(method_class, &class_signature, nullptr);

    // 检查是否为目标类
    if (strcmp(class_signature, "LDataGuard;") == 0) {
        // 获取方法名称和签名
        jvmti_env->GetMethodName(method, &method_name, &method_signature, nullptr);

        // 获取类和方法的修饰符
        jvmti_env->GetClassModifiers(method_class, &class_modifiers);
        jvmti_env->GetMethodModifiers(method, &method_modifiers);

        // 检查是否为 native 方法
        if ((method_modifiers & JVM_ACC_NATIVE) != 0) {
            printf("JVMTI: 发现 native 方法: %s%s\n", method_name, method_signature);

            // 检查是否为目标方法
            if (strcmp(method_name, target_method_name) == 0 &&
                strcmp(method_signature, target_method_signature) == 0) {
                printf("JVMTI: 找到目标方法 %s%s，准备替换实现\n", method_name, method_signature);

                // 使用 SetNativeMethodPrefix 方法替换实现
                // jvmtiError error = jvmti_env->SetNativeMethodPrefix("MyNativeMethodPrefix_");
                // if (error != JNI_OK) {
                //     printf("JVMTI: 设置 Native 方法前缀失败，错误码: %d\n", error);
                // } else {
                //     printf("JVMTI: 成功设置 Native 方法前缀\n");
                // }

                // 或者使用 SetMethodImpl 方法直接替换实现
                // auto new_method_ptr = (void *) encrypt;
                // error = jvmti_env->SetMethodImpl(method, new_method_ptr);
                // if (error != JNI_OK) {
                //     printf("JVMTI: 设置方法实现失败，错误码: %d\n", codecvt_base::error);
                // } else {
                //     printf("JVMTI: 成功替换方法实现\n");
                // }
            }
        }

        // 释放资源
        if (method_name != nullptr) {
            jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(method_name));
        }
        if (method_signature != nullptr) {
            jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(method_signature));
        }
    }

    // 释放类签名资源
    if (class_signature != nullptr) {
        jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(class_signature));
    }
}

void NativeMethodBindCallback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method, void *address,
                              void **new_address_ptr) {
    cout << "NativeMethodBindCallback(" << jvmti_env << ", " << jni_env << ", " << thread << ", " << method << ", "
            << address << ", " << new_address_ptr << ")" << endl;
    *new_address_ptr = (void *) encrypt;
}


// 代理初始化函数
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    if (agent_onloaded) { return JNI_OK; }
    cout << "Agent_OnLoad(" << vm << ")" << endl;
    // vm->GetEnv(reinterpret_cast<void **>(&jvmti_env), JVMTI_VERSION_1_2);

    // 获取JVMTI环境
    jint result = vm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION_1_2);
    if (result != JNI_OK) {
        std::cerr << "获取JVMTI环境失败" << std::endl;
        return JNI_ERR;
    }

    // 设置JVMTI功能
    jvmtiCapabilities capabilities = {};
    capabilities.can_generate_method_entry_events = 1;

    capabilities.can_generate_compiled_method_load_events = 1;
    // capabilities.can_set_native_method_prefix = 1;
    // 如果使用 SetMethodImpl 需要此权限
    capabilities.can_generate_native_method_bind_events = 1;

    // capabilities.can_generate_all_class_hook_events = 1;
    // capabilities.can_redefine_classes = 1;
    // capabilities.can_retransform_classes = 1;

    if (jvmti->AddCapabilities(&capabilities) != JVMTI_ERROR_NONE) {
        std::cerr << "设置JVMTI功能失败" << std::endl;
        return JNI_ERR;
    }

    // 注册事件回调
    jvmtiEventCallbacks callbacks = {};
    // callbacks.MethodEntry = &MethodEntry;
    callbacks.CompiledMethodLoad = &CompiledMethodLoadCallback;
    callbacks.NativeMethodBind = &NativeMethodBindCallback;

    if (jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks)) != JVMTI_ERROR_NONE) {
        std::cerr << "注册事件回调失败" << std::endl;
        return JNI_ERR;
    }

    // 启用方法进入事件
    if (jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, nullptr) != JVMTI_ERROR_NONE) {
        std::cerr << "启用方法进入事件失败" << std::endl;
        return JNI_ERR;
    }
    if (jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_COMPILED_METHOD_LOAD, nullptr) != JVMTI_ERROR_NONE) {
        std::cerr << "启用方法载入事件失败" << std::endl;
        return JNI_ERR;
    }
    if (jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_NATIVE_METHOD_BIND, nullptr) != JVMTI_ERROR_NONE) {
        std::cerr << "启用本地方法绑定事件失败" << std::endl;
        return JNI_ERR;
    }

    std::cout << "JVMTI代理加载成功" << std::endl;
    agent_onloaded = true; // 标记为已初始化
    return JNI_OK;
}

JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM *vm, char *options, void *reserved) {
    cout << "Agent_OnAttach(" << vm << ")" << endl;
    jvmtiEnv *jvmti = nullptr;
    const jint result = vm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION_1_1);
    if (result != JNI_OK) {
        printf("ERROR: Unable to access JVMTI!\n");
        return JNI_ERR;
    }
    auto err = static_cast<jvmtiError>(0);
    jclass *classes;
    jint count;

    err = jvmti->GetLoadedClasses(&count, &classes);
    if (err) {
        printf("ERROR: JVMTI GetLoadedClasses failed!\n");
    }
    for (int i = 0; i < count; i++) {
        char *sig;
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
