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

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sace/SaceObj.h>

namespace android {

enum ErrorCode response_to_error (SaceResponseStatus status) {
    switch (status) {
        case SACE_RESPONSE_STATUS_EXIT:
        case SACE_RESPONSE_STATUS_SIGNAL:
            return ERR_EXIT;
        case SACE_RESPONSE_STATUS_USER:
            return ERR_EXIT_USER;
        case SACE_RESPONSE_STATUS_UNKNOWN:
        default:
            return ERR_UNKNOWN;
    }
}

enum ErrorCode result_to_error (SaceResultStatus status) {
    switch (status) {
        case SACE_RESULT_STATUS_OK:
            return ERR_OK;
        case SACE_RESULT_STATUS_TIMEOUT:
            return ERR_TIMEOUT;
        case SACE_RESULT_STATUS_FAIL:
        case SACE_RESULT_STATUS_SECURE:
        return ERR_EXIT_USER;
            return ERR_EXIT;
        case SACE_RESULT_STATUS_EXISTS:
        default:
            return ERR_UNKNOWN;
    }
}

int SaceCommandObj::read (char* buf, int len) {
    if (!in)
        throw UnsupportedOperation("only read support");

    string cmd_str = string("command [cmd=").append(cmd).append(", sequence=")
        .append(::to_string(LABEL_TO_SEQUENCE(label))).append("] ");
    enum ErrorCode code = getError();
    if (code == ERR_UNKNOWN)
        throw RemoteException(cmd_str + "May be initialize failed");

    if (code == ERR_EXIT)
        throw RemoteException(cmd_str + " Exit Abnormally");

    if (code == ERR_EXIT_USER)
        throw InvalidOperation(cmd_str + " Has Exit By User");

    return ::read(fd, buf, len);
}

int SaceCommandObj::write (char *buf, int len) {
    if (in)
        throw UnsupportedOperation("only write support");

    string cmd_str = string("command [cmd=").append(cmd).append(", sequence=")
        .append(::to_string(LABEL_TO_SEQUENCE(label))).append("] ");
    enum ErrorCode code = getError();
    if (code == ERR_UNKNOWN)
        throw RemoteException(cmd_str + "May be initialize failed");

    if (code == ERR_EXIT)
        throw RemoteException(cmd_str + "Exit Abnormally");

    if (code == ERR_EXIT_USER)
        throw InvalidOperation(cmd_str + "Has Exit By User");

    return ::write(fd, buf, len);
}

void SaceCommandObj::close () {
    enum ErrorCode code = getError();

    if (code == ERR_EXIT || code == ERR_EXIT_USER || code == ERR_UNKNOWN) {
        SACE_LOGI("command [sequence=%d, cmd=%s, err=%d] Has Closed", LABEL_TO_SEQUENCE(label), cmd.c_str(), code);
        return;
    }

    if (fd < 0)
        return;

    ::fsync(fd);
    ::close(fd);
    fd = -1;

    SaceCommand command;
    command.label = label;
    command.sequence = LABEL_TO_SEQUENCE(label);
    command.type  = SACE_TYPE_NORMAL;
    command.normalCmdType = SACE_NORMAL_CMD_CLOSE;
    command.command.assign(cmd);
    SaceResult result = excute(command);

    setError(result_to_error(result.resultStatus));
    if (getError() != ERR_OK)
        SACE_LOGE("close %s fail", cmd.c_str());

    if (mCmdCallback)
        mCmdCallback(SACE_TYPE_NORMAL, label);
}

// -------------------------------------------------
bool SaceServiceObj::stop () {
    enum ErrorCode code = getError();

    string sve_str = string("service [name=").append(name).append(", command=")
        .append(command).append(", sequence=").append(::to_string(LABEL_TO_SEQUENCE(label))).append("]");
    if (code == ERR_UNKNOWN || code == ERR_NOT_EXISTS)
        throw RemoteException(sve_str + " Not Exists Or Not Initialize");

    if (code == ERR_EXIT)
        throw RemoteException(sve_str + " Exit Abnormally");

    if (code == ERR_EXIT_USER)
        throw InvalidOperation(sve_str + " Has Exit By User");

    if (!label) {
        SACE_LOGE("%s not Exists", sve_str.c_str());
        return false;
    }

    mCmd.init();
    mCmd.label = label;
    mCmd.sequence = LABEL_TO_SEQUENCE(label);
    mCmd.type  = SACE_TYPE_SERVICE;
    mCmd.serviceCmdType = SACE_SERVICE_CMD_STOP;

    mRlt = excute(mCmd);
    setError(result_to_error(mRlt.resultStatus));

    if (mCmdCallback)
        mCmdCallback(SACE_TYPE_SERVICE, label);

    return mRlt.resultStatus == SACE_RESULT_STATUS_OK;
}

bool SaceServiceObj::pause () {
    enum ErrorCode code = getError();

    string sve_str = string("service [name=").append(name).append(", command=")
        .append(command).append(", sequence=").append(::to_string(LABEL_TO_SEQUENCE(label))).append("]");
    if (code == ERR_UNKNOWN || code == ERR_NOT_EXISTS)
        throw RemoteException(sve_str + "Not Exists Or Not Initialize");

    if (code == ERR_EXIT)
        throw RemoteException(sve_str + "Exit Abnormally");

    if (code == ERR_EXIT_USER)
        throw InvalidOperation(sve_str + "Has Exit By User");

    if (!label) {
        SACE_LOGE("%s not Exists", sve_str.c_str());
        return false;
    }

    mCmd.init();
    mCmd.label = label;
    mCmd.sequence = LABEL_TO_SEQUENCE(label);
    mCmd.type  = SACE_TYPE_SERVICE;
    mCmd.serviceCmdType  = SACE_SERVICE_CMD_PAUSE;

    mRlt = excute(mCmd);
    setError(result_to_error(mRlt.resultStatus));

    return mRlt.resultStatus == SACE_RESULT_STATUS_OK;
}

bool SaceServiceObj::restart () {
    enum ErrorCode code = getError();

    string sve_str = string("service [name=").append(name).append(", command=")
        .append(command).append(", sequence=").append(::to_string(LABEL_TO_SEQUENCE(label))).append("]");
    if (code == ERR_UNKNOWN || code == ERR_NOT_EXISTS)
        throw RemoteException(sve_str + "Not Exists Or Not Initialize");

    if (code == ERR_EXIT)
        throw RemoteException(sve_str + "Exit Abnormally");

    if (code == ERR_EXIT_USER)
        throw InvalidOperation(sve_str + "Has Exit By User");

    if (!label) {
        SACE_LOGE("%s not Exists", sve_str.c_str());
        return false;
    }

    mCmd.init();
    mCmd.label = label;
    mCmd.sequence = LABEL_TO_SEQUENCE(label);
    mCmd.type  = SACE_TYPE_SERVICE;
    mCmd.serviceCmdType  = SACE_SERVICE_CMD_RESTART;

    mRlt = excute(mCmd);
    setError(result_to_error(mRlt.resultStatus));

    return mRlt.resultStatus == SACE_RESULT_STATUS_OK;
}

enum SaceServiceInfo::ServiceState SaceServiceObj::getState() {
    if (getError() == ERR_EXIT)
        throw RemoteException(name + " Exit Abnormally");

    if (getError() == ERR_EXIT_USER)
        throw InvalidOperation(name + " Has Exit By User");

    mCmd.init();
    mCmd.type  = SACE_TYPE_SERVICE;
    mCmd.label = label;
    mCmd.sequence = LABEL_TO_SEQUENCE(label);
    mCmd.serviceCmdType = SACE_SERVICE_CMD_INFO;
    mCmd.command.assign(SaceServiceInfo::SERVICE_GET_BY_LABEL);

    mRlt = excute(mCmd);
    setError(result_to_error(mRlt.resultStatus));

    if (mRlt.resultStatus == SACE_RESULT_STATUS_OK) {
        SaceServiceInfo::ServiceInfo info;
        memcpy(&info, mRlt.resultExtra, mRlt.resultExtraLen);
        return info.state;
    }
    else
        return SaceServiceInfo::SERVICE_DIED_UNKNOWN;
}

}; //namespace android
