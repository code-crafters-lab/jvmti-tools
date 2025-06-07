#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>
#include <thread>
#include <chrono>
#include <unordered_set>

#include "spdlog/sinks/rotating_file_sink.h"

void test() {
    std::unordered_set<std::string> target_packages = {
        "com/fr/jvm/",
        "com/fr/license/"
    };
    bool is_target = false;
    const std::string sig = "Lcom/fr/jvm/assist/FineAssist;";
    for (const auto &pkg: target_packages) {
        if (sig.find(pkg) != std::string::npos) {
            spdlog::get("as")->warn("method_entry_callback => {}", sig);
            spdlog::get("as")->warn("find: {0} != {1} {3} contains {2}", sig.find(pkg), std::string::npos, pkg, sig);
            // is_target = true;
            break;
        }
    }
    spdlog::get("as")->info("is target: {0}!", is_target);
}

int main() {
    try {
        // 1. 创建控制台彩色sink
        const auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        const auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/jvmti_agent.log", 10 * 1024 * 1024, 10, false);
        spdlog::sinks_init_list sinks = {console_sink, file_sink};

        // 2. 创建异步logger，参数为sink和队列大小
        // 第二个参数是队列大小，第三个参数表示是否自动flush
        // const auto async_logger = std::make_shared<spdlog::async_logger>(
        //     "async_console", sinks, spdlog::thread_pool(), spdlog::async_overflow_policy::block);

        const auto async_logger = spdlog::rotating_logger_mt<spdlog::async_factory>("async_console",
            "logs/async.log", 10 * 1024 * 1024, 10, false);
        // alternatively:
        // auto async_logger = spdlog::create_async<spdlog::sinks::rotating_file_sink_mt>(
        //     "async_file_logger", "logs/async2.log",10 * 1024 * 1024, 3);


        // 3. 设置日志格式
        async_logger->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%L] [%n] [%t] %v%$");

        // 4. 设置日志级别
        async_logger->set_level(spdlog::level::trace);
        async_logger->flush_on(spdlog::level::off);


        // default thread pool settings can be modified *before* creating the async logger:
        // 2. 创建异步logger，参数为sink和队列大小
        // 第二个参数是队列大小，第三个参数表示是否自动flush
        auto tp = std::make_shared<spdlog::details::thread_pool>(8, 4);
        const auto logger = std::make_shared<spdlog::async_logger>("as", sinks, tp,
                                                                   spdlog::async_overflow_policy::block);
        // logger->flush_on(spdlog::level::off);


        // 5. 注册logger，以便通过名称获取
        spdlog::register_logger(logger);
        spdlog::flush_every(std::chrono::milliseconds(500));

        // 测试日志记录
        for (int i = 0; i < 10; ++i) {
            spdlog::get("async_console")->info("异步日志测试 - 消息 {}", i);
            spdlog::get("as")->info("调试信息 - 消息 {}", i);
        }

        test();


        // 模拟异步日志处理时间
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // async_logger->flush();
        // 6. 确保所有日志都被刷新并输出
    } catch (const spdlog::spdlog_ex &ex) {
        // 初始化失败时使用标准错误输出
        fprintf(stderr, "Failed to initialize logger: %s\n", ex.what());
        return 1;
    }
    spdlog::shutdown();
    return 0;
}
