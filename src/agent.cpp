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
#include <regex>
#include <shared_mutex>
#include <unordered_set>

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
                logger->set_level(spdlog::level::debug);
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

static const std::regex exclude_class_pattern("^(L)?(apple|java|jdk|sun|com/sun|com/apple)/");
static const std::regex include_class_pattern(
    "^L?com/fr/(general|license|plugin|protect|record|regist|security|stable|startup|web|workspace|jvm)|TestApp|DataGuard");
static std::shared_ptr<unordered_set<std::string> > encryptedClasses = nullptr;

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

std::string removeTrailingSlash(const std::string &base_path) {
    namespace fs = std::filesystem;
    fs::path p = base_path;
    if (!p.empty() && p.filename() == "") {
        // 尾部是斜杠
        p = p.parent_path();
    }
    return p.string();
}

// 转储类文件到磁盘
void dump_class_file(const std::string &base, const std::string &class_name,
                     const unsigned char *class_data,
                     const jint class_data_len, const atomic_bool &encrypted_class = false) {
    if (!class_data || class_data_len <= 0) {
        return;
    }

    const auto logger = JvmtiLogger::get();

    // 创建输出目录
    std::string file_path = std::format("{0}/{2}/{1}.class", removeTrailingSlash(base), class_name,
                                        encrypted_class ? "classes_encrypted" : "classes");

    // logger->warn("file path {}", file_path);

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

// 检测类是否被加密（示例逻辑，需根据实际加密方式调整）
bool isClassEncrypted(const unsigned char *class_data, const jsize class_data_len) {
    // 示例：检查类文件魔数（正常Java类魔数为0xCAFEBABE）
    if (class_data_len >= 4 &&
        (class_data[0] != 0xCA || class_data[1] != 0xFE ||
         class_data[2] != 0xBA || class_data[3] != 0xBE)) {
        return true; // 魔数不匹配，可能被加密
    }
    return false;
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
    if (!name || std::regex_search(name, exclude_class_pattern)) return;
    if (std::regex_search(name, include_class_pattern)) {
        const bool is_encrypted = isClassEncrypted(class_data, class_data_len);
        // 验证魔数
        if (is_encrypted) {
            encryptedClasses->insert(name);
            const auto log = JvmtiLogger::get();
            log->trace("JVMTI ClassFileLoad: encrypted_class [{}] {}",
                       std::format("{:04}", encryptedClasses->size()), name);
        }
        // todo 获取类加载器名称, 查看到底是哪个类加载器解密的，或者是 attach agent 解密的
        // log->trace("JVMTI ClassFileLoad: {}", name);

        // 转储原始类文件
        dump_class_file("/Users/wuyujie/Project/opensource/jvmti-demo/",
                        name, class_data, class_data_len, is_encrypted);
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
    // if (!agent_state->getConfig().enabled) return;
    //
    // auto &data = jvmti_tools::AgentState::getThreadData(jvmti, thread);
    // if (data.call_stack.empty()) return;
    //
    // // 获取栈顶调用记录
    // const auto &call = data.call_stack.top();
    //
    // // 计算耗时（毫秒）
    // auto end_time = std::chrono::high_resolution_clock::now();
    // std::chrono::duration<double, std::milli> elapsed = end_time - call.start_time;
    //
    // // 获取方法信息
    // char *method_name = nullptr;
    // char *method_sig = nullptr;
    // jvmti_env->GetMethodName(method, &method_name, &method_sig, nullptr);
    //
    // // 获取类信息
    // jclass clazz;
    // jvmti_env->GetMethodDeclaringClass(method, &clazz);
    // char *class_sig = nullptr;
    // jvmti_env->GetClassSignature(clazz, &class_sig, nullptr);
    //
    // // 构建耗时记录
    // jvmti_tools::MethodTiming timing;
    // timing.thread_name = data.thread_name;
    // timing.class_name = class_sig ? class_sig : "unknown";
    // timing.method_name = method_name ? method_name : "unknown";
    // timing.elapsed_ms = elapsed.count();
    //
    // // 异步记录
    // agent_state->logTiming(timing);
    //
    // // 释放资源
    // if (method_name) jvmti->Deallocate(reinterpret_cast<unsigned char *>(method_name));
    // if (method_sig) jvmti->Deallocate(reinterpret_cast<unsigned char *>(method_sig));
    // if (class_sig) jvmti->Deallocate(reinterpret_cast<unsigned char *>(class_sig));
    //
    // // 弹出栈顶
    // data.call_stack.pop();

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

// 简单的 RAII 包装类
class JvmtiResource {
public:
    JvmtiResource(jvmtiEnv *env, unsigned char *ptr) : env(env), ptr(ptr) {
    }

    ~JvmtiResource() { if (ptr) env->Deallocate(ptr); }
    explicit operator unsigned char *() const { return ptr; }

private:
    jvmtiEnv *env;
    unsigned char *ptr;
};

// 执行类转换函数
jvmtiError retransform_target_classes(jvmtiEnv *jvmti, const std::unordered_set<std::string> &target_classes) {
    // 1. 检查目标类集合是否为空
    if (target_classes.empty()) return JVMTI_ERROR_NONE;

    // 2. 获取所有已加载的类
    jclass *classes = nullptr;
    jint class_count = 0;
    jvmtiError err = jvmti->GetLoadedClasses(&class_count, &classes);
    if (err != JNI_OK) {
        return err;
    }

    // 3. 筛选出需要重新转换的类
    std::vector<jclass> classes_to_retransform;
    const auto logger = JvmtiLogger::get();

    char *class_signature = nullptr;
    for (jint i = 0; i < class_count; i++) {
        jvmti->GetClassSignature(classes[i], &class_signature, nullptr);
        std::string class_name = className(class_signature);
        // 检查是否是目标类
        for (const auto &target: target_classes) {
            if (class_name == target) {
                logger->trace("Found target class: {}", class_name);
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

// 跳板结构 - 用于保存原始方法信息
struct NativeMethodTrampoline {
    void *original_address; // 原始方法地址
    // void *replacement; // 替换方法地址
    bool active; // 是否激活
};

// 全局跳板注册表
static std::unordered_map<jmethodID, NativeMethodTrampoline> trampoline_registry;
static std::shared_mutex registry_mutex; // 读写锁

std::string jbyte_array_to_string(JNIEnv *env, jbyteArray byteArray) {
    // 1. 检查输入是否为NULL
    if (byteArray == nullptr) {
        return "";
    }

    // 2. 获取数组长度
    const jsize length = env->GetArrayLength(byteArray);
    if (length <= 0) {
        return "";
    }

    // 3. 获取数组元素
    jbyte *bytes = env->GetByteArrayElements(byteArray, nullptr);
    if (bytes == nullptr) {
        return "";
    }

    // 4. 转换为字符串（假设使用UTF-8编码）
    std::string result(reinterpret_cast<char *>(bytes), length);

    // 5. 释放数组元素
    env->ReleaseByteArrayElements(byteArray, bytes, JNI_ABORT); // 使用JNI_ABORT避免复制回JVM

    return result;
}

// 检查字节数组前几位是否满足条件
bool check_header_bytes(JNIEnv *env, jbyteArray data, const char *expected, size_t expected_len) {
    if (!data || !expected || expected_len == 0) return false;

    jsize len = env->GetArrayLength(data);
    if (len < static_cast<jsize>(expected_len)) return false;

    jbyte *buffer = new jbyte[expected_len];
    env->GetByteArrayRegion(data, 0, expected_len, buffer);

    bool match = memcmp(buffer, expected, expected_len) == 0;
    delete[] buffer;

    return match;
}

// 自定义解密函数
jbyteArray custom_decrypt(JNIEnv *env, jbyteArray data) {
    // 获取原始数据长度
    jsize original_len = env->GetArrayLength(data);
    if (original_len <= 4) {
        // 数据长度不足4字节，直接返回空
        return nullptr;
    }

    // 计算解密后的数据长度（去掉前4字节）
    jsize decrypted_len = original_len - 4;

    // 创建新的字节数组用于存储解密后的数据
    jbyteArray decrypted_data = env->NewByteArray(decrypted_len);
    if (!decrypted_data) {
        return nullptr; // 内存分配失败
    }

    // 提取原始数据中除前4字节外的内容
    jbyte *buffer = new jbyte[decrypted_len];
    env->GetByteArrayRegion(data, 4, decrypted_len, buffer);

    // 设置解密后的数据
    env->SetByteArrayRegion(decrypted_data, 0, decrypted_len, buffer);

    // 释放临时缓冲区
    delete[] buffer;

    return decrypted_data;
}

// 替换后的解密函数 - 会调用原始实现
JNIEXPORT jbyteArray JNICALL new_decrypt(JNIEnv *env, jobject object, jbyteArray data) {
    // 记录方法信息
    const auto log = JvmtiLogger::get();
    // 获取当前方法ID
    const auto clazz = env->GetObjectClass(object);
    jmethodID method_id = env->GetMethodID(clazz, "decrypt", "([B)[B");
    if (!method_id) {
        return nullptr;
    }

    // 获取原始方法地址
    std::shared_lock<std::shared_mutex> lock(registry_mutex);
    const auto it = trampoline_registry.find(method_id);
    if (it == trampoline_registry.end()) {
        return nullptr;
    }

    // 检查字节数组前几位是否符合条件（示例：前4字节为"LICx"）
    const char *expected_header = "LICx";

    jbyteArray result;
    const bool header_matched = check_header_bytes(env, data, expected_header, 4);
    log->warn("header_matched: {}", header_matched);
    if (header_matched) {
        // 满足条件时使用自定义解密方法
        result = custom_decrypt(env, data);
    } else {
        // 不满足条件时使用原始解密方法
        typedef jbyteArray (*DecryptFunc)(JNIEnv *, jobject, jbyteArray);
        const auto original_decrypt = reinterpret_cast<DecryptFunc>(it->second.original_address);
        result = original_decrypt(env, object, data);
        auto str = jbyte_array_to_string(env, result);
        log->info("{}方法解密结果: {}", header_matched ? "自定义" : "原始", str);
    }

    return result;
}

// 本地方法绑定回调函数
void native_method_bind_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method,
                                 void *address, void **new_address_ptr) {
    // 定义静态正则表达式（避免重复编译）
    static const std::regex exclude_pattern("^L?(java|sun|jdk)/");
    static const std::regex include_pattern("^L?(com/fr/|TestApp|DataGuard)");
    static const std::regex target_class_pattern("^L?com/fr/license/selector/EncryptedLicenseSelector|DataGuard");

    // 获取方法所在的类
    jclass method_class;
    if (jvmti_env->GetMethodDeclaringClass(method, &method_class) != JVMTI_ERROR_NONE) {
        return;
    }

    // 获取类签名
    char *class_signature = nullptr;
    if (jvmti_env->GetClassSignature(method_class, &class_signature, nullptr) != JVMTI_ERROR_NONE) {
        return;
    }
    JvmtiResource classResource(jvmti_env, reinterpret_cast<unsigned char *>(class_signature));

    // 过滤不需要的类
    if (!class_signature || std::regex_search(class_signature, exclude_pattern)) {
        return;
    }
    if (!std::regex_search(class_signature, include_pattern)) {
        return;
    }

    // 获取方法名称和签名
    char *method_name = nullptr;
    char *method_signature = nullptr;
    if (jvmti_env->GetMethodName(method, &method_name, &method_signature, nullptr) != JVMTI_ERROR_NONE) {
        return;
    }
    JvmtiResource methodNameResource(jvmti_env, reinterpret_cast<unsigned char *>(method_name));
    JvmtiResource methodSigResource(jvmti_env, reinterpret_cast<unsigned char *>(method_signature));

    // 获取方法修饰符
    jint method_modifiers;
    if (jvmti_env->GetMethodModifiers(method, &method_modifiers) != JVMTI_ERROR_NONE) {
        return;
    }

    // 记录方法信息
    const auto log = JvmtiLogger::get();
    log->info("JVMTI NativeMethod: {}.{}{}", className(class_signature), method_name, method_signature);

    // 检查是否为目标方法
    const bool isTargetMethod =
            std::regex_search(class_signature, target_class_pattern) &&
            (method_modifiers & JVM_ACC_PUBLIC) == JVM_ACC_PUBLIC &&
            // (method_modifiers & JVM_ACC_STATIC) == JVM_ACC_STATIC &&
            (method_modifiers & JVM_ACC_NATIVE) == JVM_ACC_NATIVE &&
            std::string(method_name) == "decrypt" &&
            std::string(method_signature) == "([B)[B";

    if (isTargetMethod) {
        log->warn("发现目标方法: {} {}", method_name, method_signature);
        // 保存原始方法信息
        {
            std::unique_lock<std::shared_mutex> lock(registry_mutex);
            trampoline_registry[method] = {
                .original_address = address,
                // .replacement = *new_address_ptr,
                .active = true
            };
        }

        if ((method_modifiers & JVM_ACC_STATIC) == JVM_ACC_STATIC) {
            *new_address_ptr = reinterpret_cast<void *>(jvmti_tools::encrypt);
        } else {
            *new_address_ptr = reinterpret_cast<void *>(&new_decrypt);
        }

        log->warn("原始地址: {} => 新地址: {}", address, *new_address_ptr);
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
    // 静态常量正则表达式，避免重复编译
    static const std::regex product_constants_pattern("Lcom/fr/stable/ProductConstants;$");
    static const std::regex general_utils_pattern("^Lcom/fr/general/GeneralUtils;$");

    const auto logger = JvmtiLogger::get();
    char *class_signature = nullptr;

    // 获取类签名
    if (jvmti_env->GetClassSignature(klass, &class_signature, nullptr) != JVMTI_ERROR_NONE) {
        return;
    }

    // 辅助函数：安全获取字符串字段值并记录日志
    auto getAndLogStringField = [&](const char *fieldName, const char *signature) {
        const jfieldID fieldId = jni_env->GetStaticFieldID(klass, fieldName, signature);
        if (!fieldId) {
            logger->warn("Field {} not found!", fieldName);
            return;
        }

        const auto strObj = static_cast<jstring>(jni_env->GetStaticObjectField(klass, fieldId));
        if (!strObj) {
            logger->warn("Field {} value is null!", fieldName);
            return;
        }

        if (const char *cStr = jni_env->GetStringUTFChars(strObj, nullptr)) {
            logger->info("{}: {}", fieldName, cStr);
            jni_env->ReleaseStringUTFChars(strObj, cStr);
        }
        jni_env->DeleteLocalRef(strObj);
    };

    // 辅助函数：安全调用静态方法并记录返回的字符串
    auto callAndLogStaticStringMethod = [&](const char *methodName, const char *signature) {
        const jmethodID methodId = jni_env->GetStaticMethodID(klass, methodName, signature);
        if (!methodId) {
            logger->warn("Method {} not found!", methodName);
            return;
        }

        const auto strObj = static_cast<jstring>(jni_env->CallStaticObjectMethod(klass, methodId));
        if (jni_env->ExceptionCheck()) {
            jni_env->ExceptionDescribe();
            jni_env->ExceptionClear();
            return;
        }

        if (!strObj) {
            logger->warn("Method {} returned null!", methodName);
            return;
        }

        if (const char *cStr = jni_env->GetStringUTFChars(strObj, nullptr)) {
            logger->info("{}: {}", methodName, cStr);
            // todo 存储方法返回值
            jni_env->ReleaseStringUTFChars(strObj, cStr);
        }
        jni_env->DeleteLocalRef(strObj);
    };

    // 处理 ProductConstants 类
    if (std::regex_match(class_signature, product_constants_pattern)) {
        getAndLogStringField("VERSION", "Ljava/lang/String;");
    }
    // 处理 GeneralUtils 类
    else if (std::regex_match(class_signature, general_utils_pattern)) {
        callAndLogStaticStringMethod("readBuildNO", "()Ljava/lang/String;");
        callAndLogStaticStringMethod("getVersion", "()Ljava/lang/String;");
    }

    // 释放类签名资源
    if (class_signature) {
        jvmti_env->Deallocate(reinterpret_cast<unsigned char *>(class_signature));
    }
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

void vm_start_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env) {
}

void exception_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method, jlocation location,
                        jobject exception,
                        jmethodID catch_method, jlocation catch_location) {
    // // 获取异常类名
    // const jclass exceptionClass = jni_env->GetObjectClass(exception);
    // char* classSignature;
    // jvmti_env->GetClassSignature(exceptionClass, &classSignature, nullptr);
    //
    // const auto log = JvmtiLogger::get();
    // // 记录异常信息
    // log->error("Exception thrown: {} at method {} at location {}", classSignature, "method", location);
    //
    // // 释放资源
    // if (classSignature != nullptr) {
    //     jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(classSignature));
    // }
}

void exception_catch_callback(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method,
                              jlocation location,
                              jobject exception) {
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

        // 初始化全局状态
        if (!encryptedClasses) {
            encryptedClasses = std::make_shared<std::unordered_set<std::string> >();
        }

        // 3. 设置 JVMTI 功能
        jvmtiCapabilities capabilities;
        capabilities.can_generate_all_class_hook_events = 1;
        capabilities.can_redefine_classes = 1;
        capabilities.can_retransform_classes = 1;
        capabilities.can_retransform_any_class = 1;

        capabilities.can_generate_method_entry_events = 1;
        capabilities.can_generate_method_exit_events = 1;

        capabilities.can_generate_exception_events = 1;
        capabilities.can_generate_native_method_bind_events = 1;
        jvmti->AddCapabilities(&capabilities);

        // 4. 注册事件回调
        jvmtiEventCallbacks callbacks = {};
        // callbacks.ThreadStart = &thread_start_callback;
        // callbacks.ThreadEnd = &thread_end_callback;
        callbacks.ClassFileLoadHook = &class_file_load_hook_callback;
        // callbacks.ClassLoad = &class_load_callback;
        callbacks.ClassPrepare = &class_prepare_callback;
        // callbacks.VMStart = &vm_start_callback;
        callbacks.Exception = &exception_callback;
        callbacks.ExceptionCatch = &exception_catch_callback;
        callbacks.NativeMethodBind = &native_method_bind_callback;
        // callbacks.MethodEntry = &method_entry_callback;
        // callbacks.MethodExit = &method_exit_callback;
        jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

        std::vector<jvmtiEvent> events = {
            // JVMTI_EVENT_THREAD_START,
            // JVMTI_EVENT_THREAD_END,
            JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, // 启用类文件加载事件
            // JVMTI_EVENT_CLASS_LOAD, // 启用类加载事件
            JVMTI_EVENT_CLASS_PREPARE, // 启用类准备事件
            // JVMTI_EVENT_VM_START, // 启用虚拟机启动事件
            JVMTI_EVENT_EXCEPTION, // 启用异常事件
            JVMTI_EVENT_EXCEPTION_CATCH, // 启用异常捕获事件
            JVMTI_EVENT_NATIVE_METHOD_BIND, // 启用本地方法绑定事件
        };
        for (jvmtiEvent event: events) {
            jvmti->SetEventNotificationMode(JVMTI_ENABLE, event, nullptr);
        }

        // 全局禁用 方法进入/退出事件
        jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_METHOD_ENTRY, nullptr);
        jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_METHOD_EXIT, nullptr);
        // 启用主进程 方法进入/退出事件
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
    retransform_target_classes(jvmti, *encryptedClasses);
    return JNI_OK;
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
    const auto logger = JvmtiLogger::get();
    // 执行其他清理操作（如释放 JVM TI 资源）
    const auto addr = reinterpret_cast<uintptr_t>(vm);
    logger->debug("JVMTI Agent Unloaded: 0x{:016X}", addr);
    // 最后关闭日志器
    JvmtiLogger::shutdown();
}
