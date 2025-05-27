//
// Created by WuYujie on 2025-05-27.
//

#ifndef METHODTRACEAGENT_H
#define METHODTRACEAGENT_H
#include <jvmti.h>
#include <string>

using namespace std;

class AgentException {
public:
    AgentException(jvmtiError err) {
        m_error = err;
    }

    string what() const throw() {
        return "AgentException";
    }

    jvmtiError ErrCode() const throw() {
        return m_error;
    }

private:
    jvmtiError m_error;
};


class MethodTraceAgent {
public:
    MethodTraceAgent() {
    }

    ~MethodTraceAgent();

    void Init(JavaVM *vm);

    void ParseOptions(const char *str);

    void AddCapability();

    void RegisterEvent();

    static void JNICALL HandleMethodEntry(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread, jmethodID method);

private:
    static void CheckException(jvmtiError error) {
        // 可以根据错误类型扩展对应的异常，这里只做简单处理
        if (error != JVMTI_ERROR_NONE) {
            throw AgentException(error);
        }
    }

    static jvmtiEnv *m_jvmti;
    static char *m_filter;
};
#endif //METHODTRACEAGENT_H
