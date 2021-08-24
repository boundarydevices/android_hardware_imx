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

#ifndef _FSL_DISPLAY_H_
#define _FSL_DISPLAY_H_

#include <cutils/properties.h>
#include <utils/threads.h>
#include <math.h>
#include "Memory.h"
#include "Layer.h"
#include "Composer.h"
#include "Edid.h"
#include <vector>

namespace fsl {

using android::Mutex;
using android::Condition;
using android::Thread;
using android::sp;
using android::Vector;

#define DISPLAY_PRIMARY 0

#define OVERLAY_COMPOSE_BIT 0
#define LAST_OVERLAY_BIT 1
#define CLIENT_COMPOSE_BIT 2
#define ONLY_OVERLAY_BIT 3
#define OVERLAY_COMPOSE_MASK (1 << OVERLAY_COMPOSE_BIT)
#define LAST_OVERLAY_MASK (1 << LAST_OVERLAY_BIT)
#define CLIENT_COMPOSE_MASK (1 << CLIENT_COMPOSE_BIT)
#define ONLY_OVERLAY_MASK (1 << ONLY_OVERLAY_BIT)

#define DEF_BACKLIGHT_DEV "pwm-backlight"
#define DEF_BACKLIGHT_PATH "/sys/class/backlight/"

#define DEFAULT_REFRESH_RATE             60
#define FLOAT_TOLERANCE                  0.01
#define RESERVED_DISPLAY_GROUP_ID        100

#define DEBUG_DUMP_REFRESH_RATE

class BufferSlot
{
public:
    static const uint32_t MAX_COUNT = 32;
    BufferSlot(uint32_t count);
    ~BufferSlot();

    // it may be block when no free slot.
    int32_t getFreeSlot();
    // it will return failure when no present slot.
    int32_t getPresentSlot();
    // get buffer from present queue.
    Memory* getPresentBuffer(int32_t slot);
    // add slot and buffer to present queue.
    void addPresentSlot(int32_t slot, Memory* buffer);
    int32_t presentSlotCount();
    int32_t presentTotal();

private:
    Mutex mLock;
    Condition mCondition;
    std::vector<int32_t> mFreeSlot;
    std::vector<int32_t> mPresentSlot;
    int32_t mLastPresent;
    int32_t mPresentTotal;
    Memory *mBuffers[MAX_COUNT];
};

class EventListener
{
public:
    virtual ~EventListener() {}
    virtual void onVSync(int disp, nsecs_t timestamp, int vsyncPeriodNanos) = 0;
    virtual void onHotplug(int disp, bool connected) = 0;
    virtual void onRefresh(int disp) = 0;
    virtual void onVSyncPeriodTimingChanged(int disp, nsecs_t newVsyncAppliedTimeNanos, bool refreshRequired, nsecs_t refreshTimeNanos) = 0;
    virtual void onSeamlessPossible(int disp) = 0;
};

enum {
    DISPLAY_INVALID = 0,
    DISPLAY_LDB = 1,
    DISPLAY_HDMI = 2,
    DISPLAY_DVI = 3,
    DISPLAY_HDMI_ON_BOARD = 4,
    DISPLAY_VIRTUAL = 5,
};

enum {
    POWER_ON = 0,
    POWER_DOZE,
    POWER_DOZE_SUSPEND,
    POWER_OFF,
};

enum {
    DISP_CONTENT_TYPE_NONE = 0,
    DISP_CONTENT_TYPE_GRAPHICS = 1,
    DISP_CONTENT_TYPE_PHOTO = 2,
    DISP_CONTENT_TYPE_CINEMA = 3,
    DISP_CONTENT_TYPE_GAME = 4,
};

struct DisplayConfig
{
    int mXres;
    int mYres;
    int mFormat;
    int mBytespixel;
    int mStride; // in bytes.
    float mFps;
    int mVsyncPeriod;
    int mXdpi;
    int mYdpi;
    int cfgGroupId;
    int modeIdx; // drmModeModeInfo index
};

typedef int (*sw_sync_timeline_create_func)(void);
typedef int (*sw_sync_timeline_inc_func)(int fd, unsigned count);
typedef int (*sw_sync_fence_create_func)(int fd, const char *name, unsigned value);
class Display
{
public:
    Display();
    virtual ~Display();

    // layers manage.
    // release layer after its usage.
    void releaseLayer(int index);
    // get layer with index.
    Layer* getLayer(int index);
    Layer* getLayerByPriv(void* priv);
    // get empty layer.
    Layer* getFreeLayer();
    // clean and invalidate all layers.
    int invalidLayers();
    // set layer info with specified layer.
    void setLayerInfo(int index, Layer* layer);
    // set or unset SKIP_LAYER flag for all layers.
    int setSkipLayer(bool skip);
    // get changed composition types for all layers.
    int getChangedTypes(uint32_t* outNumTypes, uint64_t* outLayers,
                        int32_t* outTypes);
    // get display&layer request for all layers.
    int getRequests(int32_t* outDisplayRequests, uint32_t* outNumRequests,
                    uint64_t* outLayers, int32_t* outLayerRequests);
    int getReleaseFences(uint32_t* outNumElements, uint64_t* outLayers,
                         int32_t* outFences);
    // verify all layers and marks if device can handle.
    bool verifyLayers();
    // set display composite target buffer.
    int setRenderTarget(Memory* buffer, int acquireFence);
    // to do composite all layers.
    virtual int composeLayers();
    // trigger Composer refresh
    void triggerRefresh();
    // force Vync event with evs display
    bool forceVync();

