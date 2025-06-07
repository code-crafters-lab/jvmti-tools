
#include "DataGuard.h"

#include <cctype>


jbyteArray Java_DataGuard_encrypt(JNIEnv *env, jclass clazz, jbyteArray input) {
    // 检查输入是否为空
    if (input == nullptr) return nullptr;

    // 获取输入数组的长度
    const jsize length = env->GetArrayLength(input);

    // 分配新数组用于存储结果
    jbyteArray output = env->NewByteArray(length);
    if (output == nullptr) {
        return nullptr; // 内存分配失败
    }

    // 获取输入数组的元素
    jbyte *inputBytes = env->GetByteArrayElements(input, nullptr);
    if (inputBytes == nullptr) {
        env->DeleteLocalRef(output);
        return nullptr;
    }

    // 处理每个字节，进行大小写转换
    jbyte *outputBytes = env->GetByteArrayElements(output, nullptr);
    if (outputBytes == nullptr) {
        env->ReleaseByteArrayElements(input, inputBytes, JNI_ABORT);
        env->DeleteLocalRef(output);
        return nullptr;
    }

    for (jsize i = 0; i < length; i++) {
        const auto c = static_cast<unsigned char>(inputBytes[i]);
        if (isupper(c)) {
            outputBytes[i] = static_cast<jbyte>(tolower(c));
        } else if (islower(c)) {
            outputBytes[i] = static_cast<jbyte>(toupper(c));
        } else {
            outputBytes[i] = inputBytes[i]; // 保持其他字符不变
        }
    }

    // 释放数组元素并提交更改
    env->ReleaseByteArrayElements(input, inputBytes, JNI_ABORT);
    env->ReleaseByteArrayElements(output, outputBytes, 0);

    return output;
}

jbyteArray Java_DataGuard_decrypt(JNIEnv *env, jclass clazz, jbyteArray input) {
    return Java_DataGuard_encrypt(env, clazz, input);
}
