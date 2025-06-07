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

#include "jvmti/MethodTrace.h"
#include "spdlog/async.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

using namespace std;

class JvmtiLogger {
    static std::shared_ptr<spdlog::details::thread_pool> tp;
    static std::shared_ptr<spdlog::logger> logger;
    static std::mutex mutex_;
    static std::atomic<bool> shutdown_;

public:
    static std::shared_ptr<spdlog::logger> get() {
        if (shutdown_) return nullptr;

        std::lock_guard<std::mutex> lock(mutex_);
        if (!logger) {
            if (tp == nullptr) {
                tp = std::make_shared<spdlog::details::thread_pool>(8192, 2);
            }
            try {
                // 控制台彩色日志（调试时用）
                const auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
                // console_sink->set_level(spdlog::level::trace);
                // console_sink->set_pattern("[multi_sink_example] [%^%l%$] %v");

                // 异步文件日志（按大小切割，最多保留3个备份）
                const auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    "logs/jvmti_agent.log", 10 * 1024 * 1024, 10, false);
                // file_sink->set_level(spdlog::level::trace);
                // file_sink->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%P|%t] [%L] %v%$");

                spdlog::sinks_init_list sinks = {file_sink, console_sink};

                // 同步日志器
                // logger = std::make_shared<spdlog::logger>("JVMTI", sinks);

                // 2. 创建异步 logger
                logger = std::make_shared<spdlog::async_logger>("JVMTI", sinks, tp,
                                                                spdlog::async_overflow_policy::block);
                // 设置日志格式（包含时间、线程ID、日志级别、JVM相关信息）
                logger->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%n] [%L] [%P|%t] %v%$");
                // 设置日志级别（代理开发时用debug，生产环境用info）
                logger->set_level(spdlog::level::trace);
                // 警告及以上级别立即刷新(或禁用日志刷新时间等待)
                logger->flush_on(spdlog::level::off);

                // auto sharedFileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("fileName.txt");
                // auto firstLogger = std::make_shared<spdlog::async_logger>("firstLoggerName", logger);
                // auto secondLogger = std::make_unique<spdlog::logger>("secondLoggerName", sharedFileSink);

                // register it if you need to access it globally
                spdlog::register_logger(logger);
                // 设置日志刷新间隔（每500ms刷新一次）
                spdlog::flush_every(std::chrono::milliseconds(500));
                spdlog::set_default_logger(logger);
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
                spdlog::drop_all();
                logger.reset(); // 释放共享指针
            }
        }
        shutdown_ = true;
        spdlog::shutdown();
        tp.reset(); // 释放线程池
    }
};

// 静态成员初始化
std::shared_ptr<spdlog::details::thread_pool> JvmtiLogger::tp = nullptr;
std::shared_ptr<spdlog::logger> JvmtiLogger::logger = nullptr;
std::mutex JvmtiLogger::mutex_;
std::atomic<bool> JvmtiLogger::shutdown_(false);

// 全局单例状态
static jvmti_tools::AgentState *agent_state = nullptr;
static jvmtiEnv *jvmti = nullptr; // 全局JVMTI环境指针
// static JNIEnv *jni = nullptr; // 全局JNI环境指针
// static bool agent_onloaded = false; // 全局标记
// static bool agent_unloaded = false; // 全局标记

bool startsWith(const std::string &str, const std::string &prefix) {
    if (str.length() < prefix.length()) {
        return false;
    }
    return str.substr(0, prefix.length()) == prefix;
}

