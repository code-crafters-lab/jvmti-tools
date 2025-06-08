#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>
#include <thread>
#include <chrono>
#include <unordered_set>

#include "spdlog/sinks/rotating_file_sink.h"
#include "src/jvmti/Logger.h"

void test(jvmti::Logger logger) {
    const std::unordered_set<std::string> target_packages = {
        "com/fr/jvm/",
        "com/fr/license/"
    };
    bool is_target = false;
    const std::string sig = "Lcom/fr/jvm/assist/FineAssist;";
    const auto log = logger.get(JVMTI_EVENT_METHOD_ENTRY);
    for (const auto &pkg: target_packages) {
        if (sig.find(pkg) != std::string::npos) {
            log->warn("method_entry_callback => {}", sig);
            log->warn("find: {0} != {1} {3} contains {2}", sig.find(pkg), std::string::npos, pkg, sig);
            // is_target = true;
            break;
        }
    }
    log->info("is target: {0}!", is_target);
}

int main() {
    jvmti::Logger logger;
    const auto primary = logger.get();

    // 测试日志记录
    for (int i = 0; i < 10; ++i) {
        // 模拟异步日志处理时间
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        primary->debug("异步日志测试 - 消息 {}", i);
    }

    test(logger);

    logger.shutdown();

    return 0;
}
