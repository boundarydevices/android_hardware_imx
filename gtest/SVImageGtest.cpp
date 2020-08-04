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

#define LOG_TAG "image-test"

#include <math.h>
#include <cutils/log.h>
#include <gtest/gtest.h>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <gui/BufferItem.h>
#include <gui/BufferQueue.h>
#include <gui/BufferItemConsumer.h>
#include <gui/IProducerListener.h>
#include <ui/GraphicBuffer.h>
#include <gui/ISurfaceComposer.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>


#include <Imx2DSurroundView.hpp>
#include "ImageUtils.h"

using namespace std;
using namespace Eigen;
using namespace imx;
using namespace android;
using Transaction = SurfaceComposerClient::Transaction;

static bool calibrationImage(float scaler,  Matrix<double, 3, 3> &K, Matrix<double, 1, 4> &D,
                    void *pixels, void *blended_pixels,
                    uint32_t width, uint32_t height, uint32_t stride,
                    void *output_buf, size_t outsize);
static bool prepareFisheyeImages(vector<shared_ptr<char>> &distorts);

static bool calibrationImage(float scaler,  Matrix<double, 3, 3> &K, Matrix<double, 1, 4> &D,
                    void *pixels, void *blended_pixels,
                    uint32_t width, uint32_t height, uint32_t stride,
                    void *output_buf, size_t outsize) {
    //Read image
    size_t size;
    size = height * stride;

    if(blended_pixels != nullptr) {
        memcpy(blended_pixels, pixels, size);
    }

    //undistorted with none matrix calculation
    double fx = K(0,0);
    double fy = K(1,1);
    double cx = K(0,2);
    double cy = K(1,2);
    double out_cx = cx*scaler;
    double out_cy = cy*scaler;
    double k1 = D(0,0);
    double k2 = D(0,1);
    double k3 = D(0,2);
    double k4 = D(0,3);
    int bpp = 4;
    uint32_t out_width = width *scaler;
    uint32_t out_height = height *scaler;
    uint32_t out_stride = out_width*bpp;
    size_t out_size = out_stride *out_height;

    char* pbuf;
    if(output_buf != nullptr) {
        if(outsize >= out_size) {
            pbuf = (char *)output_buf;
        } else {
            ALOGE("Not good! no enough buffer");
            return false;
        }
    }

    for(int v = 0; v < out_height; v++) {
        for(int u = 0; u < out_width; u++) {
            auto x = (u - out_cx) / fx;
            auto y = (v - out_cy) / fy;
            auto r = sqrt(x*x + y*y);
            double theta = atan(r);

            double theta_4 = k4*pow(theta, 9);
            double theta_3 = k3*pow(theta, 7);
            double theta_2 = k2*pow(theta, 5);
            double theta_1 = k1*pow(theta, 3);
            double theta_d = theta + theta_1 + theta_2 + theta_3 + theta_4;

            double x_distorted = x*(theta_d/r);
            double y_distorted = y*(theta_d/r);

            double u_distorted = fx * x_distorted + cx;
            double v_distorted = fy * y_distorted + cy;
            if(u_distorted >= 0 && v_distorted >=0 && \
                u_distorted < width && v_distorted < height) {
                memcpy(pbuf + out_stride * v + u*bpp,
                        (char *)pixels + stride * (int)v_distorted + (int)u_distorted*bpp,
                        bpp);

                if(blended_pixels != nullptr) {
                    int green_color = 3 | 252>>8| 119>>16;
                    int color = *(int *)((char *)blended_pixels + stride * (int)v_distorted + (int)u_distorted*bpp);
                    int blended_color = color * (1- 0.5) + 0.5*green_color;
                    *(int *)((char *)blended_pixels + stride * (int)v_distorted + (int)u_distorted*bpp) = blended_color;
                }
            }
            else {
                //memset(pbuf + stride * u + v, 0, bpp);
            }
        }
    }

    return true;
}

static void getHomography(Matrix<double, 3, 3> &Kb,
       Vector3d &bR, Vector3d &bT,
       Vector3d &cR, Vector3d &cT,
       Matrix<double, 3, 3> &Kc,
       Matrix<double, 3, 3> *homography) {
    double normal = cR.norm();

    AngleAxisd rotation_vector(normal, cR/normal);
    auto rotation_matrix = rotation_vector.toRotationMatrix();

    auto origin_point = rotation_matrix.inverse()*( - cT);
    auto d = origin_point[2]; //d = |z|;
    Vector3d n {0, 0, -1}; //unit vector which vetical to ground planar
    auto na = rotation_matrix * n; //in camera coordinate system
    auto birdeyenormal = bR.norm();
    AngleAxisd nextrotation_vector(birdeyenormal, bR/birdeyenormal);
    auto nextrotation_matrix = nextrotation_vector.toRotationMatrix();
    //auto bird_origin_point = nextrotation_matrix.inverse()*( - birdeyeT);
    auto betweenrotation_matrix = nextrotation_matrix * rotation_matrix.inverse();
    Vector3d betweent = bT - betweenrotation_matrix * cT;
    *homography = Kb * (betweenrotation_matrix + 1/d * betweent * na.transpose()) * Kc.inverse();
}

static void updateLUT(int width, int height,
                      int bwidth, int bheight,
                      vector<Matrix<double, 3, 3>> &homographies,
                      shared_ptr<PixelMap> &LUT_ptr)
{
    auto LUT = LUT_ptr.get();
    for(int index = 0; index < homographies.size(); index ++) {
        cout << "Homography " << index <<" matrix = \n" << homographies[index] << endl;
        auto &hinverse = homographies[index].inverse();
        for(uint32_t bv = 0; bv < bheight; bv++) {
            for(uint32_t bu = 0; bu < bwidth; bu++) {
                Vector3d pixel(bu, bv, 1);
                auto &cpixel = hinverse * pixel;
                auto cu = (int)(cpixel[0]/cpixel[2]);
                auto cv = (int)(cpixel[1]/cpixel[2]);
                if(cv >= 0 && cu >=0 && cu < width && cv < height) {
                    PixelMap *pMap = LUT + bv*bwidth + bu;
                    if(pMap->index0 == -1) {
                        pMap->index0 = index;
                        pMap->u0 = cu;
                        pMap->v0 = cv;
                    }
                    else if((index != pMap->index0)&&(pMap->index1 == -1)) {
                        pMap->index1 = index;
                        pMap->u1 = cu;
                        pMap->v1 = cv;
                    }
                    else if((index != pMap->index0) &&
                           (index != pMap->index1)) {
                        cout << "3 region:index="<< index <<", index0= "<< \
                            pMap->index0 << ", index1="<< pMap->index1 << endl;
                    }
                }
            }
        }
    }
}

