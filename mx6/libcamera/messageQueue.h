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


#ifndef CAMERA_HAL_MESSAGE_QUEUE_H
#define CAMERA_HAL_MESSAGE_QUEUE_H

#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <utils/threads.h>
#include <utils/Timers.h>
#include <utils/List.h>
#include <semaphore.h>

namespace android {
class CMessage;

class CMessageList {
    List< sp<CMessage> > mList;
    typedef List< sp<CMessage> > LIST;

public:
    inline LIST::iterator begin() {
        return mList.begin();
    }

    inline LIST::const_iterator begin() const {
        return mList.begin();
    }

    inline LIST::iterator end() {
        return mList.end();
    }

    inline LIST::const_iterator end() const {
        return mList.end();
    }

    inline bool isEmpty() const {
        return mList.empty();
    }

    void insert(const sp<CMessage>& node);
    void remove(LIST::iterator pos);
    void clear();
};

class CMessage : public LightRefBase<CMessage>{
public:
    int32_t what;
    int32_t arg0;

    CMessage(int32_t what,
             int32_t arg0 = 0)
        : what(what), arg0(arg0) {}

    virtual ~CMessage() {}

private:
    friend class LightRefBase<CMessage>;
};

class SyncMessage : public CMessage {
public:
    SyncMessage(int32_t what,
                int32_t arg0 = 0)
        : CMessage(what, arg0)
    {
        sem_init(&mSem, 0, 0);
    }

    void wait()
    {
        sem_wait(&mSem);
    }

    void notify()
    {
        sem_post(&mSem);
    }

    ~SyncMessage()
    {
        sem_destroy(&mSem);
    }

private:
    sem_t mSem;
};

class CMessageQueue {
    typedef List< sp<CMessage> > LIST;

public:
    CMessageQueue();
    ~CMessageQueue();

    sp<CMessage> waitMessage(nsecs_t timeout = -1);
    status_t     postMessage(const sp<CMessage>& message,
                             int32_t             flags = 0);
    status_t     postSyncMessage(const sp<SyncMessage>& message,
                                 int32_t                flags = 0);

private:
    status_t queueMessage(const sp<CMessage>& message,
                          int32_t             flags);
    status_t queueSyncMessage(const sp<SyncMessage>& message,
                              int32_t                flags);

    Mutex mLock;
    Condition mCondition;
    CMessageList mMessages;
    CMessageList mSyncMessages;
};
};

#endif // ifndef CAMERA_HAL_MESSAGE_QUEUE_H
