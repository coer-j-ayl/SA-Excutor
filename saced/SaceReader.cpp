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

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <utils/String16.h>
#include <cutils/sockets.h>

#include "SaceReader.h"
#include "sace/SaceLog.h"

namespace android {
#define MAX_SOCKET_BUF 1024

enum SaceMessageHandlerType typeCmdToMsg (enum SaceCommandType type) {
    switch (type) {
        case SACE_TYPE_NORMAL:
            return SACE_MESSAGE_HANDLER_NORMAL;
        case SACE_TYPE_SERVICE:
            return SACE_MESSAGE_HANDLER_SERVICE;
        case SACE_TYPE_EVENT:
            return SACE_MESSAGE_HANDLER_EVENT;
        default:
            return SACE_MESSAGE_HANDLER_UNKOWN;
    }
}

bool secured_by_uid_pid (uid_t uid, pid_t pid __unused) {
    if (uid == AID_SYSTEM || uid == AID_ROOT)
        return true;

    return false;
}

SaceResult resultBySecure () {
    SaceResult result;
    result.resultType   = SACE_RESULT_TYPE_NONE;
    result.resultStatus = SACE_RESULT_STATUS_SECURE;
    result.resultExtraLen = 0;
    result.resultFd = -1;
    return result;
}

SaceResult resultByFailure () {
    SaceResult result;
    result.resultType   = SACE_RESULT_TYPE_NONE;
    result.resultStatus = SACE_RESULT_STATUS_FAIL;
    result.resultExtraLen = 0;
    result.resultFd = -1;
    return result;
}

// --------------------------------------------------------------------------------
const char* SaceSocketReader::NAME        = "SRSocket";
const char* SaceSocketReader::THREAD_NAME = "SRSocket.MT";
const char* SaceSocketReader::WRITER_NAME = "SRSocket.SaceWriter";
const int   SaceSocketReader::MONITOR_TIMEOUT = 10; //10s

status_t SaceSocketReader::MonitorThread::readyToRun () {
    SACE_LOGI("%s Starting %d:%d", mReader->getName(), getpid(), gettid());
    return NO_ERROR;
}

bool SaceSocketReader::MonitorThread::threadLoop () {
    bool ret;
    do {
        ret = mReader->recv_data_or_connection();
    } while(!exitPending() || ret);

    return false;
}

void SaceSocketReader::handle_socket_msg (ClientSocket& climsg, sp<SaceCommand>& saceCmd) {
    SACE_LOGI("%s handle command : %s", getName(), saceCmd->to_string().c_str());
    if (secured_by_uid_pid(climsg.client.uid, climsg.client.pid)) {
        sp<SaceReaderMessage> saceMsg = new SaceReaderMessage;
        saceMsg->msgHandler = typeCmdToMsg(saceCmd->type);
        saceMsg->msgCmd     = saceCmd;
        saceMsg->msgWriter  = climsg.writer;
        saceMsg->msgClient  = climsg.client;
        post(saceMsg);
    }
    else {
        SaceResult rslt = resultBySecure();
        climsg.writer->sendResult(rslt);
    }
}

void SaceSocketReader::handle_socket_close (ClientSocket& climsg) {
    sp<SaceCommand> saceCmd = new SaceCommand();
    SACE_LOGI("%s client[%d:%d] close command", getName(), climsg.client.uid, climsg.client.pid);

    saceCmd->init();
    saceCmd->sequence = 0;
    saceCmd->normalCmdType = SACE_NORMAL_CMD_DESTROY;
    saceCmd->name = ::to_string(climsg.client.pid).append(":0");

    sp<SaceReaderMessage> saceMsg = new SaceReaderMessage;
    saceMsg->msgHandler = SACE_MESSAGE_HANDLER_NORMAL;
    saceMsg->msgCmd     = saceCmd;
    saceMsg->msgWriter  = climsg.writer;
    saceMsg->msgClient  = climsg.client;
    post(saceMsg);
}

bool SaceSocketReader::recv_data_or_connection () {
    fd_set fds_read;
    struct stat fd_stat;
    int max_sockfd = -1;
    int i, ret;

    FD_ZERO(&fds_read);
    /* original socket monitor connection */
    if (mSockFd >= 0) {
        FD_SET(mSockFd, &fds_read);
        max_sockfd = mSockFd;
    }

    /* connected clients */
    for(i = 0; i < MAX_CLIENT_NUM; i++) {
        if (mClients[i].fd > 0) {
            FD_SET(mClients[i].fd, &fds_read);
            max_sockfd = max(max_sockfd, mClients[i].fd);
        }
    }

    /* no Valide fd */
    if (max_sockfd < 0)
        return false;

    struct timeval timeout = {MONITOR_TIMEOUT, 0};
    ret = select(max_sockfd + 1, &fds_read, nullptr, nullptr, &timeout);
    if (ret == 0) {
        return true;
    }
    else if (ret < 0) {
        SACE_LOGE("%s Monitor Clients fail %s", getName(), strerror(errno));
        for (int i = 0; i < MAX_CLIENT_NUM; i++) {
            if (mClients[i].fd > 0 && fstat(mClients[i].fd, &fd_stat) < 0) {
                close(mClients[i].fd);
                mClients[i].fd = -1;
                handle_socket_close(mClients[i]);
            }
        }

        if (fstat(mSockFd, &fd_stat) < 0) {
            SACE_LOGE("%s Listen-Socket err=%s, reopen...", getName(), strerror(errno));
            setup_socket();
        }

        return true;
    }

    if (FD_ISSET(mSockFd, &fds_read)) {
        struct sockaddr addr;
        socklen_t slen = sizeof(addr);

        int client_fd = accept(mSockFd, &addr, &slen);
        if (client_fd < 0) {
            SACE_LOGE("%s Accept Client %d fail %s", getName(), mSockFd, strerror(errno));
            return true;
        }

        int i;
        for (int i = 0; i < MAX_CLIENT_NUM; i++) {
            if (mClients[i].fd < 0) {
                mClients[i].fd = client_fd;

                struct ucred cred;
                socklen_t len = sizeof(struct ucred);
                getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len);

                //record clients
                mClients[i].client = SaceClientIdentifier(cred.uid, cred.pid);
                mClients[i].writer = new SaceSocketWriter(NAME, cred.pid, client_fd);
                break;
            }
        }

        if (i >= MAX_CLIENT_NUM) {
            SACE_LOGW("%s Over MAX Client Number[%d], Reject...", getName(), MAX_CLIENT_NUM);
            close(client_fd);
        }
    }
    else {
        struct msghdr msg;
        struct iovec iov[1];
        char temp[MAX_SOCKET_BUF] = {0};

        iov[0].iov_base = temp;
        iov[0].iov_len  = MAX_SOCKET_BUF;

        msg.msg_name    = nullptr;
        msg.msg_namelen = 0;
        msg.msg_iov    = iov;
        msg.msg_iovlen = 1;
        msg.msg_control    = nullptr;
        msg.msg_controllen = 0;

        for (i = 0; i < MAX_CLIENT_NUM; i++) {
            int fd = mClients[i].fd;
            if (fd < 0 || !FD_ISSET(fd, &fds_read))
                continue;

            ret = TEMP_FAILURE_RETRY(read(fd, temp, MAX_SOCKET_BUF));
            if (ret <= 0) {
                if (ret < 0)
                    SACE_LOGE("%s Receive Incomming Command fail uid=%d, pid=%d, fd=%d : %s", getName(), mClients[i].client.uid, mClients[i].client.pid, fd, strerror(errno));
                else
                    SACE_LOGE("%s Close Socket uid=%d, pid=%d, fd=%d", getName(), mClients[i].client.uid, mClients[i].client.pid, fd);

                close(mClients[i].fd);
                mClients[i].fd = -1;

                handle_socket_close(mClients[i]);

                continue;
            }

            if ((size_t)ret < SaceCommandHeader::parcelSize()) {
                SACE_LOGE("%s - %d Invalide SaceCommandHeader. Size %d, Required %d", getName(), fd, ret, SaceCommandHeader::parcelSize());
                continue;
            }

            Parcel parcel;
            parcel.setData((uint8_t*)temp, ret);

            SaceCommandHeader header;
            header.readFromParcel(&parcel);
            if (parcel.dataSize() < header.len) {
                SACE_LOGE("%s - %d Invalide SaceCommand. Size %d, Required %d", getName(), fd, (uint32_t)parcel.dataSize(), header.len);
                continue;
            }

            sp<SaceCommand> saceCmd = new SaceCommand();
            parcel.setDataPosition(0);
            saceCmd->readFromParcel(&parcel);

            handle_socket_msg(mClients[i], saceCmd);
        }
    }

