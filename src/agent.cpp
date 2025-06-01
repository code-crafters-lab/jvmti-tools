#include <jvmti.h>
#include <iostream>
#include <cstring>
#include <classfile_constants.h>
#include <Windows.h>
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


// 替换后的 native 方法实现
JNIEXPORT jbyteArray JNICALL encrypt(JNIEnv *, jclass, jbyteArray input) {
    printf("JVMTI: The replaced encrypt method is called\n");
    return input;
}

// 替换后的 native 方法实现
JNIEXPORT jbyteArray JNICALL decrypt(JNIEnv *, jclass, jbyteArray input) {
    printf("JVMTI: The replaced decrypt method is called \n");
    return input;
}


// ClassFileLoadHook 回调函数
void JNICALL class_file_load_hook_callback(
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
    // 非目标类，直接返回原始字节码
    // *new_class_data_len = class_data_len;
    // *new_class_data = static_cast<unsigned char *>(malloc(class_data_len));
    // memcpy(*new_class_data, class_data, class_data_len);

    if (startsWith(name, "DataGuard")) {
        // jvmti_env->GetClassMethods()
        printf("JVMTI: ClassFileLoadHook callback: => %s \n", name);
        // jvmti_env->SetNativeMethodPrefix("native_");
        // 定义方法映射表
        char e[] = "encrypt";
        char d[] = "decrypt";
        char s[] = "([B)[B";
        constexpr JNINativeMethod methods[] = {
            // {e, s, (void*)&encrypt},
            // {d, s, reinterpret_cast<void*>(decrypt)}
        };

        // 注册方法到Java类
        // jni_env->RegisterNatives(class_being_redefined, methods, 1);
        // printf("Native methods registered dynamically\n");
        return;
    }
}

// 方法进入事件回调
void method_entry_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method) {
    char *method_name = nullptr;
    char *class_signature = nullptr;
    jclass declaring_class;

    // 获取方法所属类
    jvmti_env->GetMethodDeclaringClass(method, &declaring_class);

    // 获取类签名
    jvmti_env->GetClassSignature(declaring_class, &class_signature, nullptr);

    if (strcmp(class_signature, "LTestApp;") == 0 || strcmp(class_signature, "LDataGuard;") == 0 || startsWith(
            class_signature, "Lcom/grapecity")) {
        printf("method_entry_callback jvmti: (%p) (%p) (%hhd)\n", jvmti, jvmti_env, jvmti == jvmti_env);
        printf("method_entry_callback jni: (%p) (%p) (%hhd)\n", jni, jni_env, jni == jni_env);
        // 获取方法名称
        jvmti_env->GetMethodName(method, &method_name, nullptr, nullptr);

        // 输出类名和方法名
        std::cout << "Method call: " << class_signature << "::" << method_name << std::endl;
        printf("\n");
    }

    // 释放资源
    jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(method_name));
    jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(class_signature));
}