static void generateBirdView(int /*width*/, int /*height*/, int stride,
                             int bwidth, int bheight, int bstride,
                             shared_ptr<PixelMap> &LUT_ptr,
                             vector<shared_ptr<char>> &undistorts,
                             shared_ptr<char> &birdeye_outbuf_ptr)
{
    int bpp = 4;
    auto LUT = LUT_ptr.get();
    auto birdeye_outbuf = birdeye_outbuf_ptr.get();
    for(uint32_t bv = 0; bv < bheight; bv++) {
        for(uint32_t bu = 0; bu < bwidth; bu++) {
            PixelMap *pMap = LUT + bv*bwidth + bu;
            if((pMap->index0 >= 0) && (pMap->index1 >= 0)) {
                //alpha blending on overlap region
                auto undistort0 = undistorts[pMap->index0].get();
                auto undistort1 = undistorts[pMap->index1].get();
                unsigned int color0 = *(int *)(( char *)undistort0 + \
                        stride * pMap->v0 + pMap->u0*bpp);
                unsigned int color1 = *(int *)(( char *)undistort1 + \
                        stride * pMap->v1 + pMap->u1*bpp);
                float alpha = 0.5;
                unsigned char R = alpha *(color0 & 0xff0000 >> 16) + (1 - alpha)*((color1 & 0xff0000) >> 16);
                unsigned char G = alpha *(color0 & 0xff00 >> 8) + (1- alpha)*((color1 & 0xff00) >> 8);
                unsigned char B = alpha *(color0 & 0xff) + (1- alpha)*(color1 & 0xff);
                color0 = (R << 16) | (G << 8) | B;
                *(int *)((char *)birdeye_outbuf + bstride * ((int)bv) + ((int)bu)*bpp) =
                    color0;
                //cout << "blending for pixel:" << bu << "," << bv <<"!!";
            }
            else if(pMap->index0 >= 0) {
                int index = pMap->index0;

                auto undistort0 = undistorts[index].get();
                unsigned int color0 = *(int *)(( char *)undistort0 + \
                        stride * pMap->v0 + pMap->u0*bpp);
                *(int *)((char *)birdeye_outbuf + bstride * bv + bu*bpp) =
                    color0;
            }
            /* should not run into this case
            else if(pMap->index1 > 0) {
                auto undistort1 = undistorts[pMap->index1 - 1].get();
                unsigned int color1 = *(int *)(( char *)undistort1 + \
                        stride * pMap->v1 + pMap->u1*bpp);
                *(int *)((char *)birdeye_outbuf + bstride * bv + bu*bpp) =
                    color1;
                cout << "Fetch image xx"<< (pMap->index0 - 1) <<" for pixel:" << bu << "," << bv <<"!!";
            }
            */
            //else
            //    cout << "No region mapped for " << bu << "," << bv <<"!!";
        }
    }
}

static void generateBirdView(int width, int height, int stride,
                             int bwidth, int bheight, int bstride,
                             vector<Matrix<double, 3, 3>> &homographies,
                             vector<shared_ptr<char>> &undistorts,
                             shared_ptr<char> &birdeye_outbuf_ptr)
{
    int *buf_mapper = nullptr;
    int bpp = 4;
    shared_ptr<int> buf_mapper_ptr(new int[bwidth * bheight],
                std::default_delete<int[]>());
    if(buf_mapper_ptr != nullptr) {
        buf_mapper = buf_mapper_ptr.get();
        memset((void *)buf_mapper, 0, bwidth * bheight * 4);
    }
    auto birdeye_outbuf = birdeye_outbuf_ptr.get();
    for(int index = 0; index < undistorts.size(); index ++) {
        //for each (u,v) belong to (w,h) in undistored jpg
        //[ub,vb,1]t = homography*[u,v,1]
        //fill birdeye image's[ub,vb] with pixel in undistorted image's [u,v]
        auto &homography = homographies[index];
        auto &undistort_outbuf = undistorts[index];
        auto undistort = undistort_outbuf.get();
        for(uint32_t v = 0; v < height; v++) {
            for(uint32_t u = 0; u < width; u++) {
                Vector3d undistort_v(u, v, 1);
                auto &birdeye_v = homography * undistort_v;
                //cout << "undistorted u="<< u << ", v=" << v << endl;
                //cout << "map to birdeye u,v,z = " << birdeye_v.transpose() << endl;
                //cout << "map to normalized birdeye u,v= " << birdeye_v[0]/birdeye_v[2] << " " <<
                //    birdeye_v[1]/birdeye_v[2] << endl;
                auto bv = birdeye_v[1]/birdeye_v[2];
                auto bu = birdeye_v[0]/birdeye_v[2];
                if(bv >= 0 && bu >=0 && bu < bwidth && bv < bheight) {
                    int *all_mapper = buf_mapper + (int)bv * bwidth + (int)bu;
                    char *current_mapper = ((char *)all_mapper) + index;
                    if(*all_mapper == 0)
                        memcpy((char *)birdeye_outbuf + bstride * ((int)bv) + ((int)bu)*bpp,
                            (char *)undistort + stride * (int)v + (int)u*bpp,
                            bpp);
                    else if (*current_mapper == 0){
                        unsigned int color = *(int *)((char *)birdeye_outbuf + bstride * ((int)bv) + ((int)bu)*bpp);
                        unsigned int newcolor = *(int *)(( char *)undistort + stride * (int)v + (int)u*bpp);
                        float alpha = 0.5;
                        unsigned char R = alpha *(color & 0xff0000 >> 16) + (1 - alpha)*((newcolor & 0xff0000) >> 16);
                        unsigned char G = alpha *(color & 0xff00 >> 8) + (1- alpha)*((newcolor & 0xff00) >> 8);
                        unsigned char B = alpha *(color & 0xff) + (1- alpha)*(newcolor & 0xff);
                        color = (R << 16) | (G << 8) | B;
                        *(int *)((char *)birdeye_outbuf + bstride * ((int)bv) + ((int)bu)*bpp) =
                            color;
                    }
                    *current_mapper = 1;
                }

            }
        }
    }

    //For those unmapped pixel, we fetch the neighbour's pixel to
    //fill them
    int maxs = 8;
    for(uint32_t v = maxs; v < (bheight - maxs); v++) {
        for(uint32_t u = maxs; u < (bwidth - maxs); u++) {
            int all_mapper = buf_mapper[(int)v * bwidth + (int)u];
            if(all_mapper == 0) {
                int color = 0;
                uint32_t R = 0;
                uint32_t G = 0;
                uint32_t B = 0;
                int count = 0;
                for(int s = 1; s < maxs; s ++) {
                    if(buf_mapper[(v-s) * bwidth + (u-s)] != 0){
                        color = *(int *)((char *)birdeye_outbuf + bstride * (v-s) + (u-s)*bpp);
                        R += (color & 0xff0000 >> 16);
                        G += (color & 0xff00 >> 8);
                        B += (color & 0xff);
                        count ++;
                    }
                    if(buf_mapper[(v-s) * bwidth + (u)] != 0){
                        color = *(int *)((char *)birdeye_outbuf + bstride * (v-s) + (u)*bpp);
                        R += (color & 0xff0000 >> 16);
                        G += (color & 0xff00 >> 8);
                        B += (color & 0xff);
                        count ++;
                    }

                    if(buf_mapper[(v-s) * bwidth + (u+s)] != 0){
                        color = *(int *)((char *)birdeye_outbuf + bstride * (v-s) + (u+s)*bpp);
                        R += (color & 0xff0000 >> 16);
                        G += (color & 0xff00 >> 8);
                        B += (color & 0xff);
                        count ++;
                    }

                    if(buf_mapper[v * bwidth + (u+s)] != 0){
                        color = *(int *)((char *)birdeye_outbuf + bstride * v + (u+s)*bpp);
                        R += (color & 0xff0000 >> 16);
                        G += (color & 0xff00 >> 8);
                        B += (color & 0xff);
                        count ++;
                    }

                    if(buf_mapper[v * bwidth + (u-s)] != 0){
                        color = *(int *)((char *)birdeye_outbuf + bstride * v + (u-s)*bpp);
                        R += (color & 0xff0000 >> 16);
                        G += (color & 0xff00 >> 8);
                        B += (color & 0xff);
                        count ++;
                    }

                    if(buf_mapper[(v+s) * bwidth + (u-s)] != 0){
                        color = *(int *)((char *)birdeye_outbuf + bstride * (v+s) + (u-s)*bpp);
                        R += (color & 0xff0000 >> 16);
                        G += (color & 0xff00 >> 8);
                        B += (color & 0xff);
                        count ++;
                    }

                    if(buf_mapper[(v+s) * bwidth + (u+s)] != 0){
                        color = *(int *)((char *)birdeye_outbuf + bstride * (v+s) + (u+s)*bpp);
                        R += (color & 0xff0000 >> 16);
                        G += (color & 0xff00 >> 8);
                        B += (color & 0xff);
                        count ++;
                    }
                    if(count != 0)
                        break;
                }

                if(count != 0){
                    unsigned char r = (unsigned char)(R/count);
                    unsigned char g = (unsigned char)(G/count);
                    unsigned char b = (unsigned char)(B/count);
                    *(int *)((char *)birdeye_outbuf + bstride * v + u*bpp) =
                        (r << 16) | (g << 8) | (b);
                }
            }
        }
    }
}