namespace jvmti_tools {
    class AgentState;
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
// static std::atomic<bool> dump_enabled(true);

// 转储类文件到磁盘
void dump_class_file(const std::string &base, const std::string &class_name,
                     const unsigned char *class_data,
                     const jint class_data_len, const atomic_bool &dump_enabled = true) {
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

    if (
        startsWith(name, "com/fr/license")
        || startsWith(name, "com/fr/regist")
        || startsWith(name, "com/fr/jvm")
    ) {
        const auto log = JvmtiLogger::get();
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
            log->trace("JVMTI ClassFileLoad: {}", name);
        } else {
            // 0x03050709 ... 记录需要转换重新转换的类
            log->error("JVMTI ClassFileLoad: magic => 0x{:02X}{:02X}{:02X}{:02X},{} has been encrypted by fr",
                       magic[0], magic[1], magic[2], magic[3], name);
        }
        // jthread thread;
        // jvmtiThreadInfo thread_info;
        // jvmti_env->GetCurrentThread(&thread);
        // jvmti_env->GetThreadInfo(thread, &thread_info);
        // log->info("JVMTI ClassFileLoad: thread name => {1}, thread priority => {0}", thread_info.priority, thread_info.name);
        // if (thread_info.name != "main") {
        //     // jvmti_env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, thread);
        // }

        // 转储原始类文件
        dump_class_file("/Users/wuyujie/Project/opensource/jvmti-demo/classes/", name, class_data, class_data_len);
    }
}


