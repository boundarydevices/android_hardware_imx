#define LOG_TAG "image-test"

#include <android/data_space.h>
#include <android/bitmap.h>
#include <android/imagedecoder.h>
#include <math.h>
#include <cutils/log.h>
#include <gtest/gtest.h>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <Imx2DSurroundView.hpp>

using namespace std;
using namespace Eigen;
using namespace imx;

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

static bool getImageInfo(const char *input, uint32_t *width,
                    uint32_t *height, uint32_t *stride);
static bool calibrationImage(float scaler,  Matrix<double, 3, 3> &K, Matrix<double, 1, 4> &D,
                    const char *input_file, const char *output_file, const char *dotfile,
                    void *output_buf = nullptr, size_t outsize = 0);
static bool prepareFisheyeImages(vector<shared_ptr<char>> &distorts);

static bool getImageInfo(const char *input, uint32_t *width, uint32_t *height, uint32_t *stride) {
    int fd = -1;
    int result = -1;
    AImageDecoder* decoder;
    const AImageDecoderHeaderInfo* info;

    fd = open(input, O_RDWR, O_RDONLY);
    if (fd < 0) {
        ALOGE("Unable to open file [%s]",
             input);
        return false;
    }
 
    result = AImageDecoder_createFromFd(fd, &decoder);
    if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
        // An error occurred, and the file could not be decoded.
        ALOGE("Not a valid image file [%s]",
             input);
        close(fd);
        return false;
    }
    info = AImageDecoder_getHeaderInfo(decoder);
    *width = AImageDecoderHeaderInfo_getWidth(info);
    *height = AImageDecoderHeaderInfo_getHeight(info);
    *stride = AImageDecoder_getMinimumStride(decoder); 

    AImageDecoder_delete(decoder);

    if(fd >=0)
        close(fd);
    return true;
}

static bool encoderImage(uint32_t width, uint32_t height, 
                uint32_t stride, AndroidBitmapFormat format,
                char *image, const char *name)
{
    //Encoder the flat_output
    AndroidBitmapInfo bpinfo = {
         .flags = 0,
         .format = format,
         .height = height,
         .width = width,
         .stride = stride,
    };

     int outfd = open(name, O_CREAT | O_RDWR, 0666);
     if (outfd < 0) {
         ALOGE("Unable to open out file [%s]",
                 name);
         return false;
     }

     auto fn = [](void *userContext, const void *data, size_t size) -> bool {
        if((userContext == nullptr) || (data == nullptr) || (size == 0)) {
            ALOGE("Error on encoder!");
            return false;
        }
        int fd = *(int *)userContext;
        int len = 0;
        len = write(fd, data, size);
        return true;
     };

     int result = -1;
     result = AndroidBitmap_compress(&bpinfo, ADATASPACE_SCRGB_LINEAR,
                image, ANDROID_BITMAP_COMPRESS_FORMAT_JPEG,
                100, &outfd, fn);
     if (result != ANDROID_BITMAP_RESULT_SUCCESS ) {
         ALOGE("Error on encoder return %d!", result);
         return false;
     }
     if(outfd >=0)
         close(outfd);
     return true;
}

