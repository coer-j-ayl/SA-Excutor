
# Copyright (C) 2018-2024 The Service-And-Command Excutor Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# jni
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
	jni/com_android_sace_SaceService.cpp \
	jni/com_android_sace_SaceCommand.cpp \
	jni/com_android_sace_SaceManager.cpp

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../libsace/include
LOCAL_CPPFLAGS += -fexceptions
LOCAL_SHARED_LIBRARIES := libsace liblog libcutils libutils libbinder libnativehelper
LOCAL_MODULE := libsace_jni
include $(BUILD_SHARED_LIBRARY)

# tests
include $(CLEAR_VARS)
LOCAL_SRC_FILES := $(call all-java-files-under, tests/src)
LOCAL_JAVA_LIBRARIES := sace
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := sace_test
LOCAL_CERTIFICATE := platform
#include $(BUILD_JAVA_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := sace_jt
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_SRC_FILES := tests/sace_jt
LOCAL_REQUIRED_MODULES := sace_test
#include $(BUILD_PREBUILT)
