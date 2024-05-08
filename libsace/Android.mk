
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

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# libsace.so
LOCAL_CPPFLAGS += -fexceptions
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_AIDL_INCLUDES := $(LOCAL_PATH)/binder
LOCAL_SHARED_LIBRARIES := liblog libcutils libutils libbinder
LOCAL_MODULE := libsace

LOCAL_SRC_FILES :=        \
    binder/android/ISaceListener.aidl      \
    binder/android/ISaceManager.aidl       \
    binder/android/SaceCommand.aidl        \
    binder/android/SaceResult.aidl         \
    binder/android/SaceStatusResponse.aidl \
    SaceManager.cpp       \
    SaceSender.cpp        \
	SaceObj.cpp           \
    SaceTypes.cpp         \
    SaceServiceInfo.cpp   \
    SaceParams.cpp        \

include $(BUILD_SHARED_LIBRARY)
