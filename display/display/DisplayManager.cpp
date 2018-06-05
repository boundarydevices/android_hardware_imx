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
#include <cutils/properties.h>

#include "FbDisplay.h"
#include "KmsDisplay.h"
#include "DisplayManager.h"

namespace fsl {

#define HDMI_PLUG_EVENT "hdmi_video"
#define HDMI_PLUG_CHANGE "change@"
#define HDMI_SII902_PLUG_EVENT "change@/devices/platform/sii902x.0"
#define HDMI_EXTCON "extcon"
#define HDMI_DRM_EVENT "change@/devices/platform/display-subsystem/drm"

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

    for (int i=0; i<MAX_PHYSICAL_DISPLAY; i++) {
        mKmsDisplays[i] = new KmsDisplay();
        mKmsDisplays[i]->setIndex(i);
    }

    for (int i=0; i<MAX_VIRTUAL_DISPLAY; i++) {
        mVirtualDisplays[i] = new VirtualDisplay();
        mVirtualDisplays[i]->setIndex(i+MAX_PHYSICAL_DISPLAY);
    }

    mDrmMode = false;
    enumKmsDisplays();
    if (!mDrmMode) {
        enumFbDisplays();
    }

    // now only main display vsync valid.
    if (mDrmMode) {
        mKmsDisplays[DISPLAY_PRIMARY]->enableVsync();
    }
    else {
        mFbDisplays[DISPLAY_PRIMARY]->enableVsync();
    }

    //allow primary display plug-out then plug-in.
    Display* display = getPhysicalDisplay(DISPLAY_PRIMARY);
    if (display->connected() == false) {
        display->setFakeVSync(true);
    }

    mHotplugThread = new HotplugThread(this);
}

DisplayManager::~DisplayManager()
{
    if (mHotplugThread != NULL) {
        mHotplugThread->requestExit();
    }

    Display* display = getPhysicalDisplay(DISPLAY_PRIMARY);
    display->setVsyncEnabled(false);

    for (int i=0; i<MAX_PHYSICAL_DISPLAY; i++) {
        if (mFbDisplays[i] != NULL) {
            delete mFbDisplays[i];
        }
    }

    for (int i=0; i<MAX_PHYSICAL_DISPLAY; i++) {
        if (mKmsDisplays[i] != NULL) {
            delete mKmsDisplays[i];
        }
    }

    for (int i=0; i<MAX_VIRTUAL_DISPLAY; i++) {
        if (mVirtualDisplays[i] != NULL) {
            delete mVirtualDisplays[i];
        }
    }
}

