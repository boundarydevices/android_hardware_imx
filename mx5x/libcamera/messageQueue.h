/*
 * Copyright (C) 2008 The Android Open Source Project
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

/*
 * Copyright 2009-2012 Freescale Semiconductor, Inc. All Rights Reserved.
 */


#ifndef CAMERA_HAL_MESSAGE_QUEUE_H
#define CAMERA_HAL_MESSAGE_QUEUE_H

#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <utils/threads.h>
#include <utils/Timers.h>
#include <utils/List.h>

//#include "Barrier.h"

namespace android {

typedef enum{
    CMESSAGE_TYPE_NORMAL = 0,
    CMESSAGE_TYPE_STOP = -1,
    CMESSAGE_TYPE_QUITE = -2,
}CMESSAGE_TYPE;

class CMessage;

class CMessageList
{
    List< sp<CMessage> > mList;
    typedef List< sp<CMessage> > LIST;
public:
    inline LIST::iterator begin() {return mList.begin();}
    inline LIST::const_iterator begin() const {return mList.begin();}
    inline LIST::iterator end() {return mList.end();}
    inline LIST::const_iterator end() const {return mList.end();}
    inline bool isEmpty() const {return mList.empty();}
    void insert(const sp<CMessage> &node);
    void remove(LIST::iterator pos);
    void clear();
};

class CMessage : public LightRefBase<CMessage>
{
public:
    CMESSAGE_TYPE what;
    int32_t arg0;

    //CMessage(): what(0), arg0(0) {}
    CMessage(CMESSAGE_TYPE what, int32_t arg0=0)
        : what(what), arg0(arg0) {}

//protected:
    virtual ~CMessage() {}

private:
    friend class LightRefBase<CMessage>;
};

class CMessageQueue
{
    typedef List< sp<CMessage> > LIST;
public:
    CMessageQueue();
    ~CMessageQueue();

    sp<CMessage> waitMessage(nsecs_t timeout=-1);
    status_t postMessage(const sp<CMessage>& message, int32_t flags=0);
    status_t postQuitMessage();
    status_t postStopMessage();
    void clearMessage();

private:
    status_t queueMessage(const sp<CMessage>& message, int32_t flags);

    Mutex mLock;
    Condition mCondition;
    CMessageList mMessages;
    bool mQuit;
    bool mStop;
    sp<CMessage> mQuitMessage;
    sp<CMessage> mStopMessage;
};


};

#endif
