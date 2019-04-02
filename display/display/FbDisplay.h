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

#ifndef _FB_DISPLAY_H_
#define _FB_DISPLAY_H_

#include <linux/fb.h>
#include <utils/threads.h>

#include "Display.h"

namespace fsl {

// numbers of buffers for page flipping
#ifndef NUM_FRAMEBUFFER_SURFACE_BUFFERS
#define MAX_FRAMEBUFFERS 3
#else
#define MAX_FRAMEBUFFERS NUM_FRAMEBUFFER_SURFACE_BUFFERS
#endif

#define HWC_PATH_LENGTH 256
#define HWC_MODES_LENGTH 1024
#define HWC_STRING_LENGTH 32
#define SYS_GRAPHICS "/sys/class/graphics"
#define HWC_FB_SYS "/sys/class/graphics/fb"
#define HWC_FB_DEV "/dev/graphics/fb"

using android::Condition;

class FbDisplay : public Display
{
public:
    FbDisplay();
    virtual ~FbDisplay();

    // set display power on/off.
    virtual int setPowerMode(int mode);
    // enable display vsync thread.
    virtual void enableVsync();
    // enable/disable display vsync.
    virtual void setVsyncEnabled(bool enabled);
    // use software vsync.
    virtual void setFakeVSync(bool enable);

    virtual bool checkOverlay(Layer* layer);
    virtual int performOverlay();
    // compose all layers.
    virtual int composeLayers();
    // set display active config.
    virtual int setActiveConfig(int configId);
    // update composite buffer to screen.
    virtual int updateScreen();
    virtual int getPresentFence(int32_t* outPresentFence);

    // open fb device.
    int openFb();
    // close fb device.
    int closeFb();
    // read display type.
    int readType();
    // read display connection state.
    int readConnection();
    // set display fb number.
    void setFb(int fb);
    // get display fb number.
    int fb();
    // get display power mode.
    int powerMode();

private:
    int readConfigLocked();
    int setDefaultFormatLocked();
    int getConfigIdLocked(int width, int height);
    void prepareTargetsLocked();
    void releaseTargetsLocked();
    int convertFormatInfo(int format, int* bpp);

protected:
    int mFb;
    int mFd;
    int mPowerMode;

    bool mOpened;
    int mTargetIndex;
    Memory* mTargets[MAX_FRAMEBUFFERS];

    int mOvFd;
    struct fb_var_screeninfo mOvInfo;
    int mOvPowerMode;
    Layer* mOverlay;
    int mOutFence;
    int mPresentFence;

protected:
    void handleVsyncEvent(nsecs_t timestamp);
    class VSyncThread : public Thread {
    public:
        explicit VSyncThread(FbDisplay *ctx);
        void setEnabled(bool enabled);
        void setFakeVSync(bool enable);

    private:
        virtual void onFirstRef();
        virtual int32_t readyToRun();
        virtual bool threadLoop();
        void performFakeVSync();
        void performVSync();

        FbDisplay *mCtx;
        mutable Mutex mLock;
        Condition mCondition;
        bool mEnabled;

        bool mFakeVSync;
        mutable nsecs_t mNextFakeVSync;
        nsecs_t mRefreshPeriod;
        int mFd;
    };

    sp<VSyncThread> mVsyncThread;
};

}
#endif
