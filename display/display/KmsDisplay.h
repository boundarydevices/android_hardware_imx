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

#include <drm_fourcc.h>
#include <hwsecure_client.h>
#include <utils/threads.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <chrono>
#include <condition_variable>

#include "Display.h"
#include "MemoryDesc.h"
#include "MemoryManager.h"

namespace fsl {

// numbers of buffers for page flipping
#ifndef NUM_FRAMEBUFFER_SURFACE_BUFFERS
#define MAX_FRAMEBUFFERS 3
#else
#define MAX_FRAMEBUFFERS NUM_FRAMEBUFFER_SURFACE_BUFFERS
#endif

#define KMS_FORCE_VYNC_WAIT 100000000LL // unit ns, wait 100ms

using android::Condition;

#define ARRAY_LEN(_arr) (sizeof(_arr) / sizeof(_arr[0]))
#define KMS_PLANE_NUM 2

struct KmsPlane {
    void getPropertyIds();
    void connectCrtc(drmModeAtomicReqPtr pset, uint32_t crtc, uint32_t fb);
    void setDisplayFrame(drmModeAtomicReqPtr pset, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void setSourceSurface(drmModeAtomicReqPtr pset, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void setAlpha(drmModeAtomicReqPtr pset, uint32_t alpha);
    void setTableOffset(drmModeAtomicReqPtr pset, MetaData *meta);
    void setClientFence(drmModeAtomicReqPtr pset, int fd);

    uint32_t src_x;
    uint32_t src_y;
    uint32_t src_w;
    uint32_t src_h;
    uint32_t crtc_x;
    uint32_t crtc_y;
    uint32_t crtc_w;
    uint32_t crtc_h;

    uint32_t alpha_id;
    uint32_t ofs_id;
    uint32_t fb_id;
    uint32_t crtc_id;
    uint32_t mPlaneID;
    uint32_t fence_id;
    int mDrmFd;
};

struct TableProperty {
    const char *name;
    uint32_t *ptr;
};

class KmsDisplay : public Display {
public:
    KmsDisplay();
    virtual ~KmsDisplay();
    int setSecureDisplayEnable(bool enable, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    // set display power on/off.
    virtual int setPowerMode(int mode);
    // enable display vsync thread.
    virtual void enableVsync();
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
    virtual int getPresentFence(int32_t *outPresentFence);
    // switch to new display config
    virtual int changeDisplayConfig(int config, nsecs_t desiredTimeNanos, bool seamlessRequired,
                                    nsecs_t *outAppliedTime, bool *outRefresh,
                                    nsecs_t *outRefreshTime);

    // open drm device.
    int openKms();
    // config releated info when drm device is not ready.
    int openFakeKms();
    // close drm device.
    int closeKms();
    // read display type.
    int readType();
    // read display connection state.
    int readConnection();
    // set display drm fd and connector id.
    int setDrm(int drmfd, size_t connectorId);
    // get display drm fd.
    int drmfd() { return mDrmFd; }
    // get crtc pipe.
    int crtcpipe() { return mCrtcIndex; }
    // get display power mode.
    int powerMode();

    virtual bool checkOverlay(Layer *layer);
    virtual int performOverlay();
    static void getTableProperty(uint32_t objectID, uint32_t objectType,
                                 struct TableProperty *table, size_t tableLen, int drmfd);
    static void getPropertyValue(uint32_t objectID, uint32_t objectType, const char *propName,
                                 uint32_t *propId, uint64_t *value, int drmfd);
    // check whether display support HDR or not
    virtual bool isHdrSupported();

private:
    int setActiveConfigLocked(int configId);
    void buildDisplayConfigs(uint32_t mmWidth, uint32_t mmHeight, int format);
    int createDisplayConfig(int width, int height, float fps, int format);
    void prepareTargetsLocked();
    void releaseTargetsLocked();
    uint32_t convertFormatToDrm(uint32_t format);
    void getKmsProperty();
    int getPrimaryPlane();
    int getDisplayMode(drmModeConnectorPtr pConnector);

    void bindCrtc(drmModeAtomicReqPtr pset, uint32_t mode);
    void bindOutFence(drmModeAtomicReqPtr pset);
    void setHdrMetaData(drmModeAtomicReqPtr pset, hdr_output_metadata hdrMetaData);
    bool getGUIResolution(int &width, int &height);
    bool veritySourceSize(Layer *layer);
    void parseDisplayMode(int *width, int *height, int *vrefresh, int *prefermode);
#ifdef HAVE_UNMAPPED_HEAP
    int checkSecureLayers();
#endif
    bool mSecureDisplay;
    bool mForceModeSet;
    Memory *mDummyTarget;
    Layer *mDummylayer;
    bool mUseOverlayAndroidUI;

protected:
    int mDrmFd;
    int mPowerMode;

    int mTargetIndex;
    Memory *mTargets[MAX_FRAMEBUFFERS];
#ifdef HAVE_UNMAPPED_HEAP
    int mSecTargetIndex;
    Memory *mSecTargets[MAX_FRAMEBUFFERS];
    enum g2d_secure_mode mG2dMode;
#endif
    bool mHDCPMode;
    int mHDCPDisableCnt;
    bool mHDCPEnable;

    struct {
        uint32_t mode_id;
        uint32_t active;
        uint32_t fence_ptr;
        uint32_t present_fence_ptr;
        uint32_t force_modeset_id;
        uint32_t disp_xfer_id;
    } mCrtc;
    uint32_t mCrtcID;
    int mCrtcIndex;
    int mEncoderType;
    int mOutFence;
    int mPresentFence;

    struct {
        uint32_t crtc_id;
        uint32_t dpms_id;
        uint32_t hdr_meta_id;
        uint32_t protection_id;
    } mConnector;
    uint32_t mConnectorID;

    drmModeModeInfo mMode;
    Vector<drmModeModeInfo> mDrmModes;
    int mModePrefered;
    bool mModeset;
    KmsPlane mKmsPlanes[KMS_PLANE_NUM];
    uint32_t mKmsPlaneNum;
    drmModeAtomicReqPtr mPset;
    MemoryManager *mMemoryManager;
    bool mNoResolve;
    bool mAllowModifier;
    uint32_t mMetadataID;
    hdr_output_metadata mLastHdrMetaData;

protected:
    void handleVsyncEvent(nsecs_t timestamp);
    void handleRefreshFrameMissed(nsecs_t newAppliedTime, bool refresh, nsecs_t newRefreshTime);
    int setNewDrmMode(int index);
    int stopRefreshEvent();
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
        bool mSendVsync;

        bool mFakeVSync;
        mutable nsecs_t mNextFakeVSync;
        nsecs_t mRefreshPeriod;
    };

    sp<VSyncThread> mVsyncThread;

    class ConfigThread : public Thread {
    public:
        explicit ConfigThread(KmsDisplay *ctx);
        void setDisplayConfig(int configId, nsecs_t desiredTime, nsecs_t refreshTime);
        void notifyNewFrame(nsecs_t timestamp);

    private:
        virtual void onFirstRef();
        virtual int32_t readyToRun();
        virtual bool threadLoop();

        KmsDisplay *mCtx;
        mutable Mutex mLock;
        Condition mCondition;
        std::condition_variable mCondv;
        bool mNewChange;

        int mNewConfig;
        nsecs_t mDesiredTime;
        nsecs_t mRefreshTime;
    };

    sp<ConfigThread> mConfigThread;
};

} // namespace fsl
#endif