void native_method_bind_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method, void *address,
                                 void **new_address_ptr) {
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
    // printf("native_method_bind_callback => %s\n", class_signature);


    // 检查是否为目标类
    if (nullptr != class_signature && strcmp(class_signature, "LDataGuard;") == 0) {
        printf("native_method_bind_callback => %s\n", class_signature);
        // 获取方法名称和签名
        jvmti_env->GetMethodName(method, &method_name, &method_signature, nullptr);

        // 获取类和方法的修饰符
        jvmti_env->GetClassModifiers(method_class, &class_modifiers);
        jvmti_env->GetMethodModifiers(method, &method_modifiers);

        printf("Discover the target method: %s %s %d\n", method_name, method_signature,
               method_modifiers & JVM_ACC_NATIVE);
        printf("%p => %p\n", address, new_address_ptr);
        *new_address_ptr = reinterpret_cast<void *>(encrypt);

        // // 检查是否为 native 方法
        // if ((method_modifiers & JVM_ACC_NATIVE) != 0) {
        //     printf("JVMTI: 发现 native 方法: %s%s\n", method_name, method_signature);
        //
        //     // 检查是否为目标方法
        //     if (strcmp(method_name, target_method_name) == 0 &&
        //         strcmp(method_signature, target_method_signature) == 0) {
        //         printf("JVMTI: 找到目标方法 %s%s，准备替换实现\n", method_name, method_signature);
        //
        //         // 使用 SetNativeMethodPrefix 方法替换实现
        //         // jvmtiError error = jvmti_env->SetNativeMethodPrefix("MyNativeMethodPrefix_");
        //         // if (error != JNI_OK) {
        //         //     printf("JVMTI: 设置 Native 方法前缀失败，错误码: %d\n", error);
        //         // } else {
        //         //     printf("JVMTI: 成功设置 Native 方法前缀\n");
        //         // }
        //
        //         // 或者使用 SetMethodImpl 方法直接替换实现
        //         // auto new_method_ptr = (void *) encrypt;
        //         // error = jvmti_env->SetMethodImpl(method, new_method_ptr);
        //         // if (error != JNI_OK) {
        //         //     printf("JVMTI: 设置方法实现失败，错误码: %d\n", codecvt_base::error);
        //         // } else {
        //         //     printf("JVMTI: 成功替换方法实现\n");
        //         // }
        //     }
        // }

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

// 在任意线程中获取 JNIEnv 的安全方法
JNIEnv *getJNIEnv(JavaVM *vm) {
    JNIEnv *env = nullptr;
    const jint result = vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8);

    // if (result == JNI_EDETACHED) {
    //     // 线程未关联到 JVM，需要先附加
    //     JavaVMAttachArgs attachArgs;
    //     attachArgs.version = JNI_VERSION_1_8;
    //     char str1[] = "NativeThread";
    //     attachArgs.name = str1; // 线程名称，用于调试
    //     attachArgs.group = nullptr;
    //
    //     result = vm->AttachCurrentThread(reinterpret_cast<void **>(&env), nullptr);
    //     if (result != JNI_OK) {
    //         // 附加失败，处理错误
    //         return nullptr;
    //     }
    // }
    if (result == JNI_EDETACHED) {
        // 线程未附加，需要先附加
        if (vm->AttachCurrentThread(reinterpret_cast<void **>(&env), nullptr) != JNI_OK) {
            printf("Additional failure");
            return nullptr; // 附加失败
        }
        // 附加成功，env 现在有效
    } else if (result == JNI_OK) {
        // 线程已附加，env 有效
    } else {
        return nullptr; // 其他错误
    }

    return env;
}


// 代理初始化函数
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    // 设置控制台输出为 UTF-8 编码
    // SetConsoleOutputCP(65001);
    if (agent_onloaded) { return JNI_OK; }
    cout << "Agent_OnLoad(" << vm << ")" << endl;

    // 获取 JVMTI 环境
    if (vm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION_1_2) != JNI_OK) {
        std::cerr << "Failed to obtain the JVMTI environment" << std::endl;
        return JNI_ERR;
    }

    // 获取 JNI 环境
    // jni = getJNIEnv(vm);
    // 设置JVMTI功能
    jvmtiCapabilities capabilities;
    capabilities.can_generate_method_entry_events = 1;
    capabilities.can_generate_all_class_hook_events = 1;
    capabilities.can_redefine_classes = 1;
    capabilities.can_retransform_classes = 1;
    capabilities.can_generate_native_method_bind_events = 1;
    // capabilities.can_generate_compiled_method_load_events = 1;


    if (jvmti->AddCapabilities(&capabilities) != JVMTI_ERROR_NONE) {
        std::cerr << "设置 JVMTI 功能失败" << std::endl;
        return JNI_ERR;
    }
    try {
        // 注册事件回调
        jvmtiEventCallbacks callbacks = {};
        callbacks.MethodEntry = &method_entry_callback;
        callbacks.ClassFileLoadHook = &class_file_load_hook_callback;
        callbacks.NativeMethodBind = &native_method_bind_callback;


        // 注册事件回调
        jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
        // 启用方法进入事件
        jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, nullptr);
        // 启用类文件加载事件
        jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
        // 启用本地方法绑定事件
        jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_NATIVE_METHOD_BIND, nullptr);
    } catch (std::exception &e) {
        std::cerr << "The registration event callback failed: " << e.what() << std::endl;
        return JNI_ERR;
    }

    std::cout << "The JVMTI agent was loaded successfully" << std::endl;
    agent_onloaded = true; // 标记为已初始化
    return JNI_OK;
}

JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM *vm, char *options, void *reserved) {
    cout << "Agent_OnAttach(" << vm << ")" << endl;
    return JNI_OK;
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
    if (agent_unloaded) { return; }
    cout << "Agent_OnUnload(" << vm << ")" << endl;
    agent_unloaded = true;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    // 注册JVMTI回调
    printf("JNI_OnLoad(%p)\n", vm);
    return JNI_OK;
}