static void initCameraParameters(vector<Vector3d> &evsRotations, vector<Vector3d> &evsTransforms,
        vector<Matrix<double, 3, 3>> &Ks, vector<Matrix<double, 1, 4>> &Ds)
{
    evsRotations = {
        {2.26308, 0.0382788, -0.0220549},
        {1.67415, -1.74075, 0.789399},
        {-0.106409, -2.83697, 1.28629},
        {1.63019, 1.76475, -0.827941}
    };
    evsTransforms = {
        {-7.8028875403817685e-02, 1.4537396465103221e+00, -8.4197165554645001e-02},
        {2.9715052384687407e-01, 1.1407102692699396e+00, 3.0074545273489206e-01},
        {1.7115269161259747e-01, 1.4376160762596599e+00, -1.9028844233159006e-02},
        {-3.0842691427126512e-01, 1.0884122033556984e+00, 3.4419058255954926e-01}
    };
    Matrix<double, 3, 3> K0;
    K0 <<  608.0026093794693, 0.0,968.699544102168,
           0.0, 608.205469489769, 476.38843298898996,
           0.0, 0.0, 1.0;
    Ks.push_back(K0);
    Matrix<double, 3, 3> K1;
    K1 <<  607.8691721095306, 0.0,975.5686146375716,
           0.0, 608.0112887189435, 481.1938786570715,
           0.0, 0.0, 1.0;
    Ks.push_back(K1);
    Matrix<double, 3, 3> K2;
    K2 <<  608.557299289448, 0.0,960.1949354417656,
           0.0, 608.8093878512448, 474.74744054048256,
           0.0, 0.0, 1.0;
    Ks.push_back(K2);
    Matrix<double, 3, 3> K3;
    K3 <<  608.1221963545495, 0.0,943.6280444638576,
           0.0, 608.0523818661524, 474.8564698210861,
           0.0, 0.0, 1.0;
    Ks.push_back(K3);

    Matrix<double, 1, 4> D0;
    D0 << -0.03711481733589263,
          -0.0014805627895442888,
          -0.00030212056866592464,
          -0.00020149538570397933;
    Ds.push_back(D0);
    Matrix<double, 1, 4> D1;
    D1 << -0.040116809827977926,
          0.0028769489398543014,
          -0.002651039958977229,
          0.00024260630476736675;
    Ds.push_back(D1);
    Matrix<double, 1, 4> D2;
    D2 << -0.03998488563470043,
          0.00247866869091033888,
          -0.002354736769480817,
          0.00018369619088506146;
    Ds.push_back(D2);
    Matrix<double, 1, 4> D3;
    D3 << -0.038096507459563965,
          0.0004008114278766646,
          -0.0013549275607082035,
          -5.9961182248325556e-06;
    Ds.push_back(D3);
}

static bool prepareFisheyeImages(vector<shared_ptr<char>> &distorts) {
    uint32_t width = 0, height=0, stride=0;
    for(int index = 0; index < 4; index ++) {

        char input[128];
        memset(input, 0, sizeof(input));
        sprintf(input, "/sdcard/%d.png", index);

        AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
        getImageInfo(input, &width, &height, &stride);
        auto size = height * stride;
        shared_ptr<char> pixels_outbuf(new char[size],
                std::default_delete<char[]>());
        if(pixels_outbuf == nullptr)
            return false;
        auto pixels = pixels_outbuf.get();
        ALOGI("Image: %d x %d, stride %u, format %d", width, height, stride, format);
        decodeImage(pixels, size, input);

        distorts.push_back(pixels_outbuf);
    }
    return true;
}

class ImxSV2DTest: public ::testing::Test
{
protected:
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    uint32_t mStride = 0;
    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;
    Matrix<double, 3, 3> Kb;

    virtual void SetUp() {
        Kb <<  607.8691721095306, 0.0,975.5686146375716,
               0.0, 608.0112887189435, 481.1938786570715,
               0.0, 0.0, 1.0;

        initCameraParameters(evsRotations, evsTransforms,
            Ks, Ds);
        char input[128];
        memset(input, 0, sizeof(input));
        sprintf(input, "/sdcard/%d.png", 0);
        auto retValue = getImageInfo(input, &mWidth,
                        &mHeight, &mStride);
        if(!retValue)
            return;

        cout << "Get Image: width=" << mWidth << ", height=" << mHeight << ", stride=" \
                << mStride << endl;

        mComposerClient = new SurfaceComposerClient;
        ASSERT_EQ(NO_ERROR, mComposerClient->initCheck());
    }
    
    virtual void TearDown() {
    }