static bool calibrationImage(float scaler,  Matrix<double, 3, 3> &K, Matrix<double, 1, 4> &D,
                    const char *input_file, const char *output_file, const char *dotfile,
                    void *output_buf, size_t outsize) {
    //Read image
    int fd = -1;
    int outfd = -1;
    int result = -1;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    size_t size;
    void* pixels = nullptr;
    void* blended_pixels = nullptr;
    AImageDecoder* decoder;
    AndroidBitmapFormat format;
    const AImageDecoderHeaderInfo* info;

    fd = open(input_file, O_RDWR, O_RDONLY);
    if (fd < 0) {
        ALOGE("Unable to open file [%s]",
             input_file);
        return false;
    }
 
    result = AImageDecoder_createFromFd(fd, &decoder);
    if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
        // An error occurred, and the file could not be decoded.
        ALOGE("Not a valid image file [%s]",
             input_file);
        close(fd);
        return false;
    }
    info = AImageDecoder_getHeaderInfo(decoder);
    width = AImageDecoderHeaderInfo_getWidth(info);
    height = AImageDecoderHeaderInfo_getHeight(info);
    AImageDecoder_setAndroidBitmapFormat(decoder, ANDROID_BITMAP_FORMAT_RGBA_8888);
    format =
           (AndroidBitmapFormat) AImageDecoderHeaderInfo_getAndroidBitmapFormat(info);
    stride = AImageDecoder_getMinimumStride(decoder); 
    size = height * stride;
    pixels = malloc(size);

    ALOGI("Image: %d x %d, stride %u, format %d", width, height, stride, format);

    result = AImageDecoder_decodeImage(decoder, pixels, stride, size);
    if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
        // An error occurred, and the file could not be decoded.
        ALOGE("file to decode the image [%s]",
             input_file);
        AImageDecoder_delete(decoder);
        close(fd);
        return false;
    }

    if(dotfile != nullptr) {
        blended_pixels = malloc(size);
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
    if(output_buf != nullptr)
        if(outsize >= out_size)
            pbuf = (char *)output_buf; 
        else
            ALOGE("Not good! no enough buffer");
    else {
        pbuf = (char *)malloc(out_size);
        memset(pbuf, 0, out_size);
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

                if(dotfile) {
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

    auto fn = [](void *userContext, const void *data, size_t size) -> bool {
        if((userContext == nullptr) || (data == nullptr) || (size == 0)) {
            ALOGE("Error on encoder!");
            return false;
        }
        int fd = *(int *)userContext;
        int len = 0;
        len = write(fd, data, size);
        return true;
    };

    if(output_file != nullptr){
        AndroidBitmapInfo bpinfo = {
            .flags = 0,
            .format = format,
            .height = out_height,
            .width = out_width,
            .stride = out_stride,
        };
        outfd = open(output_file, O_CREAT | O_RDWR, 0666);
        if (outfd < 0) {
            ALOGE("Unable to open out file [%s]",
                    output_file);
            AImageDecoder_delete(decoder);
            close(fd);
            return false;
        }

        result = AndroidBitmap_compress(&bpinfo, ADATASPACE_SCRGB_LINEAR,
                pbuf, ANDROID_BITMAP_COMPRESS_FORMAT_JPEG, 100, &outfd, fn);
        if (result != ANDROID_BITMAP_RESULT_SUCCESS ) {
            ALOGE("Error on encoder return %d!", result);
        }
    }

    if(dotfile != nullptr) {
        AndroidBitmapInfo bpinfo = {
            .flags = 0,
            .format = format,
            .height = height,
            .width = width,
            .stride = stride,
        };

        int alpha_outfd = open(dotfile, O_CREAT | O_RDWR, 0666);
        if (alpha_outfd < 0) {
            ALOGE("Unable to open out file [%s]",
                    dotfile);
            AImageDecoder_delete(decoder);
            close(outfd);
            close(fd);
            return false;
        }

        result = AndroidBitmap_compress(&bpinfo, ADATASPACE_SCRGB_LINEAR,
                blended_pixels, ANDROID_BITMAP_COMPRESS_FORMAT_JPEG, 100, &alpha_outfd, fn);
        if (result != ANDROID_BITMAP_RESULT_SUCCESS ) {
            ALOGE("Error on encoder return %d!", result);
        }
        if(blended_pixels != nullptr)
            free(blended_pixels);
        if(alpha_outfd >=0)
            close(alpha_outfd);
    }

    // We’re done with the decoder, so now it’s safe to delete it.
    AImageDecoder_delete(decoder);

    // Free the pixels when done drawing with them
    if(pixels != nullptr)
        free(pixels);

    if(output_buf == nullptr)
        if(pbuf != nullptr)
            free(pbuf);

    if(fd >=0)
        close(fd);

    if(outfd >=0)
        close(outfd);

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

static bool flat2DSurround(float pw, float ph, uint32_t flatw, uint32_t flath,
                vector<Vector3d> &evsRotations, vector<Vector3d> &evsTransforms,
                vector<Matrix<double, 3, 3>> &Ks, vector<Matrix<double, 1, 4>> &Ds)
{
    bool retValue;
    int bpp = 4;

    if(evsRotations.size() != 4 ||
        evsTransforms.size() != 4 ||
        Ks.size() != 4 ||
        Ds.size() != 4){
        cout << "Not valid input, as we need four camera parameters!!" << endl;
        return false;
    }

    char input[128];
    memset(input, 0, sizeof(input));
    sprintf(input, "/sdcard/%d.png", 0);
    uint32_t width = 0, height=0, stride=0;
    retValue = getImageInfo(input, &width,
                    &height, &stride);
    if(!retValue)
        return retValue;

    cout << "Get Image: width=" << width << ", height=" << height << ", stride=" \
            << stride << endl;

    uint32_t fstride = flatw * bpp;
    uint32_t fsize = flath * fstride;
    char *flat_outbuf = nullptr;
    shared_ptr<char> flat_outbuf_ptr(new char[fsize],
                std::default_delete<char[]>());
    if(flat_outbuf_ptr != nullptr) {
        flat_outbuf = flat_outbuf_ptr.get();
        memset(flat_outbuf, 0, fsize);
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
                        u_distorted < width && v_distorted < height) {
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
                        stride * pMap->v0 + pMap->u0*bpp);
                unsigned int color1 = *(int *)(( char *)distort1 + \
                        stride * pMap->v1 + pMap->u1*bpp);
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
                        stride * pMap->v0 + pMap->u0*bpp);
                *(int *)((char *)flat_outbuf + fstride * v + u*bpp) =
                    color0;
            }
        }
    }

    //Encoder the flat_output
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/flat-surround-w%f-h%f.jpg", pw, ph);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(flatw, flath, fstride, format,
                flat_outbuf, output) == false)
        return false;

    return true;
}


