# 检测操作系统类型
OS := $(shell uname -s)

# 基础配置
CLASS_PATH?=/Users/wuyujie/Project/opensource/jvmti-demo/build/classes/java/main
JAVA_LIBRARY_PATH?=/Users/wuyujie/CLionProjects/jvmti-tools/install/lib

# 根据操作系统设置代理库名称
ifeq ($(OS), Darwin)  # macOS
    AGENT_LIB_NAME := libagent.dylib
    DATA_GUARD_LIB_NAME := libdata-guard.dylib
else ifeq ($(OS), Linux)  # Linux
    AGENT_LIB_NAME := libagent.so
    DATA_GUARD_LIB_NAME := libdata-guard.so
else ifeq ($(findstring MINGW,$(OS)), MINGW)  # Windows (MinGW)
	JAVA_LIBRARY_PATH=D:\project\open-source\jvmti-tools\install\bin
    AGENT_LIB_NAME := agent.dll
    DATA_GUARD_LIB_NAME := data-guard.dll
else
    $(error "不支持的操作系统: $(OS)")
endif

# 构建完整的代理库路径
AGENT_LIB_PATH := $(JAVA_LIBRARY_PATH)/$(AGENT_LIB_NAME)

test-app:
	@java -Dfile.encoding=UTF-8 \
		-Djava.library.path=$(JAVA_LIBRARY_PATH) \
		-agentpath:$(AGENT_LIB_PATH) \
		-cp $(CLASS_PATH) TestApp
attach-%:
	./jattach $(subst attach-,,$@) load libagent.dylib true