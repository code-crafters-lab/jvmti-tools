//
// Created by WuYujie on 2025-06-08.
//

#ifndef LOGGER_H
#define LOGGER_H
#include <jvmti.h>
#include "spdlog/async.h"
#include "spdlog/spdlog.h"


namespace jvmti {

    using Event = std::variant<jvmtiEvent, int>;

    class Logger {
    private:
        static std::shared_ptr<spdlog::details::thread_pool> thread_pool_;
        static std::unordered_map<Event, std::shared_ptr<spdlog::logger> > event_loggers_;
        static std::mutex mutex_;
        static std::atomic<bool> shutdown_;

    public:
        Logger();

        virtual ~Logger();

        virtual std::string getLoggerName(Event event);

        std::shared_ptr<spdlog::logger> get(Event event = 0);

        virtual void shutdown();
    };
} // jvmti

#endif //LOGGER_H
