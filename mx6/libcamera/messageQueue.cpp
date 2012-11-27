/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2009-2012 Freescale Semiconductor, Inc.
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


#include <stdint.h>
#include <errno.h>
#include <sys/types.h>

#include <utils/threads.h>
#include <utils/Timers.h>
#include <utils/Log.h>
#include <binder/IPCThreadState.h>

#include "messageQueue.h"

namespace android {

void CMessageList::insert(const sp<CMessage>& node)
{
    mList.push_back(node);
};

void CMessageList::remove(CMessageList::LIST::iterator pos)
{
    mList.erase(pos);
}

void CMessageList::clear()
{
    mList.clear();
}

CMessageQueue::CMessageQueue()
{
    Mutex::Autolock _l(mLock);
    mMessages.clear();
}

CMessageQueue::~CMessageQueue()
{
    Mutex::Autolock _l(mLock);
    mMessages.clear();
}

sp<CMessage> CMessageQueue::waitMessage(nsecs_t timeout)
{
    sp<CMessage> result;
    sp<SyncMessage> syncResult;
    nsecs_t timeoutTime = systemTime() + timeout;
    while(true) {
        Mutex::Autolock _l(mLock);
        nsecs_t now = systemTime();
        //handle sync message firstly.
        LIST::iterator scur(mSyncMessages.begin());
        if(scur != mSyncMessages.end()) {
            syncResult = (SyncMessage*)(*scur).get();
        }

        if(syncResult != 0) {
            result = (CMessage*)syncResult.get();
            mSyncMessages.remove(scur);
            break;
        }

        //handle sync message secondly.
        LIST::iterator cur(mMessages.begin());
        if(cur != mMessages.end()) {
            result = *cur;
        }

        if(result != 0) {
            mMessages.remove(cur);
            break;
        }

        if(timeout >= 0) {
            if(timeoutTime < now) {
                result = 0;
                break;
            }
            nsecs_t relTime = timeoutTime - systemTime();
            mCondition.waitRelative(mLock, relTime);
        }else {
            mCondition.wait(mLock);
        }
    }

    if(syncResult != NULL) {
        syncResult->notify();
    }

    return result;
}

status_t CMessageQueue::postMessage(const sp<CMessage>& message, int32_t flags)
{
    return queueMessage(message, flags);
}

status_t CMessageQueue::postSyncMessage(const sp<SyncMessage>& message, int32_t flags)
{
    status_t res = queueSyncMessage(message, flags);
    if (res == NO_ERROR) {
        message->wait();
    }
    return res;
}

status_t CMessageQueue::queueMessage(const sp<CMessage>& message, int32_t flags)
{
    Mutex::Autolock _l(mLock);
    mMessages.insert(message);
    mCondition.signal();
    return NO_ERROR;
}

status_t CMessageQueue::queueSyncMessage(const sp<SyncMessage>& message, int32_t flags)
{
    Mutex::Autolock _l(mLock);
    mSyncMessages.insert(message.get());
    mCondition.signal();
    return NO_ERROR;
}
};