    virtual void ShowImage(uint32_t width, uint32_t height, void *pixels)  {
        mSurfaceControl = mComposerClient->createSurface(
                String8("ImxSVSurface"), width, height, PIXEL_FORMAT_RGBA_8888, 0);
        ASSERT_TRUE(mSurfaceControl != nullptr);
        ASSERT_TRUE(mSurfaceControl->isValid());

        Transaction t;
        ASSERT_EQ(NO_ERROR, t.setLayer(mSurfaceControl, 0x7fffffff)
                .show(mSurfaceControl)
                .apply());

        mSurface = mSurfaceControl->getSurface();
        ASSERT_TRUE(mSurface != nullptr);
        sp<ANativeWindow> anw(mSurface);
        ASSERT_EQ(NO_ERROR, native_window_api_connect(anw.get(), NATIVE_WINDOW_API_CPU));
        ASSERT_EQ(NO_ERROR, native_window_set_usage(anw.get(),
            GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN));

        ANativeWindowBuffer* anb;
        ASSERT_EQ(NO_ERROR, native_window_dequeue_buffer_and_wait(anw.get(),
            &anb));
        ASSERT_TRUE(anb != nullptr);

        sp<GraphicBuffer> buf(GraphicBuffer::from(anb));
        // Fill the buffer with the a checkerboard pattern
        uint8_t* img = nullptr;
        buf->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&img));
        int size =  width * height * 4;
        memcpy(img, pixels, size);
        buf->unlock();
        ASSERT_EQ(NO_ERROR, anw->queueBuffer(anw.get(), buf->getNativeBuffer(),
            -1));
        sleep(3);
    }

    sp<Surface> mSurface;
    sp<SurfaceComposerClient> mComposerClient;
    sp<SurfaceControl> mSurfaceControl;

    bool birdeye2DSurround(float bscaler, float bird_height,
                           uint32_t bwidth, uint32_t bheight, uint32_t bstride,
                           shared_ptr<char> &birdeye_outbuf_ptr, bool lut = false) {
        bool retValue;
        Vector3d birdeyeR(0, M_PI, 0);
        Vector3d birdeyeT(0, 0, bird_height);

        if(evsRotations.size() != 4 ||
            evsTransforms.size() != 4 ||
            Ks.size() != 4 ||
            Ds.size() != 4){
            cout << "Not valid input, as we need four camera parameters!!" << endl;
            return false;
        }

        auto b_cx = Kb(0,2);
        auto b_cy = Kb(1,2);
        Kb(0,2) = b_cx * bscaler;
        Kb(1,2) = b_cy * bscaler;

        uint32_t size = mHeight * mStride;

        int *buf_mapper = nullptr;
        shared_ptr<int> buf_mapper_ptr(new int[bwidth * bheight],
                    std::default_delete<int[]>());
        if(buf_mapper_ptr != nullptr) {
            buf_mapper = buf_mapper_ptr.get();
            memset((void *)buf_mapper, 0, bwidth * bheight * 4);
        }

        shared_ptr<PixelMap> LUT_ptr(new PixelMap[bwidth * bheight],
                    std::default_delete<PixelMap[]>());
        if(lut) {
            if(LUT_ptr != nullptr) {
                auto LUT = LUT_ptr.get();
                memset((void *)LUT, -1, sizeof(PixelMap) * bwidth * bheight);
            }
        }

        vector<shared_ptr<char>> undistorts;
        vector<Matrix<double, 3, 3>> homographies;
        for(int index = 0; index < evsRotations.size(); index ++) {
            auto &r = evsRotations[index];
            auto &t = evsTransforms[index];
            auto &K = Ks[index];
            auto &D = Ds[index];
            cout << "Camera " << index << "********" << endl;

            //Get undistored jpg
            float scaler = 1;
            char input[128];
            memset(input, 0, sizeof(input));
            sprintf(input, "/sdcard/%d.png", index);

            shared_ptr<char> pixels_outbuf(new char[size],
                std::default_delete<char[]>());
            if(pixels_outbuf == nullptr)
                return false;
            auto pixels = pixels_outbuf.get();
            decodeImage(pixels, size, input);

            shared_ptr<char> undistort_outbuf(new char[size],
                    std::default_delete<char[]>());
            if(undistort_outbuf == nullptr)
                return false;

            retValue = calibrationImage(scaler, K, D, pixels, nullptr,
                                        mWidth, mHeight, mStride,
                                        undistort_outbuf.get(), size);

            if(!retValue) {
                cout << "Error on calibration the image " << index <<" !!" << endl;
                return false;
            }
            undistorts.push_back(undistort_outbuf);

            Matrix<double, 3, 3> homography;
            getHomography(Kb, birdeyeR, birdeyeT,
                        r, t, K, &homography);

            cout << "Homography between bird and camera matrix = \n" << homography << endl;
            homographies.push_back(homography);
        }

        if(!lut)
            generateBirdView(mWidth, mHeight, mStride,
                         bwidth, bheight, bstride,
                         homographies,
                         undistorts,
                         birdeye_outbuf_ptr);
        else {
            updateLUT(mWidth, mHeight,
                      bwidth, bheight,
                      homographies,
                      LUT_ptr);
            generateBirdView(mWidth, mHeight, mStride,
                             bwidth, bheight, bstride,
                             LUT_ptr,
                             undistorts,
                             birdeye_outbuf_ptr);
        }


        return true;
    }

    bool flat2DSurround(float pw, float ph, uint32_t flatw,
            uint32_t flath, uint32_t fstride, char *flat_outbuf) {
        int bpp = 4;

        if(evsRotations.size() != 4 ||
           evsTransforms.size() != 4 ||
            Ks.size() != 4 ||
            Ds.size() != 4){
            cout << "Not valid input, as we need four camera parameters!!" << endl;
            return false;
        }

        PixelMap *LUT = nullptr;
        shared_ptr<PixelMap> LUT_ptr(new PixelMap[flath * flatw],
                    std::default_delete<PixelMap[]>());
        if(LUT_ptr != nullptr) {
            LUT = LUT_ptr.get();
            memset((void *)LUT, -1, sizeof(PixelMap) * flath * flatw);
        }

        LUT = LUT_ptr.get();
        vector<shared_ptr<char>> distorts;
        for(int index = 0; index < evsRotations.size(); index ++) {
            auto &R = evsRotations[index];
            auto &T = evsTransforms[index];
            auto &K = Ks[index];
            auto &D = Ds[index];
            double k1 = D(0,0);
            double k2 = D(0,1);
            double k3 = D(0,2);
            double k4 = D(0,3);
            double fx = K(0,0);
            double fy = K(1,1);
            double cx = K(0,2);
            double cy = K(1,2);
            double normal = R.norm();
            AngleAxisd rotation_vector(normal, R/normal);
            auto rotation_matrix = rotation_vector.toRotationMatrix();
            for(uint32_t v = 0; v < flath; v++) {
                for(uint32_t u = 0; u < flatw; u++) {
                    float x = - pw/2 + u * pw / flatw;
                    float y = ph/2 - v * ph / flath;
                    Vector3d worldPoint {x, y, 0};

                    Vector3d cameraPoint = rotation_matrix * worldPoint + T;
                    Vector3d normalizedCamera = cameraPoint/cameraPoint[2];
                    Vector3d cameraZ {0, 0, 1};
                    auto dot = cameraZ.dot(cameraPoint);
                    auto fov = acos(dot/cameraPoint.norm());
                    auto threshhold = M_PI/3;
                    // if the worldPoint beyond the camera FOV, ignore it.
                    // assume we only care FOV < 180 point
                    // a dot b = a.norm()*b.norm*cos(angle)
                    // here angle should be less than 90 degree.
                    if(dot/cameraPoint.norm() < cos(threshhold)) {
                        //cout << "No FOV in camera "<< index << ",u="\
                        //    << u << ", v=" << v << endl;
                        //cout << "cameraPoint:" << cameraPoint << endl;
                        continue;
                    }
                    Vector3d undistortPoint = K * normalizedCamera;
                    auto xc = (undistortPoint[0]/undistortPoint[2] - cx)/fx;
                    auto yc = (undistortPoint[1]/undistortPoint[2] - cy)/fy;

                    auto r = sqrt(xc*xc + yc*yc);
                    double theta = atan(r);

                    double theta_4 = k4*pow(theta, 9);
                    double theta_3 = k3*pow(theta, 7);
                    double theta_2 = k2*pow(theta, 5);
                    double theta_1 = k1*pow(theta, 3);
                    double theta_d = theta + theta_1 + theta_2 + theta_3 + theta_4;
                    double x_distorted = xc*(theta_d/r);
                    double y_distorted = yc*(theta_d/r);

                    double u_distorted = fx * x_distorted + cx;
                    double v_distorted = fy * y_distorted + cy;
                    if(u_distorted >= 0 && v_distorted >=0 && \
                            u_distorted < mWidth && v_distorted < mHeight) {
                        PixelMap *pMap = LUT + v*flatw + u;
                        if(pMap->index0 == -1) {
                            pMap->index0 = index;
                            pMap->u0 = u_distorted;
                            pMap->v0 = v_distorted;
                            pMap->fov0 = fov;
                        }
                        else if((index != pMap->index0)&&(pMap->index1 == -1)) {
                            pMap->index1 = index;
                            pMap->u1 = u_distorted;
                            pMap->v1 = v_distorted;
                            pMap->fov1 = fov;
                            auto fov0 = pMap->fov0;
                            auto alpha0 = (threshhold - fov0)/(threshhold - fov0 + threshhold - fov);
                            pMap->alpha1 = 1 - alpha0;
                            pMap->alpha0 = alpha0;
                        }
                        else if((index != pMap->index0) &&
                               (index != pMap->index1)) {
                            cout << "3 region:index="<< index <<", index0= "<< \
                                pMap->index0 << ", index1="<< pMap->index1 << endl;
                            cout << "pixel:" << u << "x" << v << endl;
                        }
                    }
                }
            }
        }

        if(prepareFisheyeImages(distorts) == false)
            return false;

        for(uint32_t v = 0; v < flath; v++) {
            for(uint32_t u = 0; u < flatw; u++) {
                PixelMap *pMap = LUT + v*flatw + u;
                if((pMap->index0 >= 0) && (pMap->index1 >= 0)) {
                    //alpha blending on overlap region
                    auto distort0 = distorts[pMap->index0].get();
                    auto distort1 = distorts[pMap->index1].get();
                    unsigned int color0 = *(int *)(( char *)distort0 + \
                            mStride * pMap->v0 + pMap->u0*bpp);
                    unsigned int color1 = *(int *)(( char *)distort1 + \
                            mStride * pMap->v1 + pMap->u1*bpp);
                    //float alpha = 0.5;
                    float alpha = pMap->alpha0;
                    unsigned char R = alpha *(color0 & 0xff0000 >> 16) + (1 - alpha)*((color1 & 0xff0000) >> 16);
                    unsigned char G = alpha *(color0 & 0xff00 >> 8) + (1- alpha)*((color1 & 0xff00) >> 8);
                    unsigned char B = alpha *(color0 & 0xff) + (1- alpha)*(color1 & 0xff);
                    color0 = (R << 16) | (G << 8) | B;
                    *(int *)((char *)flat_outbuf + fstride * ((int)v) + ((int)u)*bpp) =
                        color0;
                }
                else if(pMap->index0 >= 0) {
                    int index = pMap->index0;

                    auto distort0 = distorts[index].get();
                    unsigned int color0 = *(int *)(( char *)distort0 + \
                            mStride * pMap->v0 + pMap->u0*bpp);
                    *(int *)((char *)flat_outbuf + fstride * v + u*bpp) =
                        color0;
                }
            }
        }

        return true;
    }
};

