
/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef CAR_EVS_APP_RENDERTOPVIEW_H
#define CAR_EVS_APP_RENDERTOPVIEW_H


#include "RenderBase.h"

#include <android/hardware/automotive/evs/1.1/IEvsEnumerator.h>
#include "ConfigManager.h"
#include "VideoTex.h"
#include <math/mat4.h>


using namespace ::android::hardware::automotive::evs::V1_1;
/*
 * For RenderTopView, mActiveCameras info is from jason file.
 * jason file include the four camera view relationship(offset/x/y)
 * can't get these infomation from camera metadata.
 * so hard code the width/height info here.
 * For RenderDirectView, the width/height is from camera metadata.
 * the metadata info is from evs hal. no need hardcode width/height
 * for RenderDirectView
 */
#define WIDTH_FOR_TOP_VIEW 1280
#define HEIGHT_FOR_TOP_VIEW 720

/*
 * Combines the views from all available cameras into one reprojected top down view.
 */
class RenderTopView: public RenderBase {
public:
    RenderTopView(sp<IEvsEnumerator> enumerator,
                  const std::vector<ConfigManager::CameraInfo>& camList,
                  const ConfigManager& config,
                  std::unique_ptr<Stream> targetCfg);

    virtual bool activate() override;
    virtual void deactivate() override;

    virtual bool drawFrame(const BufferDesc& tgtBuffer);

protected:
    struct ActiveCamera {
        const ConfigManager::CameraInfo&    info;
        std::unique_ptr<VideoTex>           tex;

        ActiveCamera(const ConfigManager::CameraInfo& c) : info(c) {};
    };

    void renderCarTopView();
    void renderCameraOntoGroundPlane(const ActiveCamera& cam);

    sp<IEvsEnumerator>              mEnumerator;
    const ConfigManager&            mConfig;
    std::vector<ActiveCamera>       mActiveCameras;
    std::unique_ptr<Stream>         mTargetCfg;

    struct {
        std::unique_ptr<TexWrapper> checkerBoard;
        std::unique_ptr<TexWrapper> carTopView;
    } mTexAssets;

    struct {
        GLuint simpleTexture;
        GLuint projectedTexture;
    } mPgmAssets;

    android::mat4   orthoMatrix;
};


#endif //CAR_EVS_APP_RENDERTOPVIEW_H
