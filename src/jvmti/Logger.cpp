//
// Created by WuYujie on 2025-06-08.
//

#include "Logger.h"

#include <ranges>

#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace jvmti {
    std::shared_ptr<spdlog::details::thread_pool> Logger::thread_pool_ = nullptr;
    std::unordered_map<Event, std::shared_ptr<spdlog::logger> > Logger::event_loggers_;
    std::mutex Logger::mutex_;
    std::atomic_bool Logger::shutdown_(false);

    Logger::Logger() {
        thread_pool_ = std::make_shared<spdlog::details::thread_pool>(8192, 2);
    };

    Logger::~Logger() = default;

    std::string Logger::getLoggerName(const Event event) {
        return std::visit([&](auto &&arg)-> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, jvmtiEvent>) {
                switch (arg) {
                    case JVMTI_EVENT_VM_INIT:
                        return "VM_INIT";
                    case JVMTI_EVENT_VM_DEATH:
                        return "VM_DEATH";
                    case JVMTI_EVENT_THREAD_START:
                        return "THREAD_START";
                    case JVMTI_EVENT_THREAD_END:
                        return "THREAD_END";
                    case JVMTI_EVENT_CLASS_FILE_LOAD_HOOK:
                        return "CLASS_FILE_LOAD";
                    case JVMTI_EVENT_METHOD_ENTRY:
                        return "METHOD_ENTRY";
                    case JVMTI_EVENT_METHOD_EXIT:
                        return "METHOD_EXIT";
                    case JVMTI_EVENT_NATIVE_METHOD_BIND:
                        return "METHOD_BIND";
                    default:
                        return "JVMTI";
                }
            } else {
                return "JVMTI";
            }
        }, event);
    }

    bool operator==(const Event &lhs, int rhs) {
        return lhs == static_cast<Event>(rhs);
    }

    std::shared_ptr<spdlog::logger> Logger::get(const Event event) {
        if (shutdown_) return nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        // 安全用法：先检查键是否存在
        if (event_loggers_.find(event) != event_loggers_.end()) {
            return event_loggers_[event]; // 键存在，安全访问
        }
        if (event_loggers_[event]) return event_loggers_[event];
        try {
            bool isDefaultLogger = false;

            if (event == 0) {
                isDefaultLogger = true;
            }
            // 控制台彩色日志（调试时用）
            const auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
            // console_sink->set_level(spdlog::level::trace);
            // console_sink->set_pattern("[multi_sink_example] [%^%l%$] %v");

            // 异步文件日志（按大小切割，最多保留3个备份）
            const auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                "logs/jvmti_agent.log", 10 * 1024 * 1024, 3, false);
            // file_sink->set_level(spdlog::level::trace);
            // file_sink->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%P|%t] [%L] %v%$");

            spdlog::sinks_init_list sinks = {file_sink, console_sink};

            // 同步日志器
            // logger = std::make_shared<spdlog::logger>("JVMTI", sinks);

            // 2. 创建异步 logger
            auto logger_name = getLoggerName(event);
            const auto logger = std::make_shared<spdlog::async_logger>(logger_name, sinks, thread_pool_,
                                                                       spdlog::async_overflow_policy::block);
            // 设置日志格式（包含时间、线程ID、日志级别、JVM相关信息）
            logger->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%n] [%L] [%P|%t] %v%$");
            // 设置日志级别（代理开发时用debug，生产环境用info）
            logger->set_level(spdlog::level::trace);
            // 警告及以上级别立即刷新(或禁用日志刷新时间等待)
            logger->flush_on(spdlog::level::warn);

            // register it if you need to access it globally
            // todo 如果已经存在就不注册了，注册前判断
            spdlog::register_logger(logger);

            if (isDefaultLogger) {
                spdlog::set_default_logger(logger);
            }

            // 设置日志刷新间隔（每500ms刷新一次）
            spdlog::flush_every(std::chrono::milliseconds(500));

            event_loggers_[event] = logger;

            return logger;
        } catch (const spdlog::spdlog_ex &e) {
            // 初始化失败时使用标准错误输出
            fprintf(stderr, "Failed to initialize logger: %s\n", e.what());
            return nullptr;
        }
    }

    void Logger::shutdown() { {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto logger: event_loggers_ | std::views::values) {
                if (logger) {
                    logger->flush(); // 强制刷新所有待处理日志
                    logger.reset(); // 释放共享指针
                }
            }
        }
        shutdown_ = true;
        spdlog::shutdown();
        thread_pool_.reset(); // 释放线程池
    }
} // jvmti
