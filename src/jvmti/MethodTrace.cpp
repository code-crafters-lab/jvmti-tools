//
// Created by WuYujie on 2025-06-07.
//

#include "MethodTrace.h"
using namespace jvmti_tools;

template<typename T>
BlockingQueue<T>::BlockingQueue(const size_t max_size) {
    max_size_ = max_size;
}

template<typename T>
void BlockingQueue<T>::push(const T &value) {
    std::unique_lock<std::mutex> lock(mutex_);

    // 队列满时等待
    not_full_.wait(lock, [this] {
        return max_size_ == 0 || queue_.size() < max_size_;
    });

    queue_.push(value);
    not_empty_.notify_one();
}

template<typename T>
bool BlockingQueue<T>::pop(T &value, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);

    // 队列为空时等待
    if (!not_empty_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
        return false;
    }

    value = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_one();
    return true;
}

template<typename T>
size_t BlockingQueue<T>::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

// 初始化线程局部变量
thread_local std::unique_ptr<ThreadData> AgentState::thread_data_ = nullptr;

AgentState::AgentState(const std::shared_ptr<spdlog::logger> &logger): timing_queue_(config_.max_queue_size) {
    // 启动异步日志线程
    logging_thread_ = std::thread(&AgentState::loggingLoop, this);
    logger_ = logger;
}


AgentState::~AgentState() {
    running_ = false;
    if (logging_thread_.joinable()) {
        logging_thread_.join();
    }
}

AgentConfig AgentState::getConfig() {
    return config_;
}

void AgentState::logTiming(const MethodTiming &timing) {
    if (config_.async_logging) {
        timing_queue_.push(timing);
    } else {
        writeTimingToLog(timing);
    }
}

ThreadData &AgentState::getThreadData(jvmtiEnv *jvmti, jthread thread) {
    if (!thread_data_) {
        thread_data_ = std::make_unique<ThreadData>();
        // 获取线程名称
        jvmtiThreadInfo jti;
        jvmti->GetThreadInfo(thread, &jti);
        if (jti.name) {
            thread_data_->thread_name = jti.name;
            // jvmti->Deallocate(reinterpret_cast<unsigned char *>(jti));
        }
    }
    return *thread_data_;
}

// 日志线程主循环
void AgentState::loggingLoop() {
    MethodTiming timing;
    while (running_) {
        if (timing_queue_.pop(timing, std::chrono::milliseconds(100))) {
            writeTimingToLog(timing);
        }
    }

    // 处理队列中剩余的所有数据
    while (timing_queue_.pop(timing)) {
        writeTimingToLog(timing);
    }
}

void AgentState::writeTimingToLog(const MethodTiming &timing) const {
    // 写入日志文件（实际应用可替换为数据库或其他存储）
    // todo 此处简化实现，实际应使用文件流或日志库
    if (logger_) {
        logger_->debug("{} {}.{} took {} ms", timing.thread_name,
                       timing.class_name, timing.method_name, timing.elapsed_ms);
    } else {
        printf("[%s] %s.%s took %.2f ms\n",
               timing.thread_name.c_str(),
               timing.class_name.c_str(),
               timing.method_name.c_str(),
               timing.elapsed_ms);
    }
}
