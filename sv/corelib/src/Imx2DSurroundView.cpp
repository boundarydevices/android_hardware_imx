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

#include "Imx2DSurroundView.hpp"

#include <cutils/log.h>
#include <math.h>

#include <vector>

using namespace std;
using namespace Eigen;

namespace imx {

Imx2DSV::Imx2DSV() : mLookupPtr(nullptr) {}

bool Imx2DSV::startSV() {
    int flatw = m2DParams.resolution.width;
    int flath = m2DParams.resolution.height;
    shared_ptr<PixelMap> LUT_ptr(new PixelMap[flath * flatw], std::default_delete<PixelMap[]>());
    if (LUT_ptr != nullptr) {
        PixelMap *LUT = LUT_ptr.get();
        memset((void *)LUT, -1, sizeof(PixelMap) * flath * flatw);
        mLookupPtr = LUT_ptr;
    } else {
        ALOGE("Cannot allocate Lookup mapping buffer!");
        return false;
    }
    return updateLUT();
}

bool Imx2DSV::stopSV() {
    mLookupPtr = nullptr;
    return true;
}

bool Imx2DSV::SetConfigs(ImxSV2DParams &sv2DParams, vector<Vector3d> &evsRotations,
                         vector<Vector3d> &evsTransforms, vector<Matrix<double, 3, 3>> &Ks,
                         vector<Matrix<double, 1, 4>> &Ds) {
    if (evsRotations.size() != 4 || evsTransforms.size() != 4 || Ks.size() != 4 || Ds.size() != 4)
        return false;
    m2DParams = sv2DParams;
    mEvsRotations = evsRotations;
    mEvsTransforms = evsTransforms;
    mKs = Ks;
    mDs = Ds;
    return true;
}

// Assume the input and out buffer share the same format/bpp
bool Imx2DSV::GetSVBuffer(vector<shared_ptr<char>> &distorts, void *flat_outbuf, uint32_t bpp) {
    uint32_t flatw = m2DParams.resolution.width;
    uint32_t flath = m2DParams.resolution.height;
    uint32_t stride = m2DParams.cam_resolution.width * bpp;
    uint32_t fstride = flatw * bpp;
    if (flat_outbuf == nullptr || distorts.size() != 4) {
        ALOGE("Error!Not valid input for SV process!");
        return false;
    }
    PixelMap *LUT = mLookupPtr.get();
    for (uint32_t v = 0; v < flath; v++) {
        for (uint32_t u = 0; u < flatw; u++) {
            PixelMap *pMap = LUT + v * flatw + u;
            if ((pMap->index0 >= 0) && (pMap->index1 >= 0)) {
                // alpha blending on overlap region
                auto distort0 = distorts[pMap->index0].get();
                auto distort1 = distorts[pMap->index1].get();
                unsigned int color0 =
                        *(int *)((char *)distort0 + stride * pMap->v0 + pMap->u0 * bpp);
                unsigned int color1 =
                        *(int *)((char *)distort1 + stride * pMap->v1 + pMap->u1 * bpp);
                float alpha = pMap->alpha0;
                unsigned char R = alpha * (color0 & 0xff0000 >> 16) +
                        (1 - alpha) * ((color1 & 0xff0000) >> 16);
                unsigned char G =
                        alpha * (color0 & 0xff00 >> 8) + (1 - alpha) * ((color1 & 0xff00) >> 8);
                unsigned char B = alpha * (color0 & 0xff) + (1 - alpha) * (color1 & 0xff);
                color0 = (R << 16) | (G << 8) | B;
                *(int *)((char *)flat_outbuf + fstride * ((int)v) + ((int)u) * bpp) = color0;
            } else if (pMap->index0 >= 0) {
                int index = pMap->index0;

                auto distort0 = distorts[index].get();
                unsigned int color0 =
                        *(int *)((char *)distort0 + stride * pMap->v0 + pMap->u0 * bpp);
                *(int *)((char *)flat_outbuf + fstride * v + u * bpp) = color0;
            }
        }
    }

    return true;
}

bool Imx2DSV::updateLUT() {
    uint32_t flatw = m2DParams.resolution.width;
    uint32_t flath = m2DParams.resolution.height;
    float pw = m2DParams.physical_size.width;
    float ph = m2DParams.physical_size.height;
    uint32_t width = m2DParams.cam_resolution.width;
    uint32_t height = m2DParams.cam_resolution.height;
    PixelMap *LUT = mLookupPtr.get();
    for (int index = 0; index < mEvsRotations.size(); index++) {
        auto &R = mEvsRotations[index];
        auto &T = mEvsTransforms[index];
        auto &K = mKs[index];
        auto &D = mDs[index];
        double k1 = D(0, 0);
        double k2 = D(0, 1);
        double k3 = D(0, 2);
        double k4 = D(0, 3);
        double fx = K(0, 0);
        double fy = K(1, 1);
        double cx = K(0, 2);
        double cy = K(1, 2);
        double normal = R.norm();
        AngleAxisd rotation_vector(normal, R / normal);
        auto rotation_matrix = rotation_vector.toRotationMatrix();
        for (uint32_t v = 0; v < flath; v++) {
            for (uint32_t u = 0; u < flatw; u++) {
                float x = -pw / 2 + u * pw / flatw;
                float y = ph / 2 - v * ph / flath;
                Vector3d worldPoint{x, y, 0};

                Vector3d cameraPoint = rotation_matrix * worldPoint + T;
                Vector3d normalizedCamera = cameraPoint / cameraPoint[2];
                Vector3d cameraZ{0, 0, 1};
                auto dot = cameraZ.dot(cameraPoint);
                auto fov = acos(dot / cameraPoint.norm());
                auto threshhold = M_PI / 3;
                // if the worldPoint beyond the camera FOV, ignore it.
                // assume we only care FOV < threshold point
                // a dot b = a.norm()*b.norm*cos(angle)
                // here angle should be less than 90 degree.
                if (dot / cameraPoint.norm() < cos(threshhold)) {
                    continue;
                }
                Vector3d undistortPoint = K * normalizedCamera;
                auto xc = (undistortPoint[0] / undistortPoint[2] - cx) / fx;
                auto yc = (undistortPoint[1] / undistortPoint[2] - cy) / fy;

                auto r = sqrt(xc * xc + yc * yc);
                double theta = atan(r);
                double theta_4 = k4 * pow(theta, 9);
                double theta_3 = k3 * pow(theta, 7);
                double theta_2 = k2 * pow(theta, 5);
                double theta_1 = k1 * pow(theta, 3);
                double theta_d = theta + theta_1 + theta_2 + theta_3 + theta_4;
                double x_distorted = xc * (theta_d / r);
                double y_distorted = yc * (theta_d / r);

                double u_distorted = fx * x_distorted + cx;
                double v_distorted = fy * y_distorted + cy;
                if (u_distorted >= 0 && v_distorted >= 0 && u_distorted < width &&
                    v_distorted < height) {
                    PixelMap *pMap = LUT + v * flatw + u;
                    if (pMap->index0 == -1) {
                        pMap->index0 = index;
                        pMap->u0 = u_distorted;
                        pMap->v0 = v_distorted;
                        pMap->fov0 = fov;
                    } else if ((index != pMap->index0) && (pMap->index1 == -1)) {
                        pMap->index1 = index;
                        pMap->u1 = u_distorted;
                        pMap->v1 = v_distorted;
                        pMap->fov1 = fov;
                        auto fov0 = pMap->fov0;
                        // alpha value is set based the ratio of distance the border
                        // of two camera FOV
                        auto alpha0 = (threshhold - fov0) / (threshhold - fov0 + threshhold - fov);
                        pMap->alpha1 = 1 - alpha0;
                        pMap->alpha0 = alpha0;
                    } else if ((index != pMap->index0) && (index != pMap->index1)) {
                        ALOGW("Warning! Region overlapped with 3 cameras");
                    }
                }
            }
        }
    }

    return true;
}

} // namespace imx
