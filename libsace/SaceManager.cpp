/*
 * Copyright (C) 2018-2024 The Service-And-Command Excutor Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/socket.h>
#include <utils/Mutex.h>

#include "sace/SaceManager.h"
#include <sace/SaceServiceInfo.h>

#define SACE_SENDER SACE_SENDER_SOCKEET

namespace android {

static sp<SaceSender> createSaceSender () {
#if   SACE_SENDER == SACE_SENDER_SOCKEET
    return new SaceSocketSender("sace_socket", SOCK_STREAM);
#elif SACE_SENDER == SACE_SENDER_BINDER
    return new SaceBinderSender();
#else
    return new SaceSocketSender("sace_socket", SOCK_STREAM);
#endif
}

SaceManager* SaceManager::mInstance = nullptr;

SaceManager::SaceManager () {
    /* Command Params */
    cmd_param = make_shared<SaceCommandParams>();
    cmd_param->set_uid(DEF_CMD_UID);
    cmd_param->set_gid(DEF_CMD_GID);
    cmd_param->set_seclabel(DEF_CMD_SEC);

    /* Service Params */
    service_param = make_shared<SaceEventParams>();
    service_param->set_uid(DEF_SERVICE_UID);
    service_param->set_gid(DEF_SERVICE_GID);
    service_param->set_seclabel(DEF_SERVICE_SEC);

    /* Event Params */
    event_param = make_shared<SaceEventParams>();
    event_param->set_uid(DEF_SERVICE_UID);
    event_param->set_gid(DEF_SERVICE_GID);
    event_param->set_seclabel(DEF_SERVICE_SEC);

    mSender = createSaceSender();
    mSender->setCallback(this);

    mCmdCallback = [this](SaceCommandType cmdType, uint64_t label) -> void {
        AutoMutex _lock(mMutex);

        if (cmdType == SACE_TYPE_SERVICE) {
            auto it = mServices.find(label);
            if (it != mServices.end())
                mServices.erase(it);
        }
        else if (cmdType == SACE_TYPE_NORMAL) {
            auto it = mCommands.find(label);
            if (it != mCommands.end())
                mCommands.erase(it);
        }
    };
}

SaceManager::~SaceManager () {
    mSender->setCallback(nullptr);
}

SaceManager* SaceManager::getInstance () {
    Mutex mutex;
    if (mInstance == nullptr) {
        mutex.lock();
        if (mInstance == nullptr)
            mInstance = new SaceManager();
        mutex.unlock();
    }

    return mInstance;
}

sp<SaceCommandObj> SaceManager::runCommand (const char* cmd, shared_ptr<SaceCommandParams> param, bool in) {
    sp<SaceCommandObj> cmdObj = nullptr;
    ErrorCode errCode = ERR_UNKNOWN;

    SaceCommand mCmd;
    mCmd.init();
    mCmd.type = SACE_TYPE_NORMAL;
    mCmd.normalCmdType = SACE_NORMAL_CMD_START;
    mCmd.command.assign(cmd);
    mCmd.flags = in? SACE_CMD_FLAG_IN : SACE_CMD_FLAG_OUT;

    if (!param)
        mCmd.command_params = param;
    else
        mCmd.command_params = cmd_param;

    SACE_LOGI("runCommand cmd=%s, sequence=%d, in=%d", cmd, mCmd.sequence, in);
    SaceResult mRlt = mSender->excuteCommand(mCmd);
    if (mRlt.resultStatus == SACE_RESULT_STATUS_OK) {
        if (mRlt.resultType == SACE_RESULT_TYPE_FD) {
            cmdObj = new SaceCommandObj(mSender, mRlt.label, string(cmd), mRlt.resultFd, in, mCmdCallback);

            AutoMutex _lock(mMutex);
            mCommands.insert(pair<uint64_t, sp<SaceCommandObj>>(mRlt.label, cmdObj));
        }
        else
            SACE_LOGW("finish runCommand with invalid fd %s", mCmd.to_string().c_str());
    }
    else {
        errCode = result_to_error(mRlt.resultStatus);
        SACE_LOGE("error runCommand %s", mCmd.to_string().c_str());
    }

    if (!cmdObj)
        cmdObj = new SaceCommandObj(errCode, string(cmd), SEQUENCE_TO_LABEL(mCmd.sequence, 0));

    return cmdObj;
}

