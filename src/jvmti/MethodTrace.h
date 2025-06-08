//
// Created by WuYujie on 2025-06-07.
//

#ifndef METHODTRACE_H
#define METHODTRACE_H
#include <string>
#include <unordered_set>
#include <chrono>
#include <jvmti.h>
#include <queue>
#include <stack>
#include <thread>

#include "spdlog/logger.h"

namespace jvmti_tools {
    // 方法调用记录（使用智能指针管理资源）
    struct MethodCall {
        jmethodID method{};
        std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
    };

    // 方法耗时统计（用于异步处理）
    struct MethodTiming {
        std::string thread_name;
        std::string class_name;
        std::string method_name;
        double elapsed_ms;
    };

    // 线程本地存储数据
    struct ThreadData {
        std::stack<MethodCall> call_stack;
        std::string thread_name;
    };

    // 全局配置
    struct AgentConfig {
        bool enabled = true;
        bool async_logging = true;
        size_t max_queue_size = 10240;
        std::unordered_set<std::string> target_packages = {
            "com/fr/jvm/",
            "com/fr/license/",
            "TestApp",
            "DataGuard",
        };
    };

    // 无界阻塞队列（支持生产者-消费者模型）
    template<typename T>
    class BlockingQueue {
    private:
        std::queue<T> queue_;
        mutable std::mutex mutex_;
        std::condition_variable not_empty_;
        std::condition_variable not_full_;
        std::atomic<size_t> max_size_;

    public:
        explicit BlockingQueue(size_t max_size = 0);

        ~BlockingQueue() = default;

        void push(const T &value);

        bool pop(T &value, std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

        size_t size() const;
    };

    // 全局状态
    class AgentState {
    private:
        std::shared_ptr<spdlog::logger> logger_;
        AgentConfig config_;
        BlockingQueue<MethodTiming> timing_queue_;
        std::atomic<bool> running_{true};
        std::thread logging_thread_;
        static thread_local std::unique_ptr<ThreadData> thread_data_;

    public:
        explicit AgentState() = default;

        explicit AgentState(const std::shared_ptr<spdlog::logger> &logger);

        ~AgentState();

        AgentConfig getConfig();

        // 获取线程本地数据
        static ThreadData &getThreadData(jvmtiEnv *jvmti, jthread thread);

        // 异步记录方法耗时
        void logTiming(const MethodTiming &timing);

    private:
        // 日志线程主循环
        void loggingLoop();

        // 写入日志文件（实际应用可替换为数据库或其他存储）
        void writeTimingToLog(const MethodTiming &timing) const;
    };
}

#endif //METHODTRACE_H
