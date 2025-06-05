#include <jvmti.h>
#include <classfile_constants.h>
#ifdef _WIN32
#include <Windows.h>
#endif
#include <fstream>
#include <iostream>
#include <filesystem>
#include <mutex>
#include <atomic>

#include "spdlog/async_logger.h"
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
                    "logs/jvmti_agent.log", 10 * 1024 * 1024, 10);
                file_sink->set_level(spdlog::level::trace);
                // file_sink->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%P|%t] [%L] %v%$");


                // 组合多个sink
                std::vector<spdlog::sink_ptr> sinks = {file_sink, console_sink};

                logger = std::make_shared<spdlog::logger>("jvmti", begin(sinks), end(sinks));
                // 创建异步日志器，队列大小设为 8KB = 8192
                // auto tp = std::make_shared<spdlog::details::thread_pool>(8, 4);
                // logger = std::make_shared<spdlog::async_logger>("as", sinks.begin(), sinks.end(), tp,
                // spdlog::async_overflow_policy::block);
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

std::string className(const char *class_signature) {
    // 移除类签名中的 'L' 和 ';'
    std::string class_name(class_signature);
    if (class_name[0] == 'L' && class_name[class_name.length() - 1] == ';') {
        class_name = class_name.substr(1, class_name.length() - 2);
    }
    return class_name;
}

namespace fs = std::filesystem;
static std::mutex dump_mutex;
static std::atomic<bool> dump_enabled(true);

bool ensureDirectoryExists(const std::string &filePath) {
    const fs::path path(filePath);

    // 如果目录不存在，则创建（包括所有父目录）
    if (const fs::path directory = path.parent_path(); !fs::exists(directory)) {
        return fs::create_directories(directory);
    }

    return true; // 目录已存在
}

// 转储类文件到磁盘
void dump_class_file(const std::string &base, const std::string &class_name,
                     const unsigned char *class_data,
                     const jint class_data_len) {
    if (!dump_enabled || !class_data || class_data_len <= 0) {
        return;
    }

    const auto logger = JvmtiLogger::get();

    // 创建输出目录
    std::string file_path = base + class_name + ".class";

    const fs::path dir = fs::path(file_path).parent_path();
    try {
        if (!fs::exists(dir)) {
            fs::create_directories(dir);
        }
    } catch (const fs::filesystem_error &ex) {
        if (logger) logger->error("Failed to create directory: {}", ex.what());
        return;
    }

    // 写入类文件
    std::lock_guard<std::mutex> lock(dump_mutex);
    try {
        if (std::ofstream file(file_path, std::ios::binary); file.is_open()) {
            file.write(reinterpret_cast<const char *>(class_data), class_data_len);
            file.close();
        } else {
            if (logger) logger->error("Failed to open file: {}", file_path);
        }
    } catch (const std::exception &ex) {
        if (logger) logger->error("Error dumping class: {}", ex.what());
    }
}

jmethodID get_name_method;
jclass class_loader_class;
// ClassFileLoadHook 回调函数
void JNICALL class_file_load_hook_callback(
    jvmtiEnv *jvmti_env,
    JNIEnv *jni_env,
    jclass class_being_redefined,
    jobject loader,
    const char *name,
    jobject protection_domain,
    const jint class_data_len,
    const unsigned char *class_data,
    jint *new_class_data_len,
    unsigned char **new_class_data
) {
    // 非目标类，直接返回原始字节码
    // *new_class_data_len = class_data_len;
    // *new_class_data = static_cast<unsigned char *>(malloc(class_data_len));
    // memcpy(*new_class_data, class_data, class_data_len);

    if (name == nullptr || class_data_len < 0
        || startsWith(name, "java/")
        || startsWith(name, "jdk/")
        || startsWith(name, "com/sun/")
    ) {
        return;
    }

    if (startsWith(name, "com/fr/license") || startsWith(name, "com/fr/regist")) {
        const auto log = JvmtiLogger::get();
        log->trace("JVMTI ClassFileLoadHook: {}", name);
        // 获取前4个字节
        unsigned char magic[4] = {
            class_data[0], // 0xCA
            class_data[1], // 0xFE
            class_data[2], // 0xBA
            class_data[3] // 0xBE
        };
        // 验证魔数
        if (magic[0] == 0xCA && magic[1] == 0xFE && magic[2] == 0xBA && magic[3] == 0xBE) {
            // todo 获取类加载器名称, 查看到底是哪个类加载器解密的，或者是 attach agent 解密的
        } else {
            // 0x03050709 ... 记录需要转换重新转换的类
            log->error("Invalid magic number: 0x{:02X}{:02X}{:02X}{:02X}, {} has been modified by fr ",
                       magic[0], magic[1], magic[2], magic[3], name);
        }

        // 转储原始类文件
        dump_class_file("/Users/wuyujie/Project/opensource/jvmti-demo/classes/", name, class_data, class_data_len);
    }
}

