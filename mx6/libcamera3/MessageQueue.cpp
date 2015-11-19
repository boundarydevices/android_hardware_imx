/*
 * Copyright (C) 2009-2015 Freescale Semiconductor, Inc.
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

#include "MessageQueue.h"

using namespace android;

void CMessageList::insert(const sp<CMessage>& node)
{
    mList.push_back(node);
}

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
    mCommands.clear();
}

CMessageQueue::~CMessageQueue()
{
    Mutex::Autolock _l(mLock);

    mMessages.clear();
    mCommands.clear();
}
void CMessageQueue::clearMessages()
{
    Mutex::Autolock _l(mLock);

    mMessages.clear();
}

void CMessageQueue::clearCommands()
{
    Mutex::Autolock _l(mLock);

    mCommands.clear();
}

sp<CMessage> CMessageQueue::waitMessage(nsecs_t timeout)
{
    sp<CMessage>    result;
    nsecs_t timeoutTime = systemTime() + timeout;
    while (true) {
        Mutex::Autolock _l(mLock);
        nsecs_t now = systemTime();

        // handle command firstly.
        LIST::iterator cur(mCommands.begin());
        if (cur != mCommands.end()) {
            result = *cur;
        }

        if (result != 0) {
            mCommands.remove(cur);
            break;
        }

        // handle message secondly.
        cur = mMessages.begin();
        if (cur != mMessages.end()) {
            result = *cur;
        }

        if (result != 0) {
            mMessages.remove(cur);
            break;
        }

        if (timeout >= 0) {
            if (timeoutTime < now) {
                result = 0;
                break;
            }
            nsecs_t relTime = timeoutTime - systemTime();
            mCondition.waitRelative(mLock, relTime);
        } else {
            mCondition.wait(mLock);
        }
    }

    return result;
}

status_t CMessageQueue::postMessage(const sp<CMessage> message,
                                    int32_t             flags)
{
    return queueMessage(message, flags);
}

status_t CMessageQueue::queueMessage(const sp<CMessage>& message,
                                     int32_t             flags)
{
    Mutex::Autolock _l(mLock);

    if (flags == 0) {
        mMessages.insert(message);
    }
    else {
        mCommands.insert(message);
    }

    mCondition.signal();
    return NO_ERROR;
}