sp<SaceServiceObj> SaceManager::queryService (const char* name) {
    sp<SaceServiceObj> serviceObj = nullptr;
    ErrorCode errCode = ERR_UNKNOWN;

    SaceCommand mCmd;
    mCmd.init();
    mCmd.type = SACE_TYPE_SERVICE;
    mCmd.serviceCmdType = SACE_SERVICE_CMD_INFO;
    mCmd.serviceFlags   = SACE_SERVICE_FLAG_NORMAL;
    mCmd.command.assign(SaceServiceInfo::SERVICE_GET_BY_NAME);
    mCmd.name.assign(name);

    /* query normal service */
    SACE_LOGI("queryService name=%s, sequence=%d", name, mCmd.sequence);
    SaceResult mRlt = mSender->excuteCommand(mCmd);
    if (mRlt.resultStatus == SACE_RESULT_STATUS_OK) {
        if (mRlt.resultType == SACE_RESULT_TYPE_EXTRA) {
            SaceServiceInfo::ServiceInfo info;
            memcpy(&info, mRlt.resultExtra, mRlt.resultExtraLen);
            serviceObj = new SaceServiceObj(mSender, info.label, info.name, info.cmd, mCmdCallback);
        }
        else
            SACE_LOGW("finish queryService with invalid extra %s", mCmd.to_string().c_str());
    }
    else {
        errCode = result_to_error(mRlt.resultStatus);
        SACE_LOGE("error queryService %s", mCmd.to_string().c_str());
    }

    if (!serviceObj)
        serviceObj = new SaceServiceObj(errCode, string(name), string(name), SEQUENCE_TO_LABEL(mCmd.sequence, 0));

    return serviceObj;
}

sp<SaceServiceObj> SaceManager::queryEventService (const char* name) {
    sp<SaceServiceObj> serviceObj = nullptr;
    ErrorCode errCode = ERR_UNKNOWN;

    SaceCommand mCmd;
    mCmd.init();
    mCmd.type = SACE_TYPE_EVENT;
    mCmd.eventType  = SACE_EVENT_TYPE_INFO;
    mCmd.eventFlags = SACE_EVENT_FLAG_NONE;
    mCmd.command.assign(SaceServiceInfo::SERVICE_GET_BY_NAME);
    mCmd.name.assign(name);

    SACE_LOGI("queryEventService name=%s, sequence=%d", name, mCmd.sequence);
    SaceResult mRlt = mSender->excuteCommand(mCmd);
    if (mRlt.resultStatus == SACE_RESULT_STATUS_OK) {
        if (mRlt.resultType == SACE_RESULT_TYPE_EXTRA) {
            SaceServiceInfo::ServiceInfo info;
            memcpy(&info, mRlt.resultExtra, mRlt.resultExtraLen);
            serviceObj = new SaceServiceObj(mSender, info.label, info.name, info.cmd, mCmdCallback);
        }
        else
            SACE_LOGW("finish queryEventService with invalid extra %s", mCmd.to_string().c_str());
    }
    else {
        errCode = result_to_error(mRlt.resultStatus);
        SACE_LOGE("error queryEventService %s", mCmd.to_string().c_str());
    }

    if (!serviceObj)
        serviceObj = new SaceServiceObj(errCode, string(name), string(name), SEQUENCE_TO_LABEL(mCmd.sequence, 0));

    return serviceObj;
}

sp<SaceServiceObj> SaceManager::checkService (const char *name, const char *cmd, shared_ptr<SaceCommandParams> param) {
    sp<SaceServiceObj> sve = nullptr;
    ErrorCode errCode = ERR_UNKNOWN;

    if ((sve = queryService(name))->getError() == ERR_OK ||
        (sve = queryEventService(name))->getError() == ERR_OK) {
        AutoMutex _lock(mMutex);
        mServices.insert(pair<uint64_t, sp<SaceServiceObj>>(sve->label, sve));
        return sve;
    }

    if (!cmd)
        return new SaceServiceObj(ERR_NOT_EXISTS, string(name), string(cmd), SEQUENCE_TO_LABEL(0, 0));

    /* start service */
    SaceCommand mCmd;
    mCmd.init();
    mCmd.type = SACE_TYPE_SERVICE;
    mCmd.serviceCmdType = SACE_SERVICE_CMD_START;
    mCmd.serviceFlags   = SACE_SERVICE_FLAG_NORMAL;
    mCmd.name.assign(name);
    mCmd.command.assign(cmd);
    if (!param)
        mCmd.command_params = param;
    else
        mCmd.command_params = service_param;

    SACE_LOGI("checkService name=%s, cmd=%s, sequence=%d", name, cmd, mCmd.sequence);
    SaceResult mRlt = mSender->excuteCommand(mCmd);
    if (mRlt.resultStatus == SACE_RESULT_STATUS_OK) {
        sve = new SaceServiceObj(mSender, mRlt.label, string(name), string(cmd), mCmdCallback);

        AutoMutex _lock(mMutex);
        mServices.insert(pair<uint64_t, sp<SaceServiceObj>>(mRlt.label, sve));
        return sve;
    }
    else {
        errCode = result_to_error(mRlt.resultStatus);
        SACE_LOGE("error runCommand %s", mCmd.to_string().c_str());
    }

    if (!sve)
        sve = new SaceServiceObj(errCode, string(name), string(cmd), SEQUENCE_TO_LABEL(mCmd.sequence, 0));

    return sve;
}

