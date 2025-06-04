#include <jvmti.h>
#include <classfile_constants.h>
#ifdef _WIN32
#include <Windows.h>
#endif
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
                // 控制台彩色日志（调试时用）
                const auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_st>();
                console_sink->set_level(spdlog::level::trace);
                // console_sink->set_pattern("[multi_sink_example] [%^%l%$] %v");

                // 异步文件日志（按大小切割，最多保留3个备份）
                const auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    "logs/jvmti_agent.log", 10 * 1024 * 1024, 3);
                file_sink->set_level(spdlog::level::debug);


                // 组合多个sink
                std::vector<spdlog::sink_ptr> sinks = {file_sink, console_sink};

                logger = std::make_shared<spdlog::logger>("jvmti", begin(sinks), end(sinks));
                // 创建异步日志器，队列大小设为 8KB = 8192
                // auto tp = std::make_shared<spdlog::details::thread_pool>(8192, 2);
                // logger = std::make_shared<spdlog::async_logger>("as", sinks.begin(), sinks.end(), tp,
                // spdlog::async_overflow_policy::overrun_oldest);
                //register it if you need to access it globally
                spdlog::register_logger(logger);

                // 设置日志格式（包含时间、线程ID、日志级别、JVM相关信息）
                logger->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%P|%t] [%L] %v%$");

                // 设置日志级别（代理开发时用debug，生产环境用info）
                logger->set_level(spdlog::level::trace);
                // 警告及以上级别立即刷新
                logger->flush_on(spdlog::level::warn);
                // 设置日志刷新间隔（每5秒刷新一次）
                spdlog::flush_every(std::chrono::seconds(5));
                // logger->flush_on()
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

    namespace callbacks {
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
    const auto log = JvmtiLogger::get();
    if (name != nullptr) {
        log->trace("JVMTI: ClassFileLoadHook callback: {}", name);
    }

    if (startsWith(name, "DataGuard")) {
        // log->debug("JVMTI: ClassFileLoadHook callback: => {}", name);
        // 定义方法映射表
        constexpr JNINativeMethod methods[] = {};
        // 注册方法到Java类
        // jni_env->RegisterNatives(class_being_redefined, methods, 1);
    }
}