// 方法进入事件回调
void method_entry_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method) {
    // if (!agent_state->getConfig().enabled) return;
    //
    // // 获取方法所属类
    // jclass declaring_class;
    // jvmti_env->GetMethodDeclaringClass(method, &declaring_class);
    //
    // // 获取类签名
    // char *class_signature = nullptr;
    // jvmti_env->GetClassSignature(declaring_class, &class_signature, nullptr);
    // const auto logger = JvmtiLogger::get();
    //
    //
    // bool is_target = false;
    // if (class_signature) {
    //     const std::string sig(class_signature);
    //     for (const auto &pkg: agent_state->getConfig().target_packages) {
    //         if (sig.find(pkg) != std::string::npos) {
    //             is_target = true;
    //             logger->trace("method_entry_callback => {} contains {}", sig.find(pkg), sig, pkg);
    //             break;
    //         }
    //     }
    //     jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(class_signature));
    // }
    //
    // if (!is_target) return;
    //
    // // 记录方法开始时间
    // auto &data = jvmti_tools::AgentState::getThreadData(jvmti, thread);
    // data.call_stack.push({method, std::chrono::high_resolution_clock::now()});

    // 获取方法所属类
    jclass declaring_class;
    jvmti_env->GetMethodDeclaringClass(method, &declaring_class);

    // 获取类签名
    char *class_signature = nullptr;
    jvmti_env->GetClassSignature(declaring_class, &class_signature, nullptr);

    // com.fr.jvm.assist.FineAssist <clinit> loadNativeLibrary, signature , findInstrumentation
    // jvmti_env->GetStackTrace(thread, 0, 0, nullptr);
    if (startsWith(className(class_signature), "java/")
        || startsWith(className(class_signature), "jdk/")
        || startsWith(className(class_signature), "sun/")
    ) {
        return;
    }

    // 开始时间点
    const auto start = std::chrono::high_resolution_clock::now();
    const auto log = JvmtiLogger::get();
    char *method_name = nullptr;
    char *method_signature = nullptr;
    if (startsWith(className(class_signature), "TestApp")
        || startsWith(className(class_signature), "DataGuard")
        || startsWith(className(class_signature), "com/grapecity/")
        || startsWith(className(class_signature), "com/fr/jvm/assist/FineAssist")
    ) {
        // 获取方法名称
        jvmti_env->GetMethodName(method, &method_name, &method_signature, nullptr);

        // 输出类名和方法名
        log->trace("JVMTI MethodEntry: class => {}, method => {}", className(class_signature), method_name);
    }
    if (strcmp(className(class_signature).c_str(), "com/fr/jvm/assist/FineAssist") == 0
        && strcmp(method_name, "loadNativeLibrary") == 0
        && strcmp(method_signature, "()V") == 0
    ) {
        log->warn("JVMTI MethodEntry: {}.{}{}", className(class_signature), method_name, method_signature);
        // jvmti_env->SetThreadLocalStorage(thread, (void*)1);
    }
    // 结束时间点
    const auto end = std::chrono::high_resolution_clock::now();
    // 计算持续时间
    const std::chrono::duration<double, std::milli> elapsed = end - start;
    log->trace("{}.{}{} Enter 内部耗时：{}ms", className(class_signature), method_name, method_signature, elapsed.count());

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

void method_exit_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method,
                          jboolean was_popped_by_exception, jvalue return_value) {
    return;
    if (!agent_state->getConfig().enabled) return;

    auto &data = jvmti_tools::AgentState::getThreadData(jvmti, thread);
    if (data.call_stack.empty()) return;

    // 获取栈顶调用记录
    const auto &call = data.call_stack.top();

    // 计算耗时（毫秒）
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end_time - call.start_time;

    // 获取方法信息
    char *method_name = nullptr;
    char *method_sig = nullptr;
    jvmti_env->GetMethodName(method, &method_name, &method_sig, nullptr);

    // 获取类信息
    jclass clazz;
    jvmti_env->GetMethodDeclaringClass(method, &clazz);
    char *class_sig = nullptr;
    jvmti_env->GetClassSignature(clazz, &class_sig, nullptr);

    // 构建耗时记录
    jvmti_tools::MethodTiming timing;
    timing.thread_name = data.thread_name;
    timing.class_name = class_sig ? class_sig : "unknown";
    timing.method_name = method_name ? method_name : "unknown";
    timing.elapsed_ms = elapsed.count();

    // 异步记录
    agent_state->logTiming(timing);

    // 释放资源
    if (method_name) jvmti->Deallocate(reinterpret_cast<unsigned char *>(method_name));
    if (method_sig) jvmti->Deallocate(reinterpret_cast<unsigned char *>(method_sig));
    if (class_sig) jvmti->Deallocate(reinterpret_cast<unsigned char *>(class_sig));

    // 弹出栈顶
    data.call_stack.pop();

    // return;
    // const auto logger = JvmtiLogger::get();
    // // 开始时间点
    // const auto start = std::chrono::high_resolution_clock::now();
    // char *method_name = nullptr;
    // char *method_signature = nullptr;
    // char *class_signature = nullptr;
    // jclass declaring_class;
    // // 获取方法所属类
    // jvmti_env->GetMethodDeclaringClass(method, &declaring_class);
    // // 获取类签名
    // jvmti_env->GetClassSignature(declaring_class, &class_signature, nullptr);
    // // 获取方法名称
    // jvmti_env->GetMethodName(method, &method_name, &method_signature, nullptr);
    // if (startsWith(className(class_signature), "com/fr/jvm/assist/FineAssist")
    //     && strcmp(method_name, "<clinit>") == 0
    //     && strcmp(method_signature, "()V") == 0
    // ) {
    //     logger->warn("JVMTI MethodExit: {}.{}{}", className(class_signature), method_name, method_signature);
    // } else {
    //     return;
    // }
    //
    // // 释放资源
    // if (class_signature != nullptr) {
    //     jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(class_signature));
    // }
    // if (method_name != nullptr) {
    //     jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(method_name));
    // }
    // if (method_signature != nullptr) {
    //     jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(method_signature));
    // }
    // // 结束时间点
    // const auto end = std::chrono::high_resolution_clock::now();
    // // 计算持续时间
    // const std::chrono::duration<double, std::milli> elapsed = end - start;
    // logger->trace("{} {} Exit 内部耗时：{}ms", className(class_signature), method_name, elapsed.count());
}

