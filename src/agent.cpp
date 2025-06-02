#include <jvmti.h>
#include <iostream>
#include <classfile_constants.h>
#ifdef _WIN32
#include <Windows.h>
#endif
// JVM TI代理中初始化spdlog
#include <spdlog/async.h>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

using namespace std;

class JvmtiLogger {
    static std::shared_ptr<spdlog::logger> logger;
    static std::mutex mutex_;
    static std::atomic<bool> shutdown_;

public:
    static std::shared_ptr<spdlog::logger> get() {
        if (shutdown_) return nullptr;

        std::lock_guard<std::mutex> lock(mutex_);
        if (!logger) {
            try {
                // 异步文件日志（按大小切割，最多保留3个备份）
                const auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    "logs/jvmti_agent.log", 10 * 1024 * 1024, 3);
                file_sink->set_level(spdlog::level::trace);

                // 控制台彩色日志（调试时用）
                const auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                // console_sink->set_level(spdlog::level::debug);
                // console_sink->set_pattern("[multi_sink_example] [%^%l%$] %v");

                // spdlog::logger logger("multi_sink", {console_sink, file_sink});
                // logger.set_level(spdlog::level::debug);

                // 组合多个sink
                std::vector<spdlog::sink_ptr> sinks{file_sink, console_sink};

                // 创建异步日志器（队列大小8192，刷新线程优先级可调整）
                // logger = std::make_shared<spdlog::async_logger>(
                //     "jvmti_agent", sinks.begin(), sinks.end(),
                //     spdlog::thread_pool(), spdlog::async_overflow_policy::block);

                // 创建异步日志器，队列大小设为 8192
                // logger = std::make_shared<spdlog::async_logger>(
                //     )
                logger = spdlog::create_async<spdlog::sinks::stderr_color_sink_mt>(
                    "jvmti_logger");
                // logger = spdlog::stdout_color_mt("console");

                // 设置日志格式（包含时间、线程ID、日志级别、JVM相关信息）
                // logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [level %l] "
                //     "[jvm pid %P] [class %v] %message");

                // 设置日志级别（代理开发时用debug，生产环境用info）
                logger->set_level(spdlog::level::debug);
                // 警告及以上级别立即刷新
                logger->flush_on(spdlog::level::warn);
            } catch (const spdlog::spdlog_ex &e) {
                // 初始化失败时使用标准错误输出
                fprintf(stderr, "Failed to initialize logger: %s\n", e.what());
            }
        }
        return logger;
    }

    static void shutdown() { {
            std::lock_guard<std::mutex> lock(mutex_);
            if (logger) {
                logger->flush(); // 强制刷新所有待处理日志
                spdlog::drop("jvmti_logger"); // 从注册表中移除
                logger.reset(); // 释放共享指针
            }
        }
        shutdown_ = true;
        spdlog::shutdown(); // 销毁线程池
    }
};

// 静态成员初始化
std::shared_ptr<spdlog::logger> JvmtiLogger::logger = nullptr;
std::mutex JvmtiLogger::mutex_;
std::atomic<bool> JvmtiLogger::shutdown_(false);


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

namespace jvmti_tools {
    // 替换后的 native 方法实现
    JNIEXPORT jbyteArray JNICALL encrypt(JNIEnv *, jclass, jbyteArray input) {
        JvmtiLogger::get()->trace("JVMTI: The replaced encrypt method is called");

        return input;
    }

    // 替换后的 native 方法实现
    JNIEXPORT jbyteArray JNICALL decrypt(JNIEnv *, jclass, jbyteArray input) {
        JvmtiLogger::get()->trace("JVMTI: The replaced decrypt method is called");
        return input;
    }
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
        JvmtiLogger::get()->debug("JVMTI: ClassFileLoadHook callback: => {}", name);
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
        // printf("method_entry_callback jvmti: (%p) (%p) (%hhd)\n", jvmti, jvmti_env, jvmti == jvmti_env);
        // printf("method_entry_callback jni: (%p) (%p) (%hhd)\n", jni, jni_env, jni == jni_env);
        // 获取方法名称
        jvmti_env->GetMethodName(method, &method_name, nullptr, nullptr);

        // 输出类名和方法名
        JvmtiLogger::get()->debug("method_entry => class_signature: {} method_name: {}", class_signature, method_name);
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
        // printf("native_method_bind_callback => %s\n", class_signature);
        JvmtiLogger::get()->info("native_method_bind_callback => {}", class_signature);

        // 获取方法名称和签名
        jvmti_env->GetMethodName(method, &method_name, &method_signature, nullptr);

        // 获取类和方法的修饰符
        jvmti_env->GetClassModifiers(method_class, &class_modifiers);
        jvmti_env->GetMethodModifiers(method, &method_modifiers);

        JvmtiLogger::get()->debug("Discover the target method: {} {} {}", method_name, method_signature,
                                  method_modifiers & JVM_ACC_NATIVE);
        // log->debug("%p => %p", address, new_address_ptr);
        *new_address_ptr = reinterpret_cast<void *>(jvmti_tools::encrypt);

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

// 简化指针格式化器（截取低16位，无0x前缀）
// template <>
// struct fmt::formatter<void*> {
//     constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
//         return ctx.begin();
//     }
//
//     template <typename FormatContext>
//     auto format(const void* ptr, FormatContext& ctx) -> decltype(ctx.out()) {
//         // 将指针转换为 uintptr_t 并截取低16位
//         const auto addr = reinterpret_cast<uintptr_t>(ptr);
//         return fmt::format_to(ctx.out(), "{:016x}", addr & 0xFFFF);
//     }
// };

// 代理初始化函数
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    // 设置控制台输出为 UTF-8 编码
    // SetConsoleOutputCP(65001);
    auto log = JvmtiLogger::get();

    if (agent_onloaded) { return JNI_OK; }
    log->debug("JVMTI Agent OnLoad: {}", static_cast<void *>(vm));
    log->debug("JVMTI Agent OnLoad: {:p}", static_cast<void *>(vm));
    log->debug("JVMTI Agent OnLoad: 0x{:016x}", reinterpret_cast<uintptr_t>(vm));

    // 获取 JVMTI 环境
    if (vm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION_1_2) != JNI_OK) {
        JvmtiLogger::get()->error("Failed to obtain the JVMTI environment");
        return JNI_OK;
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
        log->error("设置 JVMTI 功能失败");
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
        log->error("The registration event callback failed: {}", e.what());
        return JNI_ERR;
    }

    log->debug("The JVMTI agent was loaded successfully");
    agent_onloaded = true; // 标记为已初始化
    return JNI_OK;
}

JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM *vm, char *options, void *reserved) {
    cout << "Agent_OnAttach(" << vm << ")" << endl;
    return JNI_OK;
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
    if (agent_unloaded) { return; }
    const auto logger = JvmtiLogger::get();
    if (logger) {
        logger->info("JVM TI Agent unloading...");
    }

    // 执行其他清理操作（如释放 JVM TI 资源）

    // 最后关闭日志器
    JvmtiLogger::shutdown();
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    // 注册JVMTI回调
    printf("JNI_OnLoad(%p)\n", vm);
    return JNI_OK;
}