    return true;
}

void SaceSocketReader::close_socket () {
    for (int i = 0; i < MAX_CLIENT_NUM; i++) {
        if (mClients[i].fd > 0)
            close(mClients[i].fd);
        mClients[i].fd = -1;
    }
}

int SaceSocketReader::setup_socket () {
    int socket_id;

    socket_id = socket_local_server(mSockName.c_str(), ANDROID_SOCKET_NAMESPACE_ABSTRACT, mSockType);
    if (socket_id < 0) {
        SACE_LOGE("%s create socket %s fail %s", getName(), mSockName.c_str(), strerror(errno));
        return 1;
    }

    if (listen(socket_id, MAX_CLIENT_NUM) < 0) {
        SACE_LOGE("%s initialize socket client number fail %s", getName(), strerror(errno));
        return 1;
    }

    mSockFd = socket_id;
    return 0;
}

bool SaceSocketReader::startRead () {
    if (mSockFd < 0 && setup_socket()) {
        SACE_LOGE("%s setup_socket failed", getName());
        return false;
    }

    if (mThread == nullptr)
        mThread = new MonitorThread(this);

    if (mThread == nullptr) {
        close(mSockFd);
        SACE_LOGE("%s initialize MonitorThread failed", getName());
        return false;
    }

    mThread->run(THREAD_NAME);
    return true;
}