int SaceManager::addEvent (const char* name, const char* cmd, shared_ptr<SaceEventParams> param) {
    SaceCommand mCmd;
    mCmd.init();
    mCmd.type = SACE_TYPE_EVENT;
    mCmd.eventType  = SACE_EVENT_TYPE_ADD;
    mCmd.eventFlags = SACE_EVENT_FLAG_NONE;
    mCmd.command.assign(cmd);
    mCmd.name.assign(name);

    if (!param)
        mCmd.command_params = static_pointer_cast<SaceCommandParams>(event_param);
    else
        mCmd.command_params = static_pointer_cast<SaceCommandParams>(param);

    SACE_LOGI("addEvent name=%s, cmd=%s", name, cmd);
    SaceResult mRlt = mSender->excuteCommand(mCmd);
    return mRlt.resultStatus == SACE_RESULT_STATUS_OK;
}

int SaceManager::deleteEvent (const char* name, bool __unused stop) {
    SaceCommand mCmd;
    mCmd.init();
    mCmd.type = SACE_TYPE_EVENT;
    mCmd.eventType = SACE_EVENT_TYPE_DEL;
    mCmd.name.assign(name);

    memcpy(mCmd.extra, &stop, sizeof(bool));
    mCmd.extraLen = sizeof(bool);

    SACE_LOGI("deleteEvent name=%s", name);
    SaceResult mRlt = mSender->excuteCommand(mCmd);
    return mRlt.resultStatus == SACE_RESULT_STATUS_OK;
}

void SaceManager::onResponse (const SaceStatusResponse &response) {
    uint64_t label = response.label;

    SACE_LOGI("onResponse response=%s", response.to_string().c_str());
    if (response.type == SACE_RESPONSE_TYPE_SERVICE) {
        AutoMutex _lock(mMutex);
        auto it = mServices.find(label);
        if (it == mServices.end()) {
            SACE_LOGE("Service [%s] %s. But Can't Control By Us", response.name.c_str(), SaceStatusResponse::mapStatusStr(response.status).c_str());
            return;
        }

        sp<SaceServiceObj> sveObj = it->second;
        sveObj->setError(response_to_error(response.status));

        mServices.erase(it);
        if (mCallback != nullptr) {
            ServiceResponse rsp;
            rsp.name  = sveObj->getName();
            rsp.label = label;
            rsp.cmdLine  = sveObj->getCmd();
            rsp.state    = SaceServiceInfo::SERVICE_FINISHED;
            mCallback->handleServiceResponse(sveObj, rsp);
        }
        else
            SACE_LOGE("Service [%s] %s.", response.name.c_str(), SaceStatusResponse::mapStatusStr(response.status).c_str());
    }
    else if (response.type == SACE_RESPONSE_TYPE_NORMAL) {
        AutoMutex _lock(mMutex);
        auto it = mCommands.find(label);
        if (it == mCommands.end()) {
            SACE_LOGE("Command [%s]. But Can't Control By Us", SaceStatusResponse::mapStatusStr(response.status).c_str());
            return;
        }

        sp<SaceCommandObj> cmdObj = it->second;
        cmdObj->setError(response_to_error(response.status));

        mCommands.erase(it);
        if (mCallback != nullptr) {
            CommandResponse rsp;
            rsp.cmdLine = cmdObj->getCmd();
            rsp.label   = label;
            mCallback->handleCommandResponse(cmdObj, rsp);
        }
        else
            SACE_LOGE("Command [%s] %s.", cmdObj->getCmd().c_str(), SaceStatusResponse::mapStatusStr(response.status).c_str());
    }
}

}; //namespace android