Display* DisplayManager::getDisplay(int id)
{
    Display* pDisplay = NULL;
    Mutex::Autolock _l(mLock);
    if (id >= 0 && id < MAX_PHYSICAL_DISPLAY) {
        if (mDrmMode) {
            pDisplay = (Display*)mKmsDisplays[id];
        }
        else {
            pDisplay = (Display*)mFbDisplays[id];
        }
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

Display* DisplayManager::getPhysicalDisplay(int id)
{
    Mutex::Autolock _l(mLock);
    if (id < 0 || id >= MAX_PHYSICAL_DISPLAY) {
        ALOGE("%s invalid id %d", __func__, __LINE__);
        return NULL;
    }

    if (mDrmMode) {
        return mKmsDisplays[id];
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

    mVirtualDisplays[id-MAX_PHYSICAL_DISPLAY]->setConnected(true);
    return mVirtualDisplays[id-MAX_PHYSICAL_DISPLAY];
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

    mVirtualDisplays[id-MAX_PHYSICAL_DISPLAY]->setConnected(false);
    mVirtualDisplays[id-MAX_PHYSICAL_DISPLAY]->reset();
    mVirtualDisplays[id-MAX_PHYSICAL_DISPLAY]->clearConfigs();
    mVirtualDisplays[id-MAX_PHYSICAL_DISPLAY]->setBusy(false);
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

int DisplayManager::enumKmsDisplay(const char *path, int *id, bool *foundPrimary)
{
    int drmFd = open(path, O_RDWR);
    if(drmFd < 0) {
        ALOGE("Failed to open dri-%s, error:%s", path, strerror(-errno));
        return -ENODEV;
    }

    int ret = drmSetClientCap(drmFd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    if (ret) {
        ALOGE("Failed to set universal plane cap %d", ret);
        close(drmFd);
        return ret;
    }

    ret = drmSetClientCap(drmFd, DRM_CLIENT_CAP_ATOMIC, 1);
    if (ret) {
        ALOGE("Failed to set atomic cap %d", ret);
        close(drmFd);
        return ret;
    }

    // get primary display name to match DRM.
    // the primary display can be fixed by name.
    int main = 0;
    char value[PROPERTY_VALUE_MAX];
    int len = property_get("ro.boot.primary_display", value, NULL);
    if (len > 0) {
        drmVersionPtr version = drmGetVersion(drmFd);
        if (version && !strncmp(version->name, value, len)) {
            main = 1;
        }
        drmFreeVersion(version);
    }

    drmModeResPtr res = drmModeGetResources(drmFd);
    if (!res) {
        ALOGE("Failed to get DrmResources resources");
        close(drmFd);
        return -ENODEV;
    }

    KmsDisplay* display = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        display = mKmsDisplays[*id];

        if (display->setDrm(drmFd, res->connectors[i]) != 0) {
            continue;
        }

        display->readType();
        display->readConnection();
        // primary display allow not connected.
        if (!display->connected() && (*foundPrimary || !main)) {
            (*id)++;
            continue;
        }

        if (display->openKms() != 0) {
            display->closeKms();
            (*id)++;
            continue;
        }

        // primary display is fixed by name.
        if (!*foundPrimary && main) {
            ALOGI("%s set %d as primary display", __func__, (*id));
            *foundPrimary = true;
            KmsDisplay* tmp = mKmsDisplays[0];
            mKmsDisplays[0] = mKmsDisplays[*id];
            mKmsDisplays[*id] = tmp;
            mKmsDisplays[0]->setIndex(0);
            mKmsDisplays[*id]->setIndex(*id);
        }
        else {
            (*id)++;
        }
    }
    drmModeFreeResources(res);
    close(drmFd);

    return 0;
}

int DisplayManager::enumKmsDisplays()
{
    struct dirent **dirEntry;
    char path[HWC_PATH_LENGTH];
    int ret = 0;
    char dri[PROPERTY_VALUE_MAX];
    int id = 1;
    bool foundPrimary = false;
    property_get("hwc.drm.device", dri, "/dev/dri");
    int count = -1;

    count = scandir(dri, &dirEntry, 0, alphasort);
    if(count < 0) {
        ALOGE("%s open %s failed", __func__, dri);
        return -EINVAL;
    }
    for(int i=0; i<count; i++) {
        if (strncmp(dirEntry[i]->d_name, "card", 4)) {
            free(dirEntry[i]);
            continue;
        }
        memset(path, 0, sizeof(path));
        snprintf(path, HWC_PATH_LENGTH, "/dev/dri/%s", dirEntry[i]->d_name);
        ALOGI("try dev:%s", path);
        enumKmsDisplay(path, &id, &foundPrimary);
        free(dirEntry[i]);
    }
    free(dirEntry);

    if (foundPrimary) {
        mDrmMode = true;
        ret = 0;
    }
    else {
        ret = -ENODEV;
    }

    return ret;
}

int DisplayManager::enumFbDisplays()
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
        if (fb == 0 || display->connected()) {
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

void DisplayManager::handleKmsHotplug()
{
    for (uint32_t i=0; i<MAX_PHYSICAL_DISPLAY; i++) {
        KmsDisplay* display = NULL;
        {
            Mutex::Autolock _l(mLock);
            display = mKmsDisplays[i];
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
            display->openKms();
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
            display->closeKms();
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

    bool kms = false;
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
    else if (strstr(uevent_desc, "DEVTYPE=drm_minor") &&
             strstr(uevent_desc, "HOTPLUG=1")) {
        kms = true;
    }
    else if (strstr(uevent_desc, HDMI_DRM_EVENT)) {
        ALOGV("%s kms uevent %s", __func__, uevent_desc);
        kms = true;
    }
    else {
        ALOGV("%s invalid uevent %s", __func__, uevent_desc);
        return true;
    }

    if (kms) {
        mCtx->handleKmsHotplug();
    }
    else {
        mCtx->handleHotplugEvent();
    }

    return true;
}

}
