LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_MODULE := OnlineRadio
else
    LOCAL_MODULE := OnlineRadio64
endif
LOCAL_SRC_FILES := main.cpp mod/logger.cpp mod/config.cpp
LOCAL_CFLAGS += -O2 -mfloat-abi=softfp -DNDEBUG -std=c++17
LOCAL_C_INCLUDES += $(LOCAL_PATH)/aml-psdk-gtasa $(LOCAL_PATH)/aml-psdk-gtasa/aml-psdk/game_sa
LOCAL_LDLIBS += -llog
include $(BUILD_SHARED_LIBRARY)