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

#include <fcntl.h>
#include <sys/ioctl.h>
#include <cutils/log.h>
#include <sync/sync.h>

#include <linux/fb.h>
#include <linux/mxcfb.h>

#include "Memory.h"
#include "MemoryManager.h"
#include "FbDisplay.h"

namespace fsl {

#define VSYNC_STRING_LEN 128

FbDisplay::FbDisplay()
{
    mFb = -1;
    mFd = -1;
    mVsyncThread = NULL;
    mOpened = false;
    mTargetIndex = 0;
    memset(&mTargets[0], 0, sizeof(mTargets));
}

FbDisplay::~FbDisplay()
{
    sp<VSyncThread> vsync = NULL;
    {
        Mutex::Autolock _l(mLock);
        vsync = mVsyncThread;
    }

    if (vsync != NULL) {
        vsync->requestExit();
    }

    closeFb();
}

int FbDisplay::setPowerMode(int mode)
{
    Mutex::Autolock _l(mLock);

    mPowerMode = mode;
    //HDMI need to keep unblank since audio need to be able to output
    //through HDMI cable. Blank the HDMI will lost the HDMI clock
    if (mType == DISPLAY_HDMI) {
        return 0;
    }

    int err = ioctl(mFd, FBIOBLANK, mPowerMode);
    if (err < 0) {
        ALOGE("blank ioctl failed");
        return -errno;
    }

    return err;
}

void FbDisplay::enableVsync()
{
    Mutex::Autolock _l(mLock);
    mVsyncThread = new VSyncThread(this);
}

void FbDisplay::setCallback(EventListener* callback)
{
    Mutex::Autolock _l(mLock);
    mListener = callback;
}

void FbDisplay::setVsyncEnabled(bool enabled)
{
    sp<VSyncThread> vsync = NULL;
    {
        Mutex::Autolock _l(mLock);
        vsync = mVsyncThread;
    }

    if (vsync != NULL) {
        vsync->setEnabled(enabled);
    }
}

void FbDisplay::setFakeVSync(bool enable)
{
    sp<VSyncThread> vsync = NULL;
    {
        Mutex::Autolock _l(mLock);
        vsync = mVsyncThread;
    }

    if (vsync != NULL) {
        vsync->setFakeVSync(enable);
    }
}

int FbDisplay::updateScreen()
{
    Mutex::Autolock _l(mLock);

    if (!mConnected) {
        ALOGE("updateScreen display plugout");
        return -EINVAL;
    }

    if (mPowerMode != POWER_ON) {
        ALOGE("can't update screen power mode:%d", mPowerMode);
        return -EINVAL;
    }

    Memory* buffer = mRenderTarget;
    if (!buffer || !buffer->flags & FLAGS_FRAMEBUFFER) {
        ALOGV("%s buffer is not framebuffer", __func__);
        return -EINVAL;
    }

    const DisplayConfig& config = mConfigs[mActiveConfig];
    if (buffer->width != config.mXres || buffer->height != config.mYres ||
        buffer->fslFormat != config.mFormat) {
        ALOGE("%s buffer not match: w:%d, h:%d, f:%d, xres:%d, yres:%d, mf:%d",
              __func__, buffer->width, buffer->height, buffer->fslFormat,
              config.mXres, config.mYres, config.mFormat);
        return -EINVAL;
    }

    struct mxcfb_buffer mxcbuf;
    mxcbuf.xoffset = mxcbuf.yoffset = 0;
    mxcbuf.stride = config.mStride;
    mxcbuf.phys = buffer->phys;

    if (ioctl(mFd, MXCFB_UPDATE_SCREEN, &mxcbuf) == -1) {
        ALOGW("MXCFB_UPDATE_SCREEN failed: %s", strerror(errno));
        return 0;
    }

    return 0;
}

int FbDisplay::readConfigLocked()
{
    struct fb_var_screeninfo info;
    if (ioctl(mFd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("<%s,%d> FBIOGET_VSCREENINFO failed", __func__, __LINE__);
        close(mFd);
        return -errno;
    }

    struct fb_fix_screeninfo finfo;
    if (ioctl(mFd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        ALOGE("<%s,%d> FBIOGET_FSCREENINFO failed", __func__, __LINE__);
        close(mFd);
        return -errno;
    }

    int refreshRate = 1000000000000000LLU /
    (
            uint64_t(info.upper_margin + info.lower_margin + info.yres + info.vsync_len)
            * (info.left_margin  + info.right_margin + info.xres + info.hsync_len)
            * info.pixclock
    );

    if (refreshRate == 0) {
        // bleagh, bad info from the driver
        refreshRate = 60000;  // 60 Hz
    }

    if (int(info.width) <= 0 || int(info.height) <= 0) {
        // the driver doesn't return that information
        // default to 160 dpi
        info.width  = ((info.xres * 25.4f)/160.0f + 0.5f);
        info.height = ((info.yres * 25.4f)/160.0f + 0.5f);
    }

    ssize_t configId = getConfigIdLocked(info.xres, info.yres);
    if (configId < 0) {
        ALOGE("can't find config: w:%d, h:%d", info.xres, info.yres);
        return -1;
    }

    DisplayConfig& config = mConfigs.editItemAt(configId);
    config.mXdpi = 1000 * (info.xres * 25.4f) / info.width;
    config.mYdpi = 1000 * (info.yres * 25.4f) / info.height;
    config.mFps  = refreshRate / 1000.0f;
    ALOGW("xres         = %d px\n"
          "yres         = %d px\n"
          "fps          = %.2f Hz\n"
          "bpp          = %d\n"
          "r            = %2u:%u\n"
          "g            = %2u:%u\n"
          "b            = %2u:%u\n",
          info.xres, info.yres, config.mFps, info.bits_per_pixel,
          info.red.offset, info.red.length, info.green.offset,
          info.green.length, info.blue.offset, info.blue.length);

    config.mVsyncPeriod  = 1000000000000 / refreshRate;
    config.mFormat = (info.bits_per_pixel == 32)
                       ? ((info.red.offset == 0) ? FORMAT_RGBA8888 :
                            FORMAT_BGRA8888)
                       : FORMAT_RGB565;
    config.mBytespixel = info.bits_per_pixel >> 3;
    config.mStride = finfo.line_length;

    for (size_t i=0; i<mConfigs.size(); i++) {
        if (i == (size_t)configId) {
            continue;
        }
        DisplayConfig& item = mConfigs.editItemAt(i);
        item.mXdpi = config.mXdpi;
        item.mYdpi = config.mYdpi;
        item.mVsyncPeriod = config.mVsyncPeriod;
        item.mFormat = config.mFormat;
    }

    return configId;
}

int FbDisplay::openFb()
{
    Mutex::Autolock _l(mLock);

    if (mFb < 0) {
        ALOGE("invalid fb");
        return -EINVAL;
    }

    // already initialized...
    if (mOpened) {
        ALOGI("display already initialized...");
        return 0;
    }

    ALOGV("open fb:%d", mFb);
    mFd = -1;
    char name[64];
    snprintf(name, 64, HWC_FB_DEV"%d", mFb);
    mFd = open(name, O_RDWR, 0);
    if (mFd < 0) {
        ALOGE("<%s,%d> open %s failed", __func__, __LINE__, name);
        return -errno;
    }

    if(mFb != 0) {
        if(ioctl(mFd, FBIOBLANK, POWER_ON) < 0) {
            ALOGE("<%s, %d> ioctl FBIOBLANK failed", __func__, __LINE__);
        }
    }

    int ret = setDefaultFormatLocked();
    if (ret != 0) {
        ALOGE("%s setDefaultFormatLocked failed", __func__);
        return ret;
    }

    mActiveConfig = readConfigLocked();
    prepareTargetsLocked();
    mPowerMode  = POWER_ON;
    mOpened = true;

    return 0;
}

int FbDisplay::closeFb()
{
    invalidLayers();

    Mutex::Autolock _l(mLock);

    mRenderTarget = NULL;
    if (mAcquireFence != -1) {
        close(mAcquireFence);
        mAcquireFence = -1;
    }
    mConfigs.clear();
    mActiveConfig = -1;

    if (!mOpened) {
        return 0;
    }

    ALOGV("close fb:%d", mFb);
    mOpened = false;
    releaseTargetsLocked();
    close(mFd);
    return 0;
}

void FbDisplay::prepareTargetsLocked()
{
    MemoryDesc desc;
    const DisplayConfig& config = mConfigs[mActiveConfig];
    desc.mWidth = config.mXres;
    desc.mHeight = config.mYres;
    desc.mFormat = config.mFormat;
    desc.mFslFormat = config.mFormat;
    desc.mProduceUsage |= USAGE_HW_COMPOSER |
                          USAGE_HW_2D | USAGE_HW_RENDER;
    desc.mFlag = FLAGS_FRAMEBUFFER;
    desc.checkFormat();

    MemoryManager* pManager = MemoryManager::getInstance();
    for (int i=0; i<MAX_FRAMEBUFFERS; i++) {
        pManager->allocMemory(desc, &mTargets[i]);
        if (mTargets[i]->stride != (config.mStride / config.mBytespixel)) {
            ALOGE("%s buffer stride not match!", __func__);
        }
    }
    mTargetIndex = 0;
}

void FbDisplay::releaseTargetsLocked()
{
    MemoryManager* pManager = MemoryManager::getInstance();
    for (int i=0; i<MAX_FRAMEBUFFERS; i++) {
        if (mTargets[i] == NULL) {
            continue;
        }
        pManager->releaseMemory(mTargets[i]);
        mTargets[i] = NULL;
    }
    mTargetIndex = 0;
}

int FbDisplay::getConfigIdLocked(int width, int height)
{
    int index = -1;
    DisplayConfig config;
    config.mXres = width;
    config.mYres = height;

    index = mConfigs.indexOf(config);
    if (index < 0) {
        index = mConfigs.add(config);
    }

    return index;
}

int FbDisplay::setDefaultFormatLocked()
{
    struct fb_var_screeninfo info;
    if (ioctl(mFd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("<%s,%d> FBIOGET_VSCREENINFO failed", __func__, __LINE__);
        close(mFd);
        return -errno;
    }

    /*
     * Explicitly request RGBA 8/8/8/8
     */
    info.bits_per_pixel   = 32;
    info.red.offset       = 0;
    info.red.length       = 8;
    info.red.msb_right    = 0;
    info.green.offset     = 8;
    info.green.length     = 8;
    info.green.msb_right  = 0;
    info.blue.offset      = 16;
    info.blue.length      = 8;
    info.blue.msb_right   = 0;
    info.transp.offset    = 24;
    info.transp.length    = 8;
    info.transp.msb_right = 0;
    info.activate = FB_ACTIVATE_NOW;

    if (ioctl(mFd, FBIOPUT_VSCREENINFO, &info) == -1) {
        ALOGE("setDefaultFormatLocked: RGBA8888 not supported now");
        return -errno;
    }

    return 0;
}

int FbDisplay::setActiveConfig(int configId)
{
    Mutex::Autolock _l(mLock);
    if (mActiveConfig == configId) {
        ALOGI("the same config, no need to change");
        return 0;
    }

    if (configId < 0 || configId >= (int)mConfigs.size()) {
        ALOGI("invalid config id:%d", configId);
        return -EINVAL;
    }

    const DisplayConfig& config = mConfigs[configId];

    struct fb_var_screeninfo info;
    if (ioctl(mFd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("<%s,%d> FBIOGET_VSCREENINFO failed", __func__, __LINE__);
        close(mFd);
        return -errno;
    }

    info.xres = config.mXres;
    info.yres = config.mYres;
    info.xres_virtual = info.xres;
    info.yres_virtual = info.yres;
    info.activate = FB_ACTIVATE_NOW;
    if (ioctl(mFd, FBIOPUT_VSCREENINFO, &info) == -1) {
        ALOGW("setActiveConfig: FBIOPUT_VSCREENINFO failed");
        return -EINVAL;
    }

    mActiveConfig = readConfigLocked();
    if (mActiveConfig != configId) {
        ALOGE("setActiveConfig failed");
        return -EINVAL;
    }

    releaseTargetsLocked();
    prepareTargetsLocked();

    return 0;
}

int FbDisplay::composeLayers()
{
    Mutex::Autolock _l(mLock);

    // mLayerVector's size > 0 means 2D composite.
    // only this case needs override mRenderTarget.
    if (mLayerVector.size() > 0) {
        mTargetIndex = mTargetIndex % MAX_FRAMEBUFFERS;
        mRenderTarget = mTargets[mTargetIndex];
        mTargetIndex++;
    }

    return composeLayersLocked();
}

void FbDisplay::handleVsyncEvent(nsecs_t timestamp)
{
    EventListener* callback = NULL;
    {
        Mutex::Autolock _l(mLock);
        callback = mListener;
    }

    if (callback == NULL) {
        return;
    }

    callback->onVSync(DISPLAY_PRIMARY, timestamp);
}

void FbDisplay::setFb(int fb)
{
    Mutex::Autolock _l(mLock);
    mFb = fb;
}

int FbDisplay::fb()
{
    Mutex::Autolock _l(mLock);
    return mFb;
}

int FbDisplay::powerMode()
{
    Mutex::Autolock _l(mLock);
    return mPowerMode;
}

int FbDisplay::readType()
{
    char fb_path[HWC_PATH_LENGTH];
    char value[HWC_STRING_LENGTH];
    FILE *fp = NULL;

    snprintf(fb_path, HWC_PATH_LENGTH, HWC_FB_SYS"%d/fsl_disp_dev_property", mFb);
    if (!(fp = fopen(fb_path, "r"))) {
        ALOGW("open %s failed", fb_path);
        Mutex::Autolock _l(mLock);
        mType = DISPLAY_LDB;
        return 0;
    }

    memset(value, 0, sizeof(value));
    if (!fgets(value, sizeof(value), fp)) {
        ALOGE("Unable to read %s", fb_path);
        Mutex::Autolock _l(mLock);
        mType = DISPLAY_LDB;
        fclose(fp);
        return -EINVAL;
    }

    if (strstr(value, "hdmi")) {
        ALOGI("fb%d is %s device", mFb, value);
        Mutex::Autolock _l(mLock);
        mType = DISPLAY_HDMI;
    }
    else if (strstr(value, "dvi")) {
        ALOGI("fb%d is %s device", mFb, value);
        Mutex::Autolock _l(mLock);
        mType = DISPLAY_DVI;
    }
    else {
        ALOGI("fb%d is %s device", mFb, value);
        Mutex::Autolock _l(mLock);
        mType = DISPLAY_LDB;
    }
    fclose(fp);

    return 0;
}

int FbDisplay::readConnection()
{
    char fb_path[HWC_PATH_LENGTH];
    char value[HWC_STRING_LENGTH];
    FILE *fp = NULL;

    {
        Mutex::Autolock _l(mLock);
        if (mFb < 0) {
            return -EINVAL;
        }

        if (mType == DISPLAY_LDB) {
            mConnected = true;
            return 0;
        }
    }

    snprintf(fb_path, HWC_PATH_LENGTH, HWC_FB_SYS"%d/disp_dev/cable_state", mFb);
    if (!(fp = fopen(fb_path, "r"))) {
        ALOGW("open %s failed", fb_path);
        Mutex::Autolock _l(mLock);
        mConnected = false;
        return -EINVAL;
    }

    memset(value, 0, sizeof(value));
    if (!fgets(value, sizeof(value), fp)) {
        ALOGE("Unable to read %s", fb_path);
        Mutex::Autolock _l(mLock);
        mConnected = false;
        fclose(fp);
        return -EINVAL;
    }

    if (strstr(value, "plugin")) {
        ALOGI("fb%d device %s", mFb, value);
        Mutex::Autolock _l(mLock);
        mConnected = true;
    }
    else {
        ALOGI("fb%d device %s", mFb, value);
        Mutex::Autolock _l(mLock);
        mConnected = false;
    }
    fclose(fp);

    return 0;
}

//----------------------------------------------------------
extern "C" int clock_nanosleep(clockid_t clock_id, int flags,
                           const struct timespec *request,
                           struct timespec *remain);

FbDisplay::VSyncThread::VSyncThread(FbDisplay *ctx)
    : Thread(false), mCtx(ctx), mEnabled(false),
      mFakeVSync(false), mNextFakeVSync(0), mFd(-1)
{
    mRefreshPeriod = 0;
}

void FbDisplay::VSyncThread::onFirstRef()
{
    run("HWC-VSYNC-Thread", android::PRIORITY_URGENT_DISPLAY);
}

int32_t FbDisplay::VSyncThread::readyToRun()
{
    char fb_path[HWC_PATH_LENGTH];
    memset(fb_path, 0, sizeof(fb_path));
    snprintf(fb_path, HWC_PATH_LENGTH, HWC_FB_SYS"%d""/vsync", DISPLAY_PRIMARY);

    mFd = open(fb_path, O_RDONLY);
    if (mFd <= 0) {
        ALOGW("open %s failed, fallback to fake vsync", fb_path);
        mFakeVSync = true;
    }

    return 0;
}

void FbDisplay::VSyncThread::setEnabled(bool enabled) {
    Mutex::Autolock _l(mLock);
    mEnabled = enabled;
    mCondition.signal();
}

void FbDisplay::VSyncThread::setFakeVSync(bool enable)
{
    Mutex::Autolock _l(mLock);
    mFakeVSync = enable;
}

bool FbDisplay::VSyncThread::threadLoop()
{
    { // scope for lock
        Mutex::Autolock _l(mLock);
        while (!mEnabled) {
            mCondition.wait(mLock);
        }
    }

    if (mFakeVSync) {
        performFakeVSync();
    }
    else {
        performVSync();
    }

    return true;
}

void FbDisplay::VSyncThread::performFakeVSync()
{
    const DisplayConfig& config = mCtx->getActiveConfig();
    mRefreshPeriod = config.mVsyncPeriod;
    const nsecs_t period = mRefreshPeriod;
    const nsecs_t now = systemTime(CLOCK_MONOTONIC);
    nsecs_t next_vsync = mNextFakeVSync;
    nsecs_t sleep = next_vsync - now;
    if (sleep < 0) {
        // we missed, find where the next vsync should be
        sleep = (period - ((now - next_vsync) % period));
        next_vsync = now + sleep;
    }
    mNextFakeVSync = next_vsync + period;

    struct timespec spec;
    spec.tv_sec  = next_vsync / 1000000000;
    spec.tv_nsec = next_vsync % 1000000000;

    int err;
    do {
        err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
    } while (err<0 && errno == EINTR);

    if (err == 0 && mCtx != NULL) {
        mCtx->handleVsyncEvent(next_vsync);
    }
}

void FbDisplay::VSyncThread::performVSync()
{
    uint64_t timestamp = 0;
    char buf[VSYNC_STRING_LEN];
    memset(buf, 0, VSYNC_STRING_LEN);
    static uint64_t lasttime = 0;

    ssize_t len = pread(mFd, buf, VSYNC_STRING_LEN-1, 0);
    if (len < 0) {
        ALOGE("unable to read vsync event error: %s", strerror(errno));
        return;
    }

    if (!strncmp(buf, "VSYNC=", strlen("VSYNC="))) {
        timestamp = strtoull(buf + strlen("VSYNC="), NULL, 0);
    }

    if (lasttime != 0) {
        ALOGV("vsync period: %llu", timestamp - lasttime);
    }

    lasttime = timestamp;
    if (mCtx != NULL) {
        mCtx->handleVsyncEvent(timestamp);
    }
}

}