void SaceSocketReader::stopRead () {
    close(mSockFd);

    SACE_LOGI("%s Stoping... ", getName());
    for (int i = 0; i < MAX_CLIENT_NUM; i++)
        if (mClients[i].fd > 0) shutdown(mClients[i].fd, SHUT_WR);

    if (mThread != nullptr) {
        if (mThread->isRunning())
            mThread->requestExit();
    }
}

// ------------------------------------------------------------------
const char* SaceBinderReader::NAME = "SRBinder";

void SaceBinderReader::SaceManagerDeathRecipient::binderDied (const wp<IBinder>& who) {
    SACE_LOGW("%s client[%d:%d] close commands", NAME, client.uid, client.pid);
    service->destroyClient(client);
}

void SaceBinderReader::SaceManagerService::destroyClient (SaceClientIdentifier& client) {
    sp<SaceCommand> saceCmd = new SaceCommand();
    saceCmd->init();
    saceCmd->sequence = 0;
    saceCmd->normalCmdType = SACE_NORMAL_CMD_DESTROY;
    saceCmd->name = ::to_string(client.pid).append(":0");

    sp<SaceReaderMessage> saceMsg = new SaceReaderMessage;
    saceMsg->msgHandler = SACE_MESSAGE_HANDLER_NORMAL;
    saceMsg->msgCmd     = saceCmd;
    saceMsg->msgWriter  = nullptr;
    saceMsg->msgClient  = client;
    post(saceMsg);

    mLock.lock();
    map<SaceClientIdentifier, sp<ISaceListener>>::iterator it = mListener.find(client);
    if (it != mListener.end())
        mListener.erase(it);

    map<SaceClientIdentifier, sp<SaceManagerDeathRecipient>>::iterator its = mDeathReceipient.find(client);
    if (its != mDeathReceipient.end())
        mDeathReceipient.erase(its);
    mLock.unlock();
}