TEST_F(ImxSV2DTest, SVLibFlatSurroundW16H12) {
    uint32_t flatw = 1024;
    uint32_t flath = 768;
    float pw = 16.0;
    float ph = 12.0;
    int bpp = 4;

    //prepare output buffer
    uint32_t fstride = flatw * bpp;
    uint32_t fsize = flath * fstride;
    char *flat_outbuf = nullptr;
    shared_ptr<char> flat_outbuf_ptr(new char[fsize],
                std::default_delete<char[]>());
    if(flat_outbuf_ptr != nullptr) {
        flat_outbuf = flat_outbuf_ptr.get();
        memset(flat_outbuf, 0, fsize);
    }

    //prepare input buffers
    vector<shared_ptr<char>> distorts;
    prepareFisheyeImages(distorts);

    Imx2DSV *imx2DSV = new Imx2DSV();
    ImxSV2DParams sv2DParams = ImxSV2DParams(Size2dInteger(mWidth, mHeight),
                                    Size2dInteger(flatw, flath),
                                    Size2dFloat(pw, ph));
    ASSERT_TRUE(imx2DSV->SetConfigs(sv2DParams, evsRotations,
                evsTransforms, Ks, Ds));
    ASSERT_TRUE(imx2DSV->startSV());
    //Get distorted fisheye images
    ASSERT_TRUE(imx2DSV->GetSVBuffer(distorts, flat_outbuf, bpp));
    ASSERT_TRUE(imx2DSV->stopSV());

    //Encoder the flat_output
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/svlib-surround-w%f-h%f.jpg", pw, ph);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    ASSERT_TRUE(encoderImage(flatw, flath, fstride, format,
                flat_outbuf, output));
    ShowImage(flatw, flath, flat_outbuf);

    delete imx2DSV;
}

TEST_F(ImxSV2DTest, FlatSurroundW16H12) {
    uint32_t flatw = 1024;
    uint32_t flath = 768;
    float pw = 16.0;
    float ph = 12.0;

    int bpp = 4;
    uint32_t fstride = flatw * bpp;
    uint32_t fsize = flath * fstride;
    char *flat_outbuf = nullptr;
    shared_ptr<char> flat_outbuf_ptr(new char[fsize],
                std::default_delete<char[]>());
    if(flat_outbuf_ptr != nullptr) {
        flat_outbuf = flat_outbuf_ptr.get();
        memset(flat_outbuf, 0, fsize);
    }

    ASSERT_TRUE(flat2DSurround(pw, ph, flatw, flath, fstride, flat_outbuf));

    //Encoder the flat_output
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/flat-surround-w%f-h%f.jpg", pw, ph);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(flatw, flath, fstride, format,
                flat_outbuf, output) == false)
        return;
    ShowImage(flatw, flath, flat_outbuf);
}

TEST_F(ImxSV2DTest, BEyeSurroundS2H4LUT) {
    float bscaler = 2.0;
    float bird_height = 4;
    int bpp = 4;
    uint32_t bwidth = mWidth * bscaler;
    uint32_t bheight = mHeight * bscaler;
    uint32_t bstride = bwidth * bpp;
    uint32_t birdeyesize = bheight * bstride;
    char *birdeye_outbuf = nullptr;
    shared_ptr<char> birdeye_outbuf_ptr(new char[birdeyesize],
                std::default_delete<char[]>());
    if(birdeye_outbuf_ptr != nullptr) {
        birdeye_outbuf = birdeye_outbuf_ptr.get();
        memset(birdeye_outbuf, 0, birdeyesize);
    }

    ASSERT_TRUE(birdeye2DSurround(bscaler, bird_height, bwidth, bheight, bstride,
                birdeye_outbuf_ptr, true));
    //Encoder the birdeye_output
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/birdeye-surround-s%f-h%f.jpg", bscaler, bird_height);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(bwidth, bheight, bstride, format,
                birdeye_outbuf, output) == false)
        return;
    ShowImage(bwidth, bheight, birdeye_outbuf);
}

TEST_F(ImxSV2DTest, BEyeSurroundS2H4) {
    float bscaler = 2.0;
    float bird_height = 4;
    int bpp = 4;
    uint32_t bwidth = mWidth * bscaler;
    uint32_t bheight = mHeight * bscaler;
    uint32_t bstride = bwidth * bpp;
    uint32_t birdeyesize = bheight * bstride;
    char *birdeye_outbuf = nullptr;
    shared_ptr<char> birdeye_outbuf_ptr(new char[birdeyesize],
                std::default_delete<char[]>());
    if(birdeye_outbuf_ptr != nullptr) {
        birdeye_outbuf = birdeye_outbuf_ptr.get();
        memset(birdeye_outbuf, 0, birdeyesize);
    }

    ASSERT_TRUE(birdeye2DSurround(bscaler, bird_height, bwidth, bheight, bstride,
                birdeye_outbuf_ptr, false));
    //Encoder the birdeye_output
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/birdeye-surround-none-lut-s%f-h%f.jpg", bscaler, bird_height);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(bwidth, bheight, bstride, format,
                birdeye_outbuf, output) == false)
        return;
    ShowImage(bwidth, bheight, birdeye_outbuf);
}

