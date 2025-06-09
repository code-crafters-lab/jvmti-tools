//
// Created by WuYujie on 2025-06-09.
//

#ifndef AGENT_H
#define AGENT_H
#include <jvmti.h>

#include "Logger.h"

namespace jvmti {
    class Agent {
    protected:
        static Logger logger;
        JavaVM *g_vm = nullptr;
        jvmtiEnv *g_jvmti = nullptr;
        jvmtiCapabilities capabilities = {};
        jvmtiEventCallbacks callbacks = {};
    public:
        Agent(JavaVM *vm, char *args);

        virtual ~Agent();

        virtual void processAgent(bool attach = false);

    protected:
        virtual void ParseOptions(const char *args) const = 0;

        virtual void AddCapability(jvmtiCapabilities *capabilities) const = 0;

        virtual void RegisterEvent(jvmtiEventCallbacks *callbacks) const = 0;
    };
} // jvmti

#endif //AGENT_H