// 存储目标类名和字节码修改逻辑
static std::vector<std::string> target_classes = {
    "com/fr/license/entity/AbstractLicense",
    "com/fr/license/entity/FineLicense",
    "com/fr/license/entity/LicenseMatchInfo",
    "com/fr/license/entity/TrailLicense"
};
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
    auto logger = JvmtiLogger::get();

    char *class_signature = nullptr;
    for (jint i = 0; i < class_count; i++) {
        jvmti->GetClassSignature(classes[i], &class_signature, nullptr);
        std::string class_name = className(class_signature);
        // 检查是否是目标类
        for (const auto &target: target_classes) {
            if (class_name == target) {
                logger->debug("Found target class: {}", class_name);
                classes_to_retransform.push_back(classes[i]);
                break;
            }
        }
    }
    jvmti->Deallocate(reinterpret_cast<unsigned char *>(class_signature));
    // 释放类列表
    jvmti->Deallocate(reinterpret_cast<unsigned char *>(classes));

    // 执行类转换
    if (!classes_to_retransform.empty()) {
        logger->trace("Retransforming classes {} ...", classes_to_retransform.size());
        err = jvmti->RetransformClasses(classes_to_retransform.size(), classes_to_retransform.data());
        if (err != JNI_OK) {
            logger->error("Failed to retransform classes");
        }
    }

    return err;
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
    // com/fr/license/entity/AbstractLicense.init(Ljava/lang/String;)V
    // com/fr/license/entity/TrailLicense.maxConcurrencyLevel()I
    if (std::string(className(class_signature)) == "com/fr/license/entity/TrailLicense"
        && std::string(method_name) == "maxConcurrencyLevel"
        && std::string(method_signature) == "()I"
    ) {
        // log->warn("Discover the target method: {} {} {}", method_modifiers, method_name, method_signature);
        // log->warn("重新转换已加载的类 start");
        // target_classes.clear();
        // target_classes.emplace_back("com/fr/license/selector/LicenseConstants");
        // target_classes.emplace_back("com/fr/license/entity/AbstractLicense");
        // retransform_target_classes(jvmti_env);
        // log->warn("重新转换已加载的类 end");
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

char *class_signature;
jobject class_loader;
// 缓存已处理的线程信息，避免重复查询
std::unordered_map<jthread, jvmtiThreadInfo> threadInfoCache;

void class_load_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jclass klass) {
    const auto logger = JvmtiLogger::get();
    jvmti_env->GetClassSignature(klass, &class_signature, nullptr);
    if (const auto class_name = className(class_signature); startsWith(
        class_name, "com/fr/license/selector/LicenseConstants")) {
        logger->warn("class_load_callback: {}", class_name);
        if (const auto it = threadInfoCache.find(thread); it != threadInfoCache.end()) {
            logger->warn("class_load_callback: thread: {}，class: {}", it->second.name, class_name);
        }
    }
}

void class_prepare_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jclass klass) {
}


// Thread-4 Attach Listener
void thread_start_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, const jthread thread) {
    const auto logger = JvmtiLogger::get();
    try {
        jvmtiThreadInfo threadInfo;
        jvmti_env->GetThreadInfo(thread, &threadInfo);
        threadInfoCache[thread] = threadInfo;
        logger->info("Thread started: [{}]", threadInfo.name);
        if (startsWith(threadInfo.name, "main")
            || startsWith(threadInfo.name, "Attach Listener")
            || startsWith(threadInfo.name, "Thread-4")
        ) {
            jvmti_env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, thread);
            jvmti_env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_EXIT, thread);
        }
    } catch (std::exception &e) {
        logger->error("The registration event callback failed: {}", e.what());
    }
}

void thread_end_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, const jthread thread) {
    const auto logger = JvmtiLogger::get();
    try {
        if (const auto it = threadInfoCache.find(thread); it != threadInfoCache.end()) {
            logger->info("Thread ended: {}", it->second.name);
            threadInfoCache.erase(it);
            if (startsWith(it->second.name, "main")
                || startsWith(it->second.name, "Attach Listener")
                || startsWith(it->second.name, "Thread-4")
            ) {
                jvmti_env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_METHOD_ENTRY, thread);
                jvmti_env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_METHOD_EXIT, thread);
            }
        }
    } catch (std::exception &e) {
        logger->error("The registration event callback failed: {}", e.what());
    }
}