TEST_F(ImxSV2DTest, BEyeSurroundS1H4LUT) {
    float bscaler = 1.0;
    float bird_height = 4;
    int bpp = 4;
    uint32_t bwidth = mWidth * bscaler;
    uint32_t bheight = mHeight * bscaler;
    uint32_t bstride = bwidth * bpp;
    uint32_t birdeyesize = bheight * bstride;
    char *birdeye_outbuf = nullptr;
    shared_ptr<char> birdeye_outbuf_ptr(new char[birdeyesize],
                std::default_delete<char[]>());
    if(birdeye_outbuf_ptr != nullptr) {
        birdeye_outbuf = birdeye_outbuf_ptr.get();
        memset(birdeye_outbuf, 0, birdeyesize);
    }

    ASSERT_TRUE(birdeye2DSurround(bscaler, bird_height, bwidth, bheight, bstride,
                birdeye_outbuf_ptr, true));
    //Encoder the birdeye_output
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/birdeye-surround-s%f-h%f.jpg", bscaler, bird_height);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(bwidth, bheight, bstride, format,
                birdeye_outbuf, output) == false)
        return;
    ShowImage(bwidth, bheight, birdeye_outbuf);
}

TEST_F(ImxSV2DTest, BEyeSurroundS1H4) {
    float bscaler = 1.0;
    float bird_height = 4;

    int bpp = 4;
    uint32_t bwidth = mWidth * bscaler;
    uint32_t bheight = mHeight * bscaler;
    uint32_t bstride = bwidth * bpp;
    uint32_t birdeyesize = bheight * bstride;
    char *birdeye_outbuf = nullptr;
    shared_ptr<char> birdeye_outbuf_ptr(new char[birdeyesize],
                std::default_delete<char[]>());
    if(birdeye_outbuf_ptr != nullptr) {
        birdeye_outbuf = birdeye_outbuf_ptr.get();
        memset(birdeye_outbuf, 0, birdeyesize);
    }

    ASSERT_TRUE(birdeye2DSurround(bscaler, bird_height, bwidth, bheight, bstride,
                birdeye_outbuf_ptr, true));
    //Encoder the birdeye_output
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/birdeye-surround-s%f-h%f.jpg", bscaler, bird_height);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(bwidth, bheight, bstride, format,
                birdeye_outbuf, output) == false)
        return;
    ShowImage(bwidth, bheight, birdeye_outbuf);
}

TEST_F(ImxSV2DTest, RotationVector) {
    Vector3d v(1, 0, 0);

    for(int index = 0; index < evsRotations.size(); index ++) {
        auto &r = evsRotations[index];
        auto &t = evsTransforms[index];
        double normal = r.norm();

        cout << "Camera " << index << "********" << endl;
        AngleAxisd rotation_vector(normal, r/normal);
        cout << "rotation matrix=\n" << rotation_vector.matrix() << endl;
        auto rotation_matrix = rotation_vector.toRotationMatrix();
        Vector3d euler_angles = rotation_matrix.eulerAngles(2, 1, 0);
        cout << "yaw pitch roll = " << euler_angles.transpose() << endl;
        auto origin_point = rotation_matrix.inverse()*( - t);
        cout << "origin_point = \n" << origin_point << endl;

        Isometry3d T = Isometry3d::Identity();
        T.rotate(rotation_vector);
        T.pretranslate(t);
        auto cT = T.matrix();
        cout << "Transform matrix = \n" << cT << endl;
 
        Quaterniond q = Quaterniond(rotation_vector);
        cout << "quaternion from rotation vector = " << q.coeffs().transpose() << endl;
        q = Quaterniond(rotation_matrix);
        cout << "quaternion from rotation matrix = " << q.coeffs().transpose() << endl;
        auto v_rotated = q * v;
        cout << "(1,0,0) after rotation = " << v_rotated.transpose() << endl;
        cout << "should be equal to " << (q * Quaterniond(0, 1, 0, 0) * q.inverse()).coeffs().transpose() << endl;
 
        int index_next = index + 1;
        if(index == (evsRotations.size() - 1))
            index_next = 0;
 
        auto &rnext = evsRotations[index_next];
        auto &tnext = evsTransforms[index_next];
        auto rnextnormal = rnext.norm();
        AngleAxisd nextrotation_vector(rnextnormal, rnext/normal);

        cout << "rotation matrix=\n" << nextrotation_vector.matrix() << endl;
        auto nextrotation_matrix = nextrotation_vector.toRotationMatrix();
        auto betweenrotation_matrix = nextrotation_matrix * rotation_matrix.inverse();
        Vector3d betweent = tnext - betweenrotation_matrix * t; 
        cout << "Way0: Between two cameras, the rotation matrix = \n" << betweenrotation_matrix << endl;
        cout << "Way0: Between0 two cameras, the translate vector = \n" << betweent << endl;

        AngleAxisd betweenRotation0;
        betweenRotation0.fromRotationMatrix(betweenrotation_matrix);
        auto betweenrotation_vector = betweenRotation0.angle() * betweenRotation0.axis();
        cout << "Way0: Between0 rotation vector = \n" << betweenrotation_vector << endl;

        //Isometry3d betweenT = Isometry3d::Identity();
        //betweenT.rotate(betweenrotation_vector);
        //betweenT.pretranslate(betweent);
        //auto betweencT = betweenT.matrix();
        //cout << "Between Transform matrix = \n" << betweencT << endl;

        Isometry3d TNext = Isometry3d::Identity();
        TNext.rotate(nextrotation_vector);
        TNext.pretranslate(tnext);
        auto cTNext = TNext.matrix();
        cout << "Next Transform matrix = \n" << cTNext << endl;
        auto cTBetween = cTNext * cT.inverse();
        cout << "Way1: Between two cameras, the Transform matrix = \n" << cTBetween << endl;
        AngleAxisd betweenRotation1;
        betweenRotation1.fromRotationMatrix(cTBetween.topLeftCorner<3, 3>());
        auto angle = betweenRotation1.angle();
        cout << "Way1: Camera "<< index << " with "<< index_next << " angle = "<< angle << endl;

        auto dot = r.dot(rnext);
        cout << "Camera "<< index << " dot with "<< index_next << " = "<< dot << endl;
        cout << "Camera "<< index << " angle with "<< index_next << " = "<< acos(dot/normal/rnextnormal)/M_PI << "pi" << endl;

        auto cross = r.cross(rnext);
        cout << "Camera "<< index << " cross with "<< index_next << " = \n"<< cross << endl;
        cout << "Camera "<< index << " angle with "<< index_next << " = "<< asin(cross.norm()/normal/rnextnormal)/M_PI << "pi" << endl;
    }
}

class ImxSVCali1Test: public ::testing::Test
{
protected:
    virtual void SetUp() {
        K <<  608.0026093794693, 0.0,968.699544102168,
              0.0, 608.205469489769, 476.38843298898996,
              0.0, 0.0, 1.0;

        D << -0.03711481733589263,
             -0.0014805627895442888,
             -0.00030212056866592464,
             -0.00020149538570397933;

        char input[128];
        memset(input, 0, sizeof(input));
        sprintf(input, "/sdcard/%d.png", 0);
        auto retValue = getImageInfo(input, &mWidth,
                        &mHeight, &mStride);
        if(!retValue)
            return;

        cout << "Get Image: width=" << mWidth << ", height=" << mHeight << ", stride=" \
                << mStride << endl;

        mComposerClient = new SurfaceComposerClient;
        ASSERT_EQ(NO_ERROR, mComposerClient->initCheck());

    }
    
    virtual void TearDown()  {
    }

