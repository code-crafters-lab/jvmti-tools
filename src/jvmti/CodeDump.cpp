//
// Created by WuYujie on 2025-06-09.
//

#include <filesystem>
#include <fstream>

#include "Agent.h"

namespace fs = std::filesystem;

namespace jvmti {
    class CodeDump final : private Agent {
    private:
        static std::mutex dump_mutex;

    public:
        CodeDump(JavaVM *vm, char *args) : Agent(vm, args) {
        }

    protected:
        void ParseOptions(const char *args) const override {
            // 解析命令行参数
        }

        void AddCapability(jvmtiCapabilities *capabilities) const override {
            // 添加所需的能力
            capabilities->can_generate_all_class_hook_events = 1;
        }

        void RegisterEvent(jvmtiEventCallbacks *callbacks) const override {
            // 注册事件回调
            callbacks->ClassFileLoadHook = &HandleClassFileLoad;
        }

    private:
        static void save(const std::string &base, const std::string &class_name,
                         const unsigned char *class_data, const jint class_data_len
        ) {
            if (!class_data || class_data_len <= 0) {
                return;
            }

            const auto log = logger.get(JVMTI_EVENT_CLASS_FILE_LOAD_HOOK);

            // 创建输出目录
            std::string file_path = base + class_name + ".class";

            const fs::path dir = fs::path(file_path).parent_path();
            try {
                if (!fs::exists(dir)) {
                    fs::create_directories(dir);
                }
            } catch (const fs::filesystem_error &ex) {
                if (log) log->error("Failed to create directory: {}", ex.what());
                return;
            }

            // 写入类文件
            std::lock_guard<std::mutex> lock(dump_mutex);
            try {
                if (std::ofstream file(file_path, std::ios::binary); file.is_open()) {
                    file.write(reinterpret_cast<const char *>(class_data), class_data_len);
                    file.close();
                } else {
                    if (log) log->error("Failed to open file: {}", file_path);
                }
            } catch (const std::exception &ex) {
                if (log) log->error("Error dumping class: {}", ex.what());
            }
        }

        static void HandleClassFileLoad(jvmtiEnv *jvmti_env, JNIEnv *jni_env,
                                        jclass class_being_redefined, jobject loader,
                                        const char *name, jobject protection_domain,
                                        const jint class_data_len, const unsigned char *class_data,
                                        jint *new_class_data_len, unsigned char **new_class_data) {
            if (name == nullptr || class_data_len < 0) {
                return;
            }
            save("", name, class_data, class_data_len);
        }
    };
} // jvmti
