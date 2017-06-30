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

#ifndef _KMS_DISPLAY_H_
#define _KMS_DISPLAY_H_

#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <utils/threads.h>

#include "IonManager.h"
#include "MemoryDesc.h"
#include "Display.h"

namespace fsl {

// numbers of buffers for page flipping
#ifndef NUM_FRAMEBUFFER_SURFACE_BUFFERS
#define MAX_FRAMEBUFFERS 3
#else
#define MAX_FRAMEBUFFERS NUM_FRAMEBUFFER_SURFACE_BUFFERS
#endif

using android::Condition;

#define ARRAY_LEN(_arr) (sizeof(_arr) / sizeof(_arr[0]))

struct KmsProperty
{
    struct {
        uint32_t mode_id;
        uint32_t active;
    } crtc;

    struct {
        uint32_t src_x;
        uint32_t src_y;
        uint32_t src_w;
        uint32_t src_h;
        uint32_t crtc_x;
        uint32_t crtc_y;
        uint32_t crtc_w;
        uint32_t crtc_h;
        uint32_t fb_id;
        uint32_t crtc_id;
    } plane;

    struct {
        uint32_t crtc_id;
        uint32_t dpms_id;
    } connector;
};

struct TableProperty
{
    const char *name;
    uint32_t *ptr;
};

class IonManager;

class KmsDisplay : public Display, public MemoryListener
{
public:
    KmsDisplay();
    virtual ~KmsDisplay();

    virtual int onMemoryDestroyed(Memory* handle);
    // set display power on/off.
    virtual int setPowerMode(int mode);
    // enable display vsync thread.
    void enableVsync();
    // set display vsync/hotplug callback.
    void setCallback(EventListener* callback);
    // enable/disable display vsync.
    virtual void setVsyncEnabled(bool enabled);
    // use software vsync.
    virtual void setFakeVSync(bool enable);

    // compose all layers.
    virtual int composeLayers();
    // set display active config.
    virtual int setActiveConfig(int configId);
    // update composite buffer to screen.
    virtual int updateScreen();

    // open drm device.
    int openKms(drmModeResPtr pModeRes);
    // close drm device.
    int closeKms();
    // read display type.
    int readType();
    // read display connection state.
    int readConnection();
    // set display drm fd and connector id.
    int setDrm(int drmfd, size_t connectorId);
    // get display drm fd.
    int drmfd() {return mDrmFd;}
    // get crtc pipe.
    int crtcpipe() {return mCrtcIndex;}
    // get display power mode.
    int powerMode();

private:
    int getConfigIdLocked(int width, int height);
    void prepareTargetsLocked();
    void releaseTargetsLocked();
    uint32_t convertFormatToDrm(uint32_t format);
    void getKmsProperty();
    void getTableProperty(uint32_t objectID, uint32_t objectType,
                                  struct TableProperty *table,
                                  size_t tableLen);
    void getPropertyValue(uint32_t objectID, uint32_t objectType,
                          const char *propName, uint32_t* propId,
                          uint64_t* value);
    void addAtomicRequest(drmModeAtomicReqPtr pset, uint32_t modeID, uint32_t fb);
    int getPrimaryPlane();

protected:
    int mDrmFd;
    int mPowerMode;

    int mTargetIndex;
    Memory* mTargets[MAX_FRAMEBUFFERS];

    drmModeModeInfo mMode;
    uint32_t mConnectorID;
    uint32_t mCrtcID;
    int mCrtcIndex;
    uint32_t mPlaneID;
    bool mModeset;
    KmsProperty mKmsProperty;

    IonManager* mIonManager;

protected:
    void handleVsyncEvent(nsecs_t timestamp);
    class VSyncThread : public Thread {
    public:
        explicit VSyncThread(KmsDisplay *ctx);
        void setEnabled(bool enabled);
        void setFakeVSync(bool enable);

    private:
        virtual void onFirstRef();
        virtual int32_t readyToRun();
        virtual bool threadLoop();
        void performFakeVSync();
        void performVSync();

        KmsDisplay *mCtx;
        mutable Mutex mLock;
        Condition mCondition;
        bool mEnabled;

        bool mFakeVSync;
        mutable nsecs_t mNextFakeVSync;
        nsecs_t mRefreshPeriod;
    };

    sp<VSyncThread> mVsyncThread;
    EventListener* mListener;
};

}
#endif