    virtual void ShowImage(uint32_t width, uint32_t height, void *pixels)  {
        mSurfaceControl = mComposerClient->createSurface(
                String8("ImxSVSurface"), width, height, PIXEL_FORMAT_RGBA_8888, 0);
        ASSERT_TRUE(mSurfaceControl != nullptr);
        ASSERT_TRUE(mSurfaceControl->isValid());

        Transaction t;
        ASSERT_EQ(NO_ERROR, t.setLayer(mSurfaceControl, 0x7fffffff)
                .show(mSurfaceControl)
                .apply());

        mSurface = mSurfaceControl->getSurface();
        ASSERT_TRUE(mSurface != nullptr);
        sp<ANativeWindow> anw(mSurface);
        ASSERT_EQ(NO_ERROR, native_window_api_connect(anw.get(), NATIVE_WINDOW_API_CPU));
        ASSERT_EQ(NO_ERROR, native_window_set_usage(anw.get(),
            GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN));

        ANativeWindowBuffer* anb;
        ASSERT_EQ(NO_ERROR, native_window_dequeue_buffer_and_wait(anw.get(),
            &anb));
        ASSERT_TRUE(anb != nullptr);

        sp<GraphicBuffer> buf(GraphicBuffer::from(anb));
        // Fill the buffer with the a checkerboard pattern
        uint8_t* img = nullptr;
        buf->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&img));
        int size =  width * height * 4;
        memcpy(img, pixels, size);
        buf->unlock();
        ASSERT_EQ(NO_ERROR, anw->queueBuffer(anw.get(), buf->getNativeBuffer(),
            -1));
        sleep(3);
    }

    sp<Surface> mSurface;
    sp<SurfaceComposerClient> mComposerClient;
    sp<SurfaceControl> mSurfaceControl;

    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    uint32_t mStride = 0;
    Matrix<double, 3, 3> K;
    Matrix<double, 1, 4> D;
};

TEST_F(ImxSVCali1Test, Image_1dot0) {
    float scaler = 1;
    uint32_t size = mHeight * mStride;
    int bpp = 4;
    uint32_t out_width = mWidth *scaler;
    uint32_t out_height = mHeight *scaler;
    uint32_t out_stride = out_width*bpp;
    size_t out_size = out_stride *out_height;

    shared_ptr<char> pixels_outbuf(new char[size],
        std::default_delete<char[]>());
    if(pixels_outbuf == nullptr)
        return;
    auto pixels = pixels_outbuf.get();
    decodeImage(pixels, size, "/sdcard/0.png");
    ShowImage(mWidth, mHeight, pixels);

    shared_ptr<char> undistort_outbuf(new char[size],
        std::default_delete<char[]>());
    if(undistort_outbuf == nullptr)
        return;
    auto undistort = undistort_outbuf.get();

    shared_ptr<char> dot_outbuf(new char[size],
        std::default_delete<char[]>());
    if(dot_outbuf == nullptr)
        return;
    auto dot = dot_outbuf.get();

    ASSERT_TRUE(calibrationImage(scaler, K, D, pixels, dot,
                mWidth, mHeight, mStride,
                undistort, out_size));

    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/1-undistorted-%f.jpg", scaler);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(out_width, out_height, out_stride, format,
            undistort, output) == false)
        return;
    ShowImage(out_width, out_height, undistort);

    char dotfile[128];
    memset(dotfile, 0, sizeof(dotfile));
    sprintf(dotfile, "/sdcard/1-distort-map-%f.jpg", scaler);
    if(encoderImage(mWidth, mHeight, mStride, format,
            dot, dotfile) == false)
        return;
    ShowImage(mWidth, mHeight, dot);
}

TEST_F(ImxSVCali1Test, Image_1dot2) {
    float scaler = 1.2;
    uint32_t size = mHeight * mStride;
    int bpp = 4;
    uint32_t out_width = mWidth *scaler;
    uint32_t out_height = mHeight *scaler;
    uint32_t out_stride = out_width*bpp;
    size_t out_size = out_stride *out_height;

    shared_ptr<char> pixels_outbuf(new char[size],
        std::default_delete<char[]>());
    if(pixels_outbuf == nullptr)
        return;
    auto pixels = pixels_outbuf.get();
    decodeImage(pixels, size, "/sdcard/0.png");
    ShowImage(mWidth, mHeight, pixels);

    shared_ptr<char> undistort_outbuf(new char[out_size],
        std::default_delete<char[]>());
    if(undistort_outbuf == nullptr)
        return;
    auto undistort = undistort_outbuf.get();

    shared_ptr<char> dot_outbuf(new char[size],
        std::default_delete<char[]>());
    if(dot_outbuf == nullptr)
        return;
    auto dot = dot_outbuf.get();

    ASSERT_TRUE(calibrationImage(scaler, K, D, pixels, dot,
                mWidth, mHeight, mStride,
                undistort, out_size));

    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/1-undistorted-%f.jpg", scaler);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(out_width, out_height, out_stride, format,
            undistort, output) == false)
        return;
    ShowImage(out_width, out_height, undistort);

    char dotfile[128];
    memset(dotfile, 0, sizeof(dotfile));
    sprintf(dotfile, "/sdcard/1-distort-map-%f.jpg", scaler);
    if(encoderImage(mWidth, mHeight, mStride, format,
            dot, dotfile) == false)
        return;
    ShowImage(mWidth, mHeight, dot);
}

TEST_F(ImxSVCali1Test, Image_2dot0) {
    float scaler = 2.0;
    uint32_t size = mHeight * mStride;
    int bpp = 4;
    uint32_t out_width = mWidth *scaler;
    uint32_t out_height = mHeight *scaler;
    uint32_t out_stride = out_width*bpp;
    size_t out_size = out_stride *out_height;

    shared_ptr<char> pixels_outbuf(new char[size],
        std::default_delete<char[]>());
    if(pixels_outbuf == nullptr)
        return;
    auto pixels = pixels_outbuf.get();
    decodeImage(pixels, size, "/sdcard/0.png");
    ShowImage(mWidth, mHeight, pixels);

    shared_ptr<char> undistort_outbuf(new char[out_size],
        std::default_delete<char[]>());
    if(undistort_outbuf == nullptr)
        return;
    auto undistort = undistort_outbuf.get();

    shared_ptr<char> dot_outbuf(new char[size],
        std::default_delete<char[]>());
    if(dot_outbuf == nullptr)
        return;
    auto dot = dot_outbuf.get();

    ASSERT_TRUE(calibrationImage(scaler, K, D, pixels, dot,
                mWidth, mHeight, mStride,
                undistort, out_size));

    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/1-undistorted-%f.jpg", scaler);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(out_width, out_height, out_stride, format,
            undistort, output) == false)
        return;
    ShowImage(out_width, out_height, undistort);

    char dotfile[128];
    memset(dotfile, 0, sizeof(dotfile));
    sprintf(dotfile, "/sdcard/1-distort-map-%f.jpg", scaler);
    if(encoderImage(mWidth, mHeight, mStride, format,
            dot, dotfile) == false)
        return;
    ShowImage(mWidth, mHeight, dot);
}

class ImxSVCali2Test: public ::testing::Test
{
protected:
    virtual void SetUp() {
        K << 421.05641911742293, 0.0, 623.1233368341417,
             0.0, 435.3963639225523, 381.33950935388606,
             0.0, 0.0, 1.0;
        D << -0.03270810830898023,
             0.058951819094913656,
             -0.07417478474484326,
             0.030368291431475524;

        auto retValue = getImageInfo("/sdcard/distort.jpg", &mWidth,
                        &mHeight, &mStride);
        if(!retValue)
            return;

        cout << "Get Image: width=" << mWidth << ", height=" << mHeight << ", stride=" \
                << mStride << endl;

        mComposerClient = new SurfaceComposerClient;
        ASSERT_EQ(NO_ERROR, mComposerClient->initCheck());
    }
    
