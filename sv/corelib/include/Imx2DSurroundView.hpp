/*
 *  Copyright 2020 NXP.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef IMX_2D_SURROUNDVIEW_HPP_
#define IMX_2D_SURROUNDVIEW_HPP_

#include <Eigen/Core>
#include <Eigen/Geometry>
#include "ImxSurroundViewTypes.hpp"

using namespace std;
using namespace Eigen;

namespace imx {

struct ImxSV2DParams {
  // fisheye image resolution (width, height).
  Size2dInteger cam_resolution;

  // surround view 2d image resolution (width, height).
  Size2dInteger resolution;

  // the physical size of surround view 2d area in surround view coordinate.
  // (surround view coordinate is defined as X rightward, Y forward and
  // the origin lies on the center of the (symmetric) bowl (ground).
  // When bowl is not used, surround view coordinate origin lies on the
  // center of car model bounding box.)
  // The unit should be consistent with camera extrinsics (translation).
  Size2dFloat physical_size;

  ImxSV2DParams()
      : cam_resolution{0, 0},
        resolution{0, 0},
        physical_size{0.0f, 0.0f}{}

  ImxSV2DParams(Size2dInteger cam_resolution_, 
          Size2dInteger resolution_, Size2dFloat physical_size_)
      : cam_resolution(cam_resolution_),
        resolution(resolution_),
        physical_size(physical_size_){}

  bool IsValid() const {
    return resolution.IsValid() && physical_size.IsValid() && cam_resolution.IsValid();
  }

  bool operator==(const ImxSV2DParams& rhs) const {
    return resolution == rhs.resolution && physical_size == rhs.physical_size &&
        cam_resolution == rhs.cam_resolution;
  }

  ImxSV2DParams& operator=(const ImxSV2DParams& rhs) {
    cam_resolution = rhs.cam_resolution;
    resolution = rhs.resolution;
    physical_size = rhs.physical_size;
    return *this;
  }
};

class Imx2DSV {
    public:
    virtual ~Imx2DSV() = default;
    Imx2DSV();
    
    bool startSV();
    bool stopSV();
    bool SetConfigs(ImxSV2DParams &sv2DParams,
            vector<Vector3d> &evsRotations, vector<Vector3d> &evsTransforms,
            vector<Matrix<double, 3, 3>> &Ks, vector<Matrix<double, 1, 4>> &Ds);
    bool GetSVBuffer(vector<shared_ptr<char>> &distorts, void *flat_outbuf,
            uint32_t bpp);

    private:
    struct PixelMap{
        int index0;
        int index1;
        unsigned int u0;
        unsigned int v0;
        float fov0;
        float alpha0;
        unsigned int u1;
        unsigned int v1;
        float fov1;
        float alpha1;
    };

    bool updateLUT();

    ImxSV2DParams m2DParams;
    vector<Vector3d> mEvsRotations;
    vector<Vector3d> mEvsTransforms;
    vector<Matrix<double, 3, 3>> mKs;
    vector<Matrix<double, 1, 4>> mDs;
    shared_ptr<PixelMap> mLookupPtr;
};

} //namespace
#endif