static bool birdeye2DSurround(float bscaler, float bird_height,
            Matrix<double, 3, 3> &Kb,
            vector<Vector3d> &evsRotations, vector<Vector3d> &evsTransforms,
            vector<Matrix<double, 3, 3>> &Ks, vector<Matrix<double, 1, 4>> &Ds,
            bool lut = false) {
    bool retValue;
    int bpp = 4;
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

    char input[128];
    memset(input, 0, sizeof(input));
    sprintf(input, "/sdcard/%d.png", 0);
    uint32_t width = 0, height=0, stride=0;
    retValue = getImageInfo(input, &width,
                    &height, &stride);
    if(!retValue)
        return retValue;

    cout << "Get Image: width=" << width << ", height=" << height << ", height=" << height << endl;
    uint32_t size = height * stride;

    uint32_t bwidth = width * bscaler;
    uint32_t bheight = height * bscaler;
    uint32_t bstride = bwidth * bpp;
    uint32_t birdeyesize = bheight * bstride;
    char *birdeye_outbuf = nullptr;
    shared_ptr<char> birdeye_outbuf_ptr(new char[birdeyesize], 
                std::default_delete<char[]>());
    if(birdeye_outbuf_ptr != nullptr) {
        birdeye_outbuf = birdeye_outbuf_ptr.get();
        memset(birdeye_outbuf, 0, birdeyesize);
    }

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

        shared_ptr<char> undistort_outbuf(new char[size], 
                std::default_delete<char[]>());
        if(undistort_outbuf == nullptr)
            return false;

        retValue = calibrationImage(scaler, K, D, input, 
                nullptr, nullptr, undistort_outbuf.get(), size);

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
        generateBirdView(width, height, stride,
                     bwidth, bheight, bstride,
                     homographies,
                     undistorts,
                     birdeye_outbuf_ptr);
    else {
        updateLUT(width, height,
                  bwidth, bheight,
                  homographies,
                  LUT_ptr);
        generateBirdView(width, height, stride,
                         bwidth, bheight, bstride,
                         LUT_ptr,
                         undistorts,
                         birdeye_outbuf_ptr);
    }

    //Encoder the birdeye_output
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/birdeye-surround-s%f-h%f.jpg", bscaler, bird_height);
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    if(encoderImage(bwidth, bheight, bstride, format,
                birdeye_outbuf, output) == false)
        return false;
    
    return true;
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
        AImageDecoder* decoder;
        AndroidBitmapFormat format;
        const AImageDecoderHeaderInfo* info;

        char input[128];
        memset(input, 0, sizeof(input));
        sprintf(input, "/sdcard/%d.png", index);
        auto fd = open(input, O_RDWR, O_RDONLY);
        if (fd < 0) {
            ALOGE("Unable to open file [%s]",
                 input);
            return false;
        }
 
        auto result = AImageDecoder_createFromFd(fd, &decoder);
        if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
            // An error occurred, and the file could not be decoded.
            ALOGE("Not a valid image file [%s]",
                 input);
            close(fd);
            return false;
        }
        info = AImageDecoder_getHeaderInfo(decoder);
        width = AImageDecoderHeaderInfo_getWidth(info);
        height = AImageDecoderHeaderInfo_getHeight(info);
        AImageDecoder_setAndroidBitmapFormat(decoder, ANDROID_BITMAP_FORMAT_RGBA_8888);
        format =
               (AndroidBitmapFormat) AImageDecoderHeaderInfo_getAndroidBitmapFormat(info);
        stride = AImageDecoder_getMinimumStride(decoder);
        auto size = height * stride;
        shared_ptr<char> pixels_outbuf(new char[size],
                std::default_delete<char[]>());
        if(pixels_outbuf == nullptr)
            return false;
        auto pixels = pixels_outbuf.get();
        ALOGI("Image: %d x %d, stride %u, format %d", width, height, stride, format);

        result = AImageDecoder_decodeImage(decoder, pixels, stride, size);
        if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
            // An error occurred, and the file could not be decoded.
            ALOGE("file to decode the image [%s]",
                 input);
            AImageDecoder_delete(decoder);
            close(fd);
            return false;
        }

        distorts.push_back(pixels_outbuf);
        if(fd >=0)
            close(fd);
    }
    return true;
}

