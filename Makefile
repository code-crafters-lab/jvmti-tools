CLASS_PATH?=D:\project\open-source\jvmti-test\build\classes\java\main
JAVA_LIBRARY_PATH?=.\cmake-build-debug\install\bin
test-app:
	@java -Dfile.encoding=UTF-8 -Djava.library.path=$(JAVA_LIBRARY_PATH) -agentpath:$(JAVA_LIBRARY_PATH)\agent.dll -cp $(CLASS_PATH) TestApp