android::binder::Status SaceBinderReader::SaceManagerService::sendCommand (const SaceCommand& command, SaceResult* rslt) {
    if (mExit) {
        SACE_LOGI("%s handle command : %s Ignored For Exited", NAME, command.to_string().c_str());
        rslt = new SaceResult(resultByFailure());
        return android::binder::Status::ok();
    }
    else
        SACE_LOGI("%s handle command : %s", NAME, command.to_string().c_str());

    if (!secured_by_uid_pid(IPCThreadState::self()->getCallingUid(), IPCThreadState::self()->getCallingPid())) {
        rslt = new SaceResult(resultBySecure());
        return android::binder::Status::ok();
    }

    rslt = new SaceResult();
    SaceClientIdentifier client = SaceClientIdentifier(IPCThreadState::self()->getCallingUid(), IPCThreadState::self()->getCallingPid());

    map<SaceClientIdentifier, sp<ISaceListener>>::iterator it = mListener.find(client);
    sp<SaceBinderWriter> writer = new SaceBinderWriter(NAME, IPCThreadState::self()->getCallingPid(), it->second, rslt);

    sp<SaceReaderMessage> saceMsg = new SaceReaderMessage;
    saceMsg->msgHandler = typeCmdToMsg(command.type);
    saceMsg->msgCmd  = new SaceCommand(command);
    saceMsg->msgWriter = writer;
    saceMsg->msgClient = client;

    post(saceMsg);
    writer->waitResult();

    return android::binder::Status::ok();
}

android::binder::Status SaceBinderReader::SaceManagerService::unregisterListener () {
    if (mExit) {
        SACE_LOGI("SaceManagerService %d:%d unregisterListener Ignored For Exited", IPCThreadState::self()->getCallingUid(),
            IPCThreadState::self()->getCallingPid());
        return android::binder::Status::ok();
    }
    else
        SACE_LOGI("SaceManagerService %d:%d unregisterListener", IPCThreadState::self()->getCallingUid(), IPCThreadState::self()->getCallingPid());

    SaceClientIdentifier client = SaceClientIdentifier(IPCThreadState::self()->getCallingUid(), IPCThreadState::self()->getCallingPid());

    mLock.lock();
    map<SaceClientIdentifier, sp<ISaceListener>>::iterator it = mListener.find(client);
    if (it != mListener.end()) {
        IInterface::asBinder(it->second)->linkToDeath(nullptr);
        mListener.erase(it);
    }

    map<SaceClientIdentifier, sp<SaceManagerDeathRecipient>>::iterator its = mDeathReceipient.find(client);
    if (its != mDeathReceipient.end())
        mDeathReceipient.erase(its);
    mLock.unlock();

    return android::binder::Status::ok();
}

android::binder::Status SaceBinderReader::SaceManagerService::registerListener (const sp<ISaceListener>& listener) {
    if (mExit) {
        SACE_LOGI("SaceManagerService %d:%d registerListener Ignored For Exited", IPCThreadState::self()->getCallingUid(),
            IPCThreadState::self()->getCallingPid());
        return android::binder::Status::ok();
    }
    else
        SACE_LOGI("SaceManagerService %d:%d registerListener", IPCThreadState::self()->getCallingUid(), IPCThreadState::self()->getCallingPid());

    SaceClientIdentifier client = SaceClientIdentifier(IPCThreadState::self()->getCallingUid(), IPCThreadState::self()->getCallingPid());
    sp<SaceManagerDeathRecipient> deathReceipient = new SaceManagerDeathRecipient(client, this);
    IInterface::asBinder(listener)->linkToDeath(deathReceipient);

    mLock.lock();
    mDeathReceipient.insert(pair<SaceClientIdentifier, sp<SaceManagerDeathRecipient>>(client, deathReceipient));
    mListener.insert(pair<SaceClientIdentifier, sp<ISaceListener>>(client, listener));
    mLock.unlock();

    return android::binder::Status::ok();
}

bool SaceBinderReader::startRead () {
    SACE_LOGI("%s Starting %d:%d", getName(), getpid(), gettid());
    defaultServiceManager()->addService(String16("SaceService"), mSaceManager);
    ProcessState::self()->startThreadPool();
    return true;
}

void SaceBinderReader::stopRead () {
    SACE_LOGI("%s Stoping... ", getName());
    mSaceManager->stop();
}

}; //namespace android