TEST(ImxSV, SVLibFlatSurroundW16H12) {
    uint32_t flatw = 1024;
    uint32_t flath = 768;
    float pw = 16.0;
    float ph = 12.0;
    int bpp = 4;

    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;

    initCameraParameters(evsRotations, evsTransforms,
        Ks, Ds);

    char input[128];
    memset(input, 0, sizeof(input));
    sprintf(input, "/sdcard/%d.png", 0);
    uint32_t width = 0, height=0, stride=0;
    auto retValue = getImageInfo(input, &width,
                    &height, &stride);
    if(!retValue)
        return;

    cout << "Get Image: width=" << width << ", height=" << height << ", stride=" \
            << stride << endl;

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
    ImxSV2DParams sv2DParams = ImxSV2DParams(Size2dInteger(width, height),
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

    delete imx2DSV;
}

TEST(ImxSV, FlatSurroundW16H12) {
    uint32_t flatw = 1024;
    uint32_t flath = 768;
    float pw = 16.0;
    float ph = 12.0;

    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;

    initCameraParameters(evsRotations, evsTransforms,
        Ks, Ds);
    ASSERT_TRUE(flat2DSurround(pw, ph, flatw, flath,
         evsRotations, evsTransforms,
         Ks, Ds));
}

TEST(ImxSV, BEyeSurroundS2H8) {
    float bscaler = 2.0;
    float bird_height = 8;
    Matrix<double, 3, 3> Kb;
    Kb <<  607.8691721095306, 0.0,975.5686146375716,
               0.0, 608.0112887189435, 481.1938786570715,
               0.0, 0.0, 1.0;
    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;

    initCameraParameters(evsRotations, evsTransforms,
        Ks, Ds);

    ASSERT_TRUE(birdeye2DSurround(bscaler, bird_height, Kb,
         evsRotations, evsTransforms,
         Ks, Ds));
}

TEST(ImxSV, BEyeSurroundS2H4) {
    float bscaler = 2.0;
    float bird_height = 4;
    Matrix<double, 3, 3> Kb;
    Kb <<  607.8691721095306, 0.0,975.5686146375716,
               0.0, 608.0112887189435, 481.1938786570715,
               0.0, 0.0, 1.0;
    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;

    initCameraParameters(evsRotations, evsTransforms,
        Ks, Ds);

    ASSERT_TRUE(birdeye2DSurround(bscaler, bird_height, Kb,
         evsRotations, evsTransforms,
         Ks, Ds));
}

TEST(ImxSV, BEyeSurroundS1H4LUT) {
    float bscaler = 1.0;
    float bird_height = 4;
    Matrix<double, 3, 3> Kb;
    Kb <<  607.8691721095306, 0.0,975.5686146375716,
               0.0, 608.0112887189435, 481.1938786570715,
               0.0, 0.0, 1.0;
    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;

    initCameraParameters(evsRotations, evsTransforms,
        Ks, Ds);

    ASSERT_TRUE(birdeye2DSurround(bscaler, bird_height, Kb,
         evsRotations, evsTransforms,
         Ks, Ds, true));
}

TEST(ImxSV, BEyeSurroundS1H4) {
    float bscaler = 1.0;
    float bird_height = 4;
    Matrix<double, 3, 3> Kb;
    Kb <<  607.8691721095306, 0.0,975.5686146375716,
               0.0, 608.0112887189435, 481.1938786570715,
               0.0, 0.0, 1.0;
    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;

    initCameraParameters(evsRotations, evsTransforms,
        Ks, Ds);

    ASSERT_TRUE(birdeye2DSurround(bscaler, bird_height, Kb,
         evsRotations, evsTransforms,
         Ks, Ds));
}

TEST(ImxSV, BEyeSurroundS1H8) {
    float bscaler = 1.0;
    float bird_height = 8;
    Matrix<double, 3, 3> Kb;
    Kb <<  607.8691721095306, 0.0,975.5686146375716,
               0.0, 608.0112887189435, 481.1938786570715,
               0.0, 0.0, 1.0;
    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;

    initCameraParameters(evsRotations, evsTransforms,
        Ks, Ds);

    ASSERT_TRUE(birdeye2DSurround(bscaler, bird_height, Kb,
         evsRotations, evsTransforms,
         Ks, Ds));
}

TEST(ImxSV, RotationVector) {
    Vector3d v(1, 0, 0);

    vector<Vector3d> evsRotations {
        {2.26308, 0.0382788, -0.0220549},
        {1.67415, -1.74075, 0.789399},
        {-0.106409, -2.83697, 1.28629},
        {1.63019, 1.76475, -0.827941}
    };
    vector<Vector3d> evsTransforms {
        {-7.8028875403817685e-02, 1.4537396465103221e+00, -8.4197165554645001e-02},
        {2.9715052384687407e-01, 1.1407102692699396e+00, 3.0074545273489206e-01},
        {1.7115269161259747e-01, 1.4376160762596599e+00, -1.9028844233159006e-02},
        {-3.0842691427126512e-01, 1.0884122033556984e+00, 3.4419058255954926e-01}
    };

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

TEST(ImxCalibration, Image_Google_1dot0) {
    Matrix<double, 3, 3> K;
    K <<  608.0026093794693, 0.0,968.699544102168,
           0.0, 608.205469489769, 476.38843298898996,
           0.0, 0.0, 1.0;

    Matrix<double, 1, 4> D;
    D << -0.03711481733589263,
          -0.0014805627895442888,
          -0.00030212056866592464,
          -0.00020149538570397933;

    float scaler = 1;
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/google-1-undistorted-%f.jpg", scaler);

    char dotfile[128];
    memset(dotfile, 0, sizeof(dotfile));
    sprintf(dotfile, "/sdcard/google-1-distort-map-%f.jpg", scaler);

    ASSERT_TRUE(calibrationImage(scaler, K, D, "/sdcard/1.png", output, dotfile));
}

TEST(ImxCalibration, Image_Google_1dot2) {
    Matrix<double, 3, 3> K;
    K <<  608.0026093794693, 0.0,968.699544102168,
           0.0, 608.205469489769, 476.38843298898996,
           0.0, 0.0, 1.0;

    Matrix<double, 1, 4> D;
    D << -0.03711481733589263,
          -0.0014805627895442888,
          -0.00030212056866592464,
          -0.00020149538570397933;

    float scaler = 1.2;
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/google-1-undistorted-%f.jpg", scaler);

    char dotfile[128];
    memset(dotfile, 0, sizeof(dotfile));
    sprintf(dotfile, "/sdcard/google-1-distort-map-%f.jpg", scaler);

    ASSERT_TRUE(calibrationImage(scaler, K, D, "/sdcard/1.png", output, dotfile));
}

TEST(ImxCalibration, Image_Google_2dot0) {
    Matrix<double, 3, 3> K;
    K <<  608.0026093794693, 0.0,968.699544102168,
           0.0, 608.205469489769, 476.38843298898996,
           0.0, 0.0, 1.0;

    Matrix<double, 1, 4> D;
    D << -0.03711481733589263,
          -0.0014805627895442888,
          -0.00030212056866592464,
          -0.00020149538570397933;

    float scaler = 2.0;
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/google-1-undistorted-%f.jpg", scaler);

    char dotfile[128];
    memset(dotfile, 0, sizeof(dotfile));
    sprintf(dotfile, "/sdcard/google-1-distort-map-%f.jpg", scaler);

    ASSERT_TRUE(calibrationImage(scaler, K, D, "/sdcard/1.png", output, dotfile));
}

TEST(ImxCalibration, Image_1dot0) {
    Matrix<double, 3, 3> K;
    K <<  421.05641911742293, 0.0, 623.1233368341417,
          0.0, 435.3963639225523, 381.33950935388606,
          0.0, 0.0, 1.0;

    Matrix<double, 1, 4> D;
    D << -0.03270810830898023,
         0.058951819094913656,
         -0.07417478474484326,
         0.030368291431475524;


    float scaler = 1;
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/undistorted-%f.jpg", scaler);

    char dotfile[128];
    memset(dotfile, 0, sizeof(dotfile));
    sprintf(dotfile, "/sdcard/distort-map-%f.jpg", scaler);

    ASSERT_TRUE(calibrationImage(scaler, K, D, "/sdcard/distort.jpg", output, dotfile));
    calibrationImage(scaler, K, D, "/sdcard/distort.jpg", output, dotfile);
}

TEST(ImxCalibration, Image_1dot2) {
    Matrix<double, 3, 3> K;
    K <<  421.05641911742293, 0.0, 623.1233368341417,
          0.0, 435.3963639225523, 381.33950935388606,
          0.0, 0.0, 1.0;

    Matrix<double, 1, 4> D;
    D << -0.03270810830898023,
         0.058951819094913656,
         -0.07417478474484326,
         0.030368291431475524;


    float scaler = 1.2;
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/undistorted-%f.jpg", scaler);

    char dotfile[128];
    memset(dotfile, 0, sizeof(dotfile));
    sprintf(dotfile, "/sdcard/distort-map-%f.jpg", scaler);

    ASSERT_TRUE(calibrationImage(scaler, K, D, "/sdcard/distort.jpg", output, dotfile));
    calibrationImage(scaler, K, D, "/sdcard/distort.jpg", output, dotfile);
}

TEST(ImxCalibration, Image_2dot0) {
    Matrix<double, 3, 3> K;
    K <<  421.05641911742293, 0.0, 623.1233368341417,
          0.0, 435.3963639225523, 381.33950935388606,
          0.0, 0.0, 1.0;

    Matrix<double, 1, 4> D;
    D << -0.03270810830898023,
         0.058951819094913656,
         -0.07417478474484326,
         0.030368291431475524;


    float scaler = 2;
    char output[128];
    memset(output, 0, sizeof(output));
    sprintf(output, "/sdcard/undistorted-%f.jpg", scaler);

    char dotfile[128];
    memset(dotfile, 0, sizeof(dotfile));
    sprintf(dotfile, "/sdcard/distort-map-%f.jpg", scaler);
    ASSERT_TRUE(calibrationImage(scaler, K, D, "/sdcard/distort.jpg", output, dotfile));

    ASSERT_TRUE(calibrationImage(scaler, K, D, "/sdcard/distort.jpg", output, dotfile));
    calibrationImage(scaler, K, D, "/sdcard/distort.jpg", output, dotfile);
}