    virtual void TearDown()  {
    }

    virtual void ShowImage(uint32_t width, uint32_t height, void *pixels)  {
        mSurfaceControl = mComposerClient->createSurface(
                String8("ImxSVSurface"), width, height, PIXEL_FORMAT_RGBA_8888, 0);
        ASSERT_TRUE(mSurfaceControl != nullptr);
        ASSERT_TRUE(mSurfaceControl->isValid());

        Transaction t;
        ASSERT_EQ(NO_ERROR, t.setLayer(mSurfaceControl, 0x7fffffff)
                .show(mSurfaceControl)
                .apply());

        mSurface = mSurfaceControl->getSurface();
        ASSERT_TRUE(mSurface != nullptr);
        sp<ANativeWindow> anw(mSurface);
        ASSERT_EQ(NO_ERROR, native_window_api_connect(anw.get(), NATIVE_WINDOW_API_CPU));
        ASSERT_EQ(NO_ERROR, native_window_set_usage(anw.get(),
            GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN));

        ANativeWindowBuffer* anb;
        ASSERT_EQ(NO_ERROR, native_window_dequeue_buffer_and_wait(anw.get(),
            &anb));
        ASSERT_TRUE(anb != nullptr);

        sp<GraphicBuffer> buf(GraphicBuffer::from(anb));
        // Fill the buffer with the a checkerboard pattern
        uint8_t* img = nullptr;
        buf->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&img));
        int size =  width * height * 4;
        memcpy(img, pixels, size);
        buf->unlock();
        ASSERT_EQ(NO_ERROR, anw->queueBuffer(anw.get(), buf->getNativeBuffer(),
            -1));
        sleep(3);
    }

    sp<Surface> mSurface;
    sp<SurfaceComposerClient> mComposerClient;
    sp<SurfaceControl> mSurfaceControl;

    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    uint32_t mStride = 0;
    Matrix<double, 3, 3> K;
    Matrix<double, 1, 4> D;
};

TEST_F(ImxSVCali2Test, Image_1dot0) {
    float scaler = 1;
    uint32_t size = mHeight * mStride;
    int bpp = 4;
    uint32_t out_width = mWidth *scaler;
    uint32_t out_height = mHeight *scaler;
    uint32_t out_stride = out_width*bpp;
    size_t out_size = out_stride *out_height;

    shared_ptr<char> pixels_outbuf(new char[size],
        std::default_delete<char[]>());
    if(pixels_outbuf == nullptr)
        return;
    auto pixels = pixels_outbuf.get();
    decodeImage(pixels, size, "/sdcard/distort.jpg");
    ShowImage(mWidth, mHeight, pixels);

    shared_ptr<char> undistort_outbuf(new char[out_size],
        std::default_delete<char[]>());
    if(undistort_outbuf == nullptr)
        return;
    auto undistort = undistort_outbuf.get();

    shared_ptr<char> dot_outbuf(new char[size],
        std::default_delete<char[]>());
    if(dot_outbuf == nullptr)
        return;
    auto dot = dot_outbuf.get();

    ASSERT_TRUE(calibrationImage(scaler, K, D, pixels, dot,
                mWidth, mHeight, mStride,
                undistort, out_size));

    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/2-undistorted-%f.jpg", scaler);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(out_width, out_height, out_stride, format,
            undistort, output) == false)
        return;
    ShowImage(out_width, out_height, undistort);

    char dotfile[128];
    memset(dotfile, 0, sizeof(dotfile));
    sprintf(dotfile, "/sdcard/2-distort-map-%f.jpg", scaler);
    if(encoderImage(mWidth, mHeight, mStride, format,
            dot, dotfile) == false)
        return;
    ShowImage(mWidth, mHeight, dot);
}

TEST_F(ImxSVCali2Test, Image_1dot2) {
    float scaler = 1.2;
    uint32_t size = mHeight * mStride;
    int bpp = 4;
    uint32_t out_width = mWidth *scaler;
    uint32_t out_height = mHeight *scaler;
    uint32_t out_stride = out_width*bpp;
    size_t out_size = out_stride *out_height;

    shared_ptr<char> pixels_outbuf(new char[size],
        std::default_delete<char[]>());
    if(pixels_outbuf == nullptr)
        return;
    auto pixels = pixels_outbuf.get();
    decodeImage(pixels, size, "/sdcard/distort.jpg");
    ShowImage(mWidth, mHeight, pixels);

    shared_ptr<char> undistort_outbuf(new char[out_size],
        std::default_delete<char[]>());
    if(undistort_outbuf == nullptr)
        return;
    auto undistort = undistort_outbuf.get();

    shared_ptr<char> dot_outbuf(new char[size],
        std::default_delete<char[]>());
    if(dot_outbuf == nullptr)
        return;
    auto dot = dot_outbuf.get();

    ASSERT_TRUE(calibrationImage(scaler, K, D, pixels, dot,
                mWidth, mHeight, mStride,
                undistort, out_size));

    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/2-undistorted-%f.jpg", scaler);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(out_width, out_height, out_stride, format,
            undistort, output) == false)
        return;
    ShowImage(out_width, out_height, undistort);

    char dotfile[128];
    memset(dotfile, 0, sizeof(dotfile));
    sprintf(dotfile, "/sdcard/2-distort-map-%f.jpg", scaler);
    if(encoderImage(mWidth, mHeight, mStride, format,
            dot, dotfile) == false)
        return;
    ShowImage(mWidth, mHeight, dot);
}

TEST_F(ImxSVCali2Test, Image_2dot0) {
    float scaler = 2;
    uint32_t size = mHeight * mStride;
    int bpp = 4;
    uint32_t out_width = mWidth *scaler;
    uint32_t out_height = mHeight *scaler;
    uint32_t out_stride = out_width*bpp;
    size_t out_size = out_stride *out_height;

    shared_ptr<char> pixels_outbuf(new char[size],
        std::default_delete<char[]>());
    if(pixels_outbuf == nullptr)
        return;
    auto pixels = pixels_outbuf.get();
    decodeImage(pixels, size, "/sdcard/distort.jpg");
    ShowImage(mWidth, mHeight, pixels);

    shared_ptr<char> undistort_outbuf(new char[out_size],
        std::default_delete<char[]>());
    if(undistort_outbuf == nullptr)
        return;
    auto undistort = undistort_outbuf.get();

    shared_ptr<char> dot_outbuf(new char[size],
        std::default_delete<char[]>());
    if(dot_outbuf == nullptr)
        return;
    auto dot = dot_outbuf.get();

    ASSERT_TRUE(calibrationImage(scaler, K, D, pixels, dot,
                mWidth, mHeight, mStride,
                undistort, out_size));

    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/2-undistorted-%f.jpg", scaler);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(out_width, out_height, out_stride, format,
            undistort, output) == false)
        return;
    ShowImage(out_width, out_height, undistort);

    char dotfile[128];
    memset(dotfile, 0, sizeof(dotfile));
    sprintf(dotfile, "/sdcard/2-distort-map-%f.jpg", scaler);
    if(encoderImage(mWidth, mHeight, mStride, format,
            dot, dotfile) == false)
        return;
    ShowImage(mWidth, mHeight, dot);
}