    // add hw layer.
    int addHwLayer(uint32_t index, Layer *layer);
    // remove hw layer.
    int removeHwLayer(uint32_t index);

    // display property.
    // set display vsync/hotplug callback.
    void setCallback(EventListener* callback);
    // set some hardware limitation bits
    void setDisplayLimitation(int limit);
    // set display power on/off.
    virtual int setPowerMode(int mode);
    // enable display vsync thread.
    virtual void enableVsync();
    // enable/disable display vsync.
    virtual void setVsyncEnabled(bool enabled);
    // use software vsync.
    virtual void setFakeVSync(bool enable);
    // set display connection state.
    void setConnected(bool connected);
    // get display connection state.
    bool connected();
    // set display type.
    void setType(int type);
    // get display type.
    int type();
    // set display index of array.
    void setIndex(int index);
    // get display index of array.
    int index();

    virtual bool checkOverlay(Layer* layer);
    virtual int performOverlay();
    // update composite buffer to screen.
    virtual int updateScreen();
    // set display active config.
    virtual int setActiveConfig(int configId);
    virtual int createDisplayConfig(int width, int height, float fps, int format);
    virtual int getPresentFence(int32_t* outPresentFence) {
        if (outPresentFence != NULL)
            *outPresentFence = -1;
        return 0;
    }
    // get display mode format size
    int getFormatSize(int format);
    // set display specified config parameters.
    int setConfig(int width, int height, int* format);
    // clear all display configs.
    void clearConfigs();
    // get display config with index.
    const DisplayConfig& getConfig(int config);
    // get display config with internal active index.
    const DisplayConfig& getActiveConfig();
    // copy the srcId config and insert to dstId, and use it as active config
    int CopyAsActiveConfigLocked(int srcId, int dstId);
    int CopyAsActiveConfig(int srcId, int dstId);
    // get display active config index.
    int getActiveId();
    // find the config index of specific parameters
    int findDisplayConfig(int width, int height, float fps, int format);
    // get display config number.
    int getConfigNum();
    // check whether display support HDR or not
    virtual bool isHdrSupported();
    // get HDR metadata
    int getHdrMetaData(HdrMetaData* hdrMetaData);
    bool triggerComposition();
    // init display brightness check
    void initBrightness();
    // return mMaxBrightness
    int getMaxBrightness();
    // set display Brightness with brightness
    int setBrightness(float brightness);
    // get display identification data
    int getDisplayIdentificationData(uint8_t* displayPort, uint8_t *data, uint32_t size);
    // get display connection type: internal or external
    int getDisplayConnectionType();
    // get the VSYNC period of the display
    nsecs_t getDisplayVsyncPeroid();
    // check if display support low latency mode
    bool isLowLatencyModeSupport();
    // set display low latency mode
    int setAutoLowLatencyMode(bool on);
    // get display supported content types
    int getSupportedContentTypes(int num, uint32_t *supportedTypes);
    // set display content type
    int setContentType(int contentType);
    // get group id of the display configuration
    int getConfigGroup(int config);
    virtual int changeDisplayConfig(int config, nsecs_t desiredTimeNanos, bool seamlessRequired,
                            nsecs_t *outAppliedTime, bool *outRefresh, nsecs_t *outRefreshTime);


protected:
    int composeLayersLocked();
    void resetLayerLocked(Layer* layer);
    void waitOnFenceLocked();
    bool check2DComposition();
    bool directCompositionLocked();

protected:
    Mutex mLock;
    int mIndex;
    bool mConnected;
    int mType;

    int mActiveConfig;
    Vector<DisplayConfig> mConfigs;
    bool mRefreshRequired;

    LayerVector mLayerVector;
    Layer* mLayers[MAX_LAYERS];
    Layer* mHwLayers[MAX_LAYERS];
    Composer& mComposer;
    Memory* mRenderTarget;
    int mAcquireFence;
    int mComposeFlag;
    Edid* mEdid;
    int mTileHwLimit;
    bool mResetHdrMode;
    bool mUiUpdate;
    EventListener* mListener;
    int mTotalLayerNum;
    int mMaxBrightness;
    char mBrightnessPath[PROPERTY_VALUE_MAX];
    int mTimelineFd;
    unsigned mNextSyncPoint;
    void* mSyncHandle;
    sw_sync_timeline_create_func m_sw_sync_timeline_create;
    sw_sync_timeline_inc_func m_sw_sync_timeline_inc;
    sw_sync_fence_create_func m_sw_sync_fence_create;
#ifdef DEBUG_DUMP_REFRESH_RATE
    nsecs_t m_pre_commit_start;
    nsecs_t m_pre_commit_time;
    nsecs_t m_total_sf_delay; // surfaceflinger updatescreen delay(compare with vsync period)
    nsecs_t m_total_commit_time;
    nsecs_t m_total_commit_cost;
    int m_request_refresh_cnt;
    int m_commit_cnt;
#endif
};

}
#endif