// 方法进入事件回调
void method_entry_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method) {
    char *method_name = nullptr;
    char *method_signature = nullptr;
    char *class_signature = nullptr;
    jclass declaring_class;

    // 获取方法所属类
    jvmti_env->GetMethodDeclaringClass(method, &declaring_class);

    // 获取类签名
    jvmti_env->GetClassSignature(declaring_class, &class_signature, nullptr);
    if (startsWith(className(class_signature), "java/")
        || startsWith(className(class_signature), "jdk/")
        || startsWith(className(class_signature), "sun/")
    ) {
        return;
    }

    const auto log = JvmtiLogger::get();
    if (startsWith(className(class_signature), "TestApp") ||
        startsWith(className(class_signature), "DataGuard") ||
        startsWith(className(class_signature), "com/grapecity/") ||
        startsWith(className(class_signature), "com/fr/license/LicenseActivator")
    ) {
        // 获取方法名称
        jvmti_env->GetMethodName(method, &method_name, &method_signature, nullptr);

        // 输出类名和方法名
        log->trace("JVMTI MethodEntry: class => {}, method => {}", className(class_signature), method_name);
    }
    if (startsWith(className(class_signature), "com/fr/license/LicenseActivator")
        && strcmp(method_name, "start") == 0
        && strcmp(method_signature, "()V") == 0
    ) {
        log->warn("JVMTI MethodEntry: {}.{}{}", className(class_signature), method_name, method_signature);
        log->info("try retransform class: {}", className(class_signature));
        try {
            const jclass *classes = {&declaring_class};
            jvmti_env->RetransformClasses(1, classes);
        } catch (const std::exception &ex) {
            if (log) log->error("failed retransform class:{} => {}", className(class_signature), ex.what());
        }
    }


    // 释放资源
    if (class_signature != nullptr) {
        jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(class_signature));
    }
    if (method_name != nullptr) {
        jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(method_name));
    }
    if (method_signature != nullptr) {
        jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(method_signature));
    }
}

// 本地方法绑定回调函数
void native_method_bind_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method,
                                 void *address, void **new_address_ptr) {
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

    if (nullptr == class_signature
        || startsWith(className(class_signature), "java/")
        || startsWith(className(class_signature), "jdk/")
        || startsWith(className(class_signature), "com/sun/")
        || !startsWith(className(class_signature), "com/fr")
    ) {
        return;
    }

    // 获取方法名称和签名
    jvmti_env->GetMethodName(method, &method_name, &method_signature, nullptr);

    // 获取类和方法的修饰符
    jvmti_env->GetClassModifiers(method_class, &class_modifiers);
    jvmti_env->GetMethodModifiers(method, &method_modifiers);

    const auto log = JvmtiLogger::get();
    log->trace("JVMTI NativeMethod: {}.{}{}", className(class_signature), method_name, method_signature);

    if (startsWith(className(class_signature), "DataGuard")
        && (method_modifiers & JVM_ACC_NATIVE) == JVM_ACC_NATIVE
        && std::string(method_name) == "encrypt"
        && std::string(method_signature) == "([B)[B") {
        log->warn("Discover the target method: {} {} {}", method_modifiers, method_name, method_signature);
        log->warn("{} => {}", address, static_cast<void *>(new_address_ptr));
        *new_address_ptr = reinterpret_cast<void *>(jvmti_tools::encrypt);
    }

    // 释放资源
    if (method_name != nullptr) {
        jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(method_name));
    }
    if (method_signature != nullptr) {
        jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(method_signature));
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
            // JVMTI_EVENT_METHOD_ENTRY, // 启用方法进入事件
            JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, // 启用类文件加载事件
            JVMTI_EVENT_NATIVE_METHOD_BIND // 启用本地方法绑定事件
        };
        for (jvmtiEvent event: events) {
            jvmti->SetEventNotificationMode(JVMTI_ENABLE, event, nullptr);
        }
    } catch (std::exception &e) {
        log->error("The registration event callback failed: {}", e.what());
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

    initialize_agent(vm, options);
    return JNI_OK;
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
        logger->debug("JVMTI Agent unloading...");
    }

    // 执行其他清理操作（如释放 JVM TI 资源）
    const auto addr = reinterpret_cast<uintptr_t>(vm);
    logger->debug("JVMTI Agent Unloaded: 0x{:016X}", addr);
    // 最后关闭日志器
    JvmtiLogger::shutdown();
}