// 初始化 Agent 通用逻辑
jint initialize_agent(JavaVM *vm, char *options) {
    const auto log = JvmtiLogger::get();
    try {
        // 1. 获取 JVMTI 环境
        vm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION_1_2);

        // 2. 解析命令行参数（指定目标类）
        // if (options != nullptr) {
        //     std::string opt_str(options);
        //     size_t pos = 0;
        //     while ((pos = opt_str.find(',')) != std::string::npos) {
        //         target_classes.push_back(opt_str.substr(0, pos));
        //         opt_str.erase(0, pos + 1);
        //     }
        //     target_classes.push_back(opt_str);
        // }

        // 3. 设置 JVMTI 功能
        jvmtiCapabilities capabilities;
        capabilities.can_generate_all_class_hook_events = 1;
        capabilities.can_redefine_classes = 1;
        capabilities.can_retransform_classes = 1;
        capabilities.can_retransform_any_class = 1;

        capabilities.can_generate_method_entry_events = 1;
        capabilities.can_generate_method_exit_events = 1;

        capabilities.can_generate_native_method_bind_events = 1;
        jvmti->AddCapabilities(&capabilities);

        // 4. 注册事件回调
        jvmtiEventCallbacks callbacks = {};
        callbacks.ThreadStart = &thread_start_callback;
        callbacks.ThreadEnd = &thread_end_callback;
        callbacks.ClassFileLoadHook = &class_file_load_hook_callback;
        callbacks.ClassLoad = &class_load_callback;
        callbacks.ClassPrepare = &class_prepare_callback;
        callbacks.NativeMethodBind = &native_method_bind_callback;
        callbacks.MethodEntry = &method_entry_callback;
        callbacks.MethodExit = &method_exit_callback;
        jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

        std::vector<jvmtiEvent> events = {
            JVMTI_EVENT_THREAD_START,
            JVMTI_EVENT_THREAD_END,
            JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, // 启用类文件加载事件
            JVMTI_EVENT_CLASS_LOAD, // 启用类加载事件
            JVMTI_EVENT_NATIVE_METHOD_BIND, // 启用本地方法绑定事件
        };
        for (jvmtiEvent event: events) {
            jvmti->SetEventNotificationMode(JVMTI_ENABLE, event, nullptr);
        }

        // 全局禁用 方法进入/退出事件
        jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_METHOD_ENTRY, nullptr);
        jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_METHOD_EXIT, nullptr);
        // 启用主进程 方法进入/退出事件
        // jthread *main_thread;
        // jvmti->GetCurrentThread(main_thread);
        // if (main_thread != nullptr) {
        //     jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, *main_thread);
        //     jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_EXIT, *main_thread);
        // }

        // 初始化全局状态
        agent_state = new jvmti_tools::AgentState(log);
        for (auto target_package: agent_state->getConfig().target_packages) {
            log->info("Target package: {}", target_package);
        }
    } catch (std::exception &e) {
        log->error("The registration event callback failed: {}", e.what());
    }

    return JNI_OK;
}


// 代理初始化函数
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    const auto log = JvmtiLogger::get();
    const auto addr = reinterpret_cast<uintptr_t>(vm);
    log->debug("JVMTI Agent OnLoad: 0x{:016X}", addr);

    initialize_agent(vm, options);
    return JNI_OK;
}

JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM *vm, char *options, void *reserved) {
    const auto log = JvmtiLogger::get();
    const auto addr = reinterpret_cast<uintptr_t>(vm);
    log->debug("JVMTI Agent OnAttach: 0x{:016X}", addr);

    initialize_agent(vm, options);

    // 立即执行类转换
    retransform_target_classes(jvmti);
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