// 方法进入事件回调
void method_entry_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method) {
    char *method_name = nullptr;
    char *class_signature = nullptr;
    // jvmtiThreadInfo *info_ptr = nullptr;
    jclass declaring_class;
    const auto log = JvmtiLogger::get();

    // jvmti_env->GetThreadInfo(thread, info_ptr);
    // log->info("JVMTI: MethodEntry callback: {} {} {}", &info_ptr->name, &info_ptr->priority, &info_ptr->is_daemon);


    // 获取方法所属类
    jvmti_env->GetMethodDeclaringClass(method, &declaring_class);

    // 获取类签名
    jvmti_env->GetClassSignature(declaring_class, &class_signature, nullptr);

    if (strcmp(class_signature, "LTestApp;") == 0 || strcmp(class_signature, "LDataGuard;") == 0 || startsWith(
            class_signature, "Lcom/grapecity")) {
        // 获取方法名称
        jvmti_env->GetMethodName(method, &method_name, nullptr, nullptr);

        // 输出类名和方法名
        log->debug("method_entry => class_signature: {} method_name: {}", class_signature, method_name);
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
    const auto log = JvmtiLogger::get();

    // 获取方法所在的类
    jvmti_env->GetMethodDeclaringClass(method, &method_class);
    // 获取类签名
    jvmti_env->GetClassSignature(method_class, &class_signature, nullptr);


    if (nullptr != class_signature) {
        log->trace("native_method_bind_callback => {}", class_signature);
    }

    // 检查是否为目标类
    if (nullptr != class_signature && strcmp(class_signature, "LDataGuard;") == 0) {
        log->warn("native_method_bind_callback => {}", class_signature);

        // 获取方法名称和签名
        jvmti_env->GetMethodName(method, &method_name, &method_signature, nullptr);

        // 获取类和方法的修饰符
        jvmti_env->GetClassModifiers(method_class, &class_modifiers);
        jvmti_env->GetMethodModifiers(method, &method_modifiers);

        log->warn("Discover the target method: {} {} {}", method_name, method_signature,
                  method_modifiers & JVM_ACC_NATIVE);
        log->warn("{} => {}", address, static_cast<void *>(new_address_ptr));
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

// 存储目标类名和字节码修改逻辑
static std::vector<std::string> target_classes;

// 初始化 Agent 通用逻辑
jint initialize_agent(JavaVM *vm, char *options) {
    const auto log = JvmtiLogger::get();
    try {
        // 1. 获取 JVMTI 环境
        vm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION_1_2);

        // 2. 解析命令行参数（指定目标类）
        if (options != nullptr) {
            std::string opt_str(options);
            size_t pos = 0;
            while ((pos = opt_str.find(',')) != std::string::npos) {
                target_classes.push_back(opt_str.substr(0, pos));
                opt_str.erase(0, pos + 1);
            }
            target_classes.push_back(opt_str);
        }

        // 3. 设置 JVMTI 功能
        jvmtiCapabilities capabilities;
        capabilities.can_generate_method_entry_events = 1;
        capabilities.can_generate_all_class_hook_events = 1;
        capabilities.can_redefine_classes = 1;
        capabilities.can_retransform_classes = 1;
        capabilities.can_generate_native_method_bind_events = 1;
        jvmti->AddCapabilities(&capabilities);

        // 4. 注册事件回调
        jvmtiEventCallbacks callbacks = {};
        callbacks.MethodEntry = &method_entry_callback;
        callbacks.ClassFileLoadHook = &class_file_load_hook_callback;
        callbacks.NativeMethodBind = &native_method_bind_callback;
        jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

        std::vector<jvmtiEvent> events = {
            JVMTI_EVENT_METHOD_ENTRY, // 启用方法进入事件
            JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, // 启用类文件加载事件
            JVMTI_EVENT_NATIVE_METHOD_BIND // 启用本地方法绑定事件
        };
        for (jvmtiEvent event: events) {
            jvmti->SetEventNotificationMode(JVMTI_ENABLE, event, nullptr);
        }
    } catch (std::exception &e) {
        log->error("The registration event callback failed: {}", e.what());
        // return JNI_OK;
    }

    return JNI_OK;
}

// 执行类转换函数
jvmtiError retransform_target_classes(jvmtiEnv *jvmti) {
    // 获取所有已加载的类
    jclass *classes = nullptr;
    jint class_count = 0;
    jvmtiError err = jvmti->GetLoadedClasses(&class_count, &classes);
    if (err != JNI_OK) {
        return err;
    }

    // 筛选出需要转换的类
    std::vector<jclass> classes_to_retransform;
    for (jint i = 0; i < class_count; i++) {
        char *class_signature = nullptr;
        jvmti->GetClassSignature(classes[i], &class_signature, nullptr);

        // 移除类签名中的 'L' 和 ';'
        std::string class_name(class_signature);
        if (class_name[0] == 'L' && class_name[class_name.length() - 1] == ';') {
            class_name = class_name.substr(1, class_name.length() - 2);
        }

        // 检查是否是目标类
        for (const auto &target: target_classes) {
            if (class_name == target) {
                classes_to_retransform.push_back(classes[i]);
                break;
            }
        }

        jvmti->Deallocate(reinterpret_cast<unsigned char *>(class_signature));
    }

    // 释放类列表
    jvmti->Deallocate(reinterpret_cast<unsigned char *>(classes));

    // 执行类转换
    if (!classes_to_retransform.empty()) {
        err = jvmti->RetransformClasses(classes_to_retransform.size(), classes_to_retransform.data());
    }

    return err;
}

// 代理初始化函数
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    const auto log = JvmtiLogger::get();
    const auto addr = reinterpret_cast<uintptr_t>(vm);
    log->debug("JVMTI Agent OnLoad: 0x{:016X}", addr);
    // log->debug("JVMTI Agent OnLoad: 0x {:04x} {:04x} {:04x} {:04x}",
    //            addr >> 48 & 0xFFFF, addr >> 32 & 0xFFFF,
    //            addr >> 16 & 0xFFFF, addr & 0xFFFF);

    return initialize_agent(vm, options);
}

JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM *vm, char *options, void *reserved) {
    // initialize_agent(vm, options);
    // // 立即执行类转换
    // const jvmtiError err = retransform_target_classes(jvmti);
    // if (err != JNI_OK) {
    //     return JNI_ERR;
    // }
    return JNI_OK;
}


JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
    const auto logger = JvmtiLogger::get();
    if (logger) {
        logger->info("JVM TI Agent unloading...");
    }

    // 执行其他清理操作（如释放 JVM TI 资源）

    // 最后关闭日志器
    JvmtiLogger::shutdown();
}
