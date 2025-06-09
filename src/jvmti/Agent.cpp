//
// Created by WuYujie on 2025-06-09.
//

#include "Agent.h"
#include <exception>

namespace jvmti {

    Logger Agent::logger;  // 定义并初始化（类外）

    Agent::Agent(JavaVM *vm, char *args) {
        // : g_vm{vm}
        try {
            // 1. 获取 JVMTI 环境
            vm->GetEnv(reinterpret_cast<void **>(&g_jvmti), JVMTI_VERSION_1_2);
        } catch (std::exception &e) {
            logger.get()->error("The agent initialization failed: {}", e.what());
        }
    }

    Agent::~Agent() {
    }

    void Agent::processAgent(const bool attach) {
        this->AddCapability(&capabilities);

        this->RegisterEvent(&callbacks);

        // 全局禁用 方法进入/退出事件
        g_jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_METHOD_ENTRY, nullptr);
        g_jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_METHOD_EXIT, nullptr);

        if (attach) {
        }
    }


} // jvmti
