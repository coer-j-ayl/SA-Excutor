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

package com.android.sace;

public class SaceService {
    private static final String TAG = "SaceService";
    private long mNativePtr;

    /* MUST BE SAME AS SaceServiceInfo.h */
    public static final int STATE_PAUSED = 0;             /* Service Stoped By User */
    public static final int STATE_STOPED = 1;             /* Service Paused By User */
    public static final int STATE_RUNNING = 2;            /* Service Running */
    public static final int STATE_DIED = 3;               /* Service Exit By Self Error */
    public static final int STATE_DIED_SIGNAL = 4;        /* Service Exit By SIGNAL */
    public static final int STATE_DIED_UNKNOWN = 5;       /* Service Exit By Unknown */
    public static final int STATE_FINISHED = 6;           /* Service Finished By Itself */
    public static final int STATE_FINISHING_USER = 7;     /* Service Finishing By User */
    public static final int STATE_FINISHED_USER = 8;      /* Service Finished By User */
    public static final int STATE_UNKNOWN = 9;

    public SaceService (long ptr) {
        mNativePtr = ptr;
    }

    public boolean stop () {
        return nStop(mNativePtr);
    }

    public boolean pause () {
        return nPause(mNativePtr);
    }

    public boolean restart () {
        return nRestart(mNativePtr);
    }

    public String getName () {
        return nGetName(mNativePtr);
    }

    public String getCmd () {
        return nGetCmd(mNativePtr);
    }

    public int getState () {
        return nGetState(mNativePtr);
    }

    public int getError () {
        return nGetError(mNativePtr);
    }

    @Override
    public String toString () {
        return "<name=" + getName() + ",state=" + getState() + ",error=" + getError() + ">";
    }

    @Override
    protected void finalize () throws Throwable {
        try {
            nDestroy(mNativePtr);
        }
        finally {
            super.finalize();
        }
    }

    /* Native Method */
    private native boolean nStop (long ptr);
    private native boolean nPause (long ptr);
    private native boolean nRestart (long ptr);
    private native String nGetName (long ptr);
    private native String nGetCmd (long ptr);
    private native int nGetState (long ptr);
    private native int nGetError (long ptr);
    private native void nDestroy (long ptr);
}
