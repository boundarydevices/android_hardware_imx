/*
 * Copyright 2017 NXP.
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

#include <ctype.h>
#include <dirent.h>
#include <cutils/log.h>

#include "DisplayManager.h"

namespace fsl {

#define HDMI_PLUG_EVENT "hdmi_video"
#define HDMI_PLUG_CHANGE "change@"
#define HDMI_SII902_PLUG_EVENT "change@/devices/platform/sii902x.0"
#define HDMI_EXTCON "extcon"

DisplayManager* DisplayManager::sInstance(0);
Mutex DisplayManager::sLock(Mutex::PRIVATE);

DisplayManager* DisplayManager::getInstance()
{
    Mutex::Autolock _l(sLock);
    if (sInstance != NULL) {
        return sInstance;
    }

    sInstance = new DisplayManager();
    return sInstance;
}

DisplayManager::DisplayManager()
{
    for (int i=0; i<MAX_PHYSICAL_DISPLAY; i++) {
        mFbDisplays[i] = new FbDisplay();
        mFbDisplays[i]->setIndex(i);
    }

    for (int i=0; i<MAX_VIRTUAL_DISPLAY; i++) {
        mVirtualDisplays[i] = new VirtualDisplay();
        mVirtualDisplays[i]->setIndex(i+MAX_PHYSICAL_DISPLAY);
    }

    // now only main display vsync valid.
    mFbDisplays[DISPLAY_PRIMARY]->enableVsync();

    enumDisplays();

    //allow primary display plug-out then plug-in.
    if (mFbDisplays[DISPLAY_PRIMARY]->connected() == false) {
        mFbDisplays[DISPLAY_PRIMARY]->setConnected(true);
        mFbDisplays[DISPLAY_PRIMARY]->setFakeVSync(true);
    }

    mHotplugThread = new HotplugThread(this);
}

Display* DisplayManager::getDisplay(int id)
{
    Display* pDisplay = NULL;
    Mutex::Autolock _l(mLock);
    if (id >= 0 && id < MAX_PHYSICAL_DISPLAY) {
        pDisplay = (Display*)mFbDisplays[id];
    }
    else if (id >= MAX_PHYSICAL_DISPLAY &&
             id < MAX_PHYSICAL_DISPLAY + MAX_VIRTUAL_DISPLAY) {
        pDisplay = (Display*)mVirtualDisplays[id-MAX_PHYSICAL_DISPLAY];
    }
    else {
        ALOGE("%s invalid display id:%d", __func__, id);
    }

    return pDisplay;
}

FbDisplay* DisplayManager::getFbDisplay(int id)
{
    Mutex::Autolock _l(mLock);
    if (id < 0 || id >= MAX_PHYSICAL_DISPLAY) {
        ALOGE("%s invalid id %d", __func__, __LINE__);
        return NULL;
    }

    return mFbDisplays[id];
}

VirtualDisplay* DisplayManager::getVirtualDisplay(int id)
{
    Mutex::Autolock _l(mLock);
    if (id < MAX_PHYSICAL_DISPLAY ||
        id >= MAX_PHYSICAL_DISPLAY + MAX_VIRTUAL_DISPLAY) {
        ALOGE("%s invalid id %d", __func__, id);
        return NULL;
    }

    mVirtualDisplays[id]->setConnected(true);
    return mVirtualDisplays[id];
}

VirtualDisplay* DisplayManager::createVirtualDisplay()
{
    VirtualDisplay* display = NULL;
    for (int i=0; i<MAX_VIRTUAL_DISPLAY; i++) {
        {
            Mutex::Autolock _l(mLock);
            display = mVirtualDisplays[i];
        }
        if (!display->busy()) {
            display->setBusy(true);
            display->setConnected(true);
            return display;
        }
    }

    return NULL;
}

int DisplayManager::destroyVirtualDisplay(int id)
{
    Mutex::Autolock _l(mLock);
    if (id < MAX_PHYSICAL_DISPLAY ||
        id >= MAX_PHYSICAL_DISPLAY + MAX_VIRTUAL_DISPLAY) {
        ALOGE("%s invalid id %d", __func__, id);
        return -EINVAL;
    }

    mVirtualDisplays[id]->setConnected(false);
    mVirtualDisplays[id]->reset();
    mVirtualDisplays[id]->clearConfigs();
    mVirtualDisplays[id]->setBusy(false);
    return 0;
}

void DisplayManager::setCallback(EventListener* callback)
{
    FbDisplay* display = NULL;
    {
        Mutex::Autolock _l(mLock);
        display = mFbDisplays[DISPLAY_PRIMARY];
        mListener = callback;
    }
    display->setCallback(callback);
}

bool DisplayManager::isOverlay(int fb)
{
    char fb_path[HWC_PATH_LENGTH];
    char value[HWC_STRING_LENGTH];
    FILE *fp = NULL;

    snprintf(fb_path, HWC_PATH_LENGTH, HWC_FB_SYS"%d/name", fb);
    if (!(fp = fopen(fb_path, "r"))) {
        ALOGW("open %s failed", fb_path);
        return false;
    }

    memset(value, 0, sizeof(value));
    if (!fgets(value, sizeof(value), fp)) {
        ALOGE("Unable to read %s", fb_path);
        fclose(fp);
        return false;
    }
    if (strstr(value, "FG")) {
        ALOGI("fb%d is overlay device", fb);
        fclose(fp);
        return true;
    }

    fclose(fp);
    return false;
}

int DisplayManager::enumDisplays()
{
    DIR *dir = NULL;
    struct dirent *dirEntry;
    char fb_path[HWC_PATH_LENGTH];
    char tmp[HWC_PATH_LENGTH];
    char value[HWC_STRING_LENGTH];
    FILE *fp;
    int id = 1;
    int fb = -1;
    int ret = 0;

    dir = opendir(SYS_GRAPHICS);
    if (dir == NULL) {
        ALOGE("%s open %s failed", __func__, SYS_GRAPHICS);
        return -EINVAL;
    }

    while ((dirEntry = readdir(dir)) != NULL) {
        fb = -1;
        if (strncmp(dirEntry->d_name, "fb", 2) ||
            strlen(dirEntry->d_name) < 3 ||
            !isdigit(*(dirEntry->d_name + 2))) {
            continue;
        }
        fb = atoi(dirEntry->d_name + 2);
        ALOGI("entry:%s get fb%d", dirEntry->d_name, fb);
        if (fb < 0) {
            continue;
        }

        memset(fb_path, 0, sizeof(fb_path));
        snprintf(fb_path, HWC_PATH_LENGTH, SYS_GRAPHICS"/%s", dirEntry->d_name);
        //check the fb device exist.
        if (!(fp = fopen(fb_path, "r"))) {
            ALOGW("open %s failed", fb_path);
            continue;
        }
        fclose(fp);

        if (isOverlay(fb)) {
            continue;
        }

        if (id >= MAX_PHYSICAL_DISPLAY) {
            ALOGW("display id:%d exceed max number:%d", id, MAX_PHYSICAL_DISPLAY);
            closedir(dir);
            return 0;
        }

        FbDisplay *display = NULL;
        if (fb == 0) {
            display = mFbDisplays[0];
        }
        else {
            display = mFbDisplays[id];
            id++;
        }
        display->setFb(fb);
        display->readType();
        display->readConnection();
        if (display->connected()) {
            display->openFb();
        }
    }
    closedir(dir);

    return 0;
}

void DisplayManager::handleHotplugEvent()
{
    for (uint32_t i=0; i<MAX_PHYSICAL_DISPLAY; i++) {
        FbDisplay* display = NULL;
        {
            Mutex::Autolock _l(mLock);
            display = mFbDisplays[i];
        }
        bool connected = display->connected();
        display->readConnection();
        if (display->connected() == connected) {
            continue;
        }

        // primary display.
        if (i == DISPLAY_PRIMARY) {
            display->setFakeVSync(!display->connected());
            continue;
        }

        if (display->connected()) {
            display->openFb();
        }

        EventListener* callback = NULL;
        {
            Mutex::Autolock _l(mLock);
            callback = mListener;
        }
        if (callback != NULL) {
            callback->onHotplug(i, display->connected());
        }

        if (!display->connected()) {
            display->closeFb();
        }
    }
}

//------------------------------------------------------------
DisplayManager::HotplugThread::HotplugThread(DisplayManager *ctx)
   : Thread(false), mCtx(ctx)
{
}

void DisplayManager::HotplugThread::onFirstRef()
{
    run("HWC-UEvent-Thread", android::PRIORITY_URGENT_DISPLAY);
}

int32_t DisplayManager::HotplugThread::readyToRun()
{
    uevent_init();
    return 0;
}

bool DisplayManager::HotplugThread::threadLoop()
{
    char uevent_desc[4096];
    const char *pSii902 = HDMI_SII902_PLUG_EVENT;

    memset(uevent_desc, 0, sizeof(uevent_desc));
    int len = uevent_next_event(uevent_desc, sizeof(uevent_desc) - 2);
    int type = -1;
    if (strstr(uevent_desc, HDMI_PLUG_EVENT) != NULL &&
         strstr(uevent_desc, HDMI_PLUG_CHANGE) != NULL &&
         strstr(uevent_desc, HDMI_EXTCON) == NULL) {
        type = DISPLAY_HDMI;
    }
    else if (!strncmp(uevent_desc, pSii902, strlen(pSii902))) {
        type = DISPLAY_HDMI_ON_BOARD;
    }
    else {
        ALOGV("%s invalid uevent %s", __func__, uevent_desc);
        return true;
    }

    mCtx->handleHotplugEvent();

    return true;
}

}
