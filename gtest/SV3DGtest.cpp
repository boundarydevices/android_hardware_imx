/*
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <gtest/gtest.h>
#include <EGL/egl.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <ModelLoader/ModelLoader.hpp>

#include <WindowSurface.h>
#include <EGLUtils.h>
#include "ImageUtils.h"
#include "Imx3DView.hpp"
#include "Imx3DGrid.hpp"

using namespace android;
using namespace imx;

const char *getEGLError(void) {
    switch (eglGetError()) {
        case EGL_SUCCESS:
            return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:
            return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:
            return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:
            return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:
            return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONTEXT:
            return "EGL_BAD_CONTEXT";
        case EGL_BAD_CONFIG:
            return "EGL_BAD_CONFIG";
        case EGL_BAD_CURRENT_SURFACE:
            return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY:
            return "EGL_BAD_DISPLAY";
        case EGL_BAD_SURFACE:
            return "EGL_BAD_SURFACE";
        case EGL_BAD_MATCH:
            return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER:
            return "EGL_BAD_PARAMETER";
        case EGL_BAD_NATIVE_PIXMAP:
            return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW:
            return "EGL_BAD_NATIVE_WINDOW";
        case EGL_CONTEXT_LOST:
            return "EGL_CONTEXT_LOST";
        default:
            return "Unknown error";
    }
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
        decodeImage(pixels, size, input);
        ALOGI("Image %s: %d x %d, stride %u, format %d, pixels %p",
                input, width, height, stride, format, pixels);

        distorts.push_back(pixels_outbuf);
    }
    return true;
}

static void initCameraParameters(vector<Vector3d> &evsRotations, vector<Vector3d> &evsTransforms,
        vector<Matrix<double, 3, 3>> &Ks, vector<Matrix<double, 1, 4>> &Ds)
{
#ifdef ENABLE_IMX_SV
    evsRotations = {
        {1.81080639, -0.01254808, 0.22454604},
        {1.56411997, 1.69873618, -0.66798416},
        {-0.12984119, 2.49551644, -1.74079733},
        {1.65667285, -1.56078896, 1.33886394}
    };
    evsTransforms = {
        {0.00236236, 0.33980279, -0.05790999},
        {-0.05558801, 0.38353284, 0.03021416},
        {-0.02859015, 0.33999272, -0.05377325},
        {-0.04265574, 0.36310098, -0.00825252}
    };
    Matrix<double, 3, 3> K0;
    K0 <<  435.92451924, 0.0, 646.57033235,
           0.0, 434.35194308, 390.30687001,
           0.0, 0.0, 1.0;
    Ks.push_back(K0);
    Matrix<double, 3, 3> K1;
    K1 <<  435.92451924, 0.0, 646.57033235,
           0.0, 434.35194308, 390.30687001,
           0.0, 0.0, 1.0;
    Ks.push_back(K1);
    Matrix<double, 3, 3> K2;
    K2 <<  435.92451924, 0.0, 646.57033235,
           0.0, 434.35194308, 390.30687001,
           0.0, 0.0, 1.0;
    Ks.push_back(K2);
    Matrix<double, 3, 3> K3;
    K3 <<  435.92451924, 0.0, 646.57033235,
           0.0, 434.35194308, 390.30687001,
           0.0, 0.0, 1.0;
    Ks.push_back(K3);

    Matrix<double, 1, 4> D0;
    D0 << -0.017278829037307854,
          -0.008386205507497259,
          0.007214656916952731,
          -0.003014981423176122;
    Ds.push_back(D0);
    Matrix<double, 1, 4> D1;
    D1 << -0.017278829037307854,
          -0.008386205507497259,
          0.007214656916952731,
          -0.003014981423176122;
    Ds.push_back(D1);
    Matrix<double, 1, 4> D2;
    D2 << -0.017278829037307854,
          -0.008386205507497259,
          0.007214656916952731,
          -0.003014981423176122;
    Ds.push_back(D2);
    Matrix<double, 1, 4> D3;
    D3 << -0.017278829037307854,
          -0.008386205507497259,
          0.007214656916952731,
          -0.003014981423176122;
    Ds.push_back(D3);
#else
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
#endif
}

#define SV_X_STEP (0.2)
#define SV_Z_NOP (16)
#define SV_ANGLES_IN_PI (64)
#ifdef ENABLE_IMX_SV
#define SV_RADIUS (0.75)
#else
#define SV_RADIUS (4.0)
#endif

#define CAR_ORIENTATION_X 90.0f 
#define CAR_ORIENTATION_Y 180.0f
#define CAR_ORIENTATION_Z 0.0f
#define SV_ORIENTATION_X 0.0f 
#define SV_ORIENTATION_Y 0.0f
#define SV_ORIENTATION_Z 0.0f
#define CAM_LIMIT_RY_MIN -1.57f
#define CAM_LIMIT_RY_MAX 0.0f
#define CAM_LIMIT_ZOOM_MIN -11.5f
#define CAM_LIMIT_ZOOM_MAX -2.5f

static int getMashes(CurvilinearGrid &grid, float** gl_grid, int camera)
{
	int sum_num = 0;
	sum_num = grid.getMashes(gl_grid, camera);
	return sum_num;
}

static int getGrids(float** gl_grid, uint32_t w, uint32_t h)
{
	int sum_num = 0;
    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;

    initCameraParameters(evsRotations, evsTransforms,
        Ks, Ds);

	auto grid = CurvilinearGrid(SV_ANGLES_IN_PI, SV_Z_NOP, SV_X_STEP,
                                w, h, evsRotations, evsTransforms, Ks, Ds);
	grid.createGrid(SV_RADIUS); // Calculate grid points
	sum_num = grid.getGrids(gl_grid);

	return sum_num;
}

TEST(ImxSV, 3DSurroundViewGrids) {
    EGLint majorVersion;
    EGLint minorVersion;
    EGLSurface surface;
    EGLint w, h;
    EGLDisplay dpy;

    WindowSurface windowSurface;
    EGLNativeWindowType window = windowSurface.getSurface();

    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        ALOGE("Failed to get egl display");
        return;
    }

    if(!eglInitialize(dpy, &majorVersion, &minorVersion)) {
        ALOGE("Failed to initialize EGL: %s", getEGLError());
        return;
    }

    // Hardcoded to RGBx output display
    const EGLint config_attribs[] = {
        // Tag                  Value
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,           8,
        EGL_GREEN_SIZE,         8,
        EGL_BLUE_SIZE,          8,
        EGL_NONE
    };

    // Select OpenGL ES v 3
    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLConfig egl_config;
    EGLint num_configs;
    if (!eglChooseConfig(dpy, config_attribs, &egl_config, 1, &num_configs)) {
        ALOGE("eglChooseConfig() failed with error: %s", getEGLError());
        return;
    }

    surface = eglCreateWindowSurface(dpy, egl_config, window, NULL);
    EGLContext context = eglCreateContext(dpy, egl_config,
            EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        ALOGE("Failed to create OpenGL ES Context: %s", getEGLError());
        return;
    }


    // Activate our render target for drawing
    if (!eglMakeCurrent(dpy, surface, surface, context)) {
        ALOGE("Failed to make the OpenGL ES Context current: %s", getEGLError());
        return;
    } else {
        ALOGI("We made our context current!  :)");
    }


    eglMakeCurrent(dpy, surface, surface, context);
    eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);

    Imx3DView *view = new Imx3DView();
    if (view->addProgram(s_v_shader, s_f_shader) == -1)
        return;
    if (view->addProgram(s_v_shader_line, s_f_shader_line) == -1)
        return;
    if (view->addProgram(s_v_shader_bowl, s_f_shader_bowl) == -1)
        return;
    if (view->setProgram(0) == -1)
        return;

    float* data = nullptr;
    int data_num;
    int grid_buf;
    char input[128];
    memset(input, 0, sizeof(input));
    sprintf(input, "/sdcard/%d.png", 0);
    uint32_t width = 0, height=0, stride=0;
    bool retValue = getImageInfo(input, &width,
                    &height, &stride);
    if(!retValue)
        return;

    data_num = getGrids(&data, width, height);
    ALOGI("Draw point %d", data_num / SV_VERTEX_NUM);
    grid_buf = view->addBuffer(data, data_num / SV_VERTEX_NUM);

    view->cleanView();

    if(view->setProgram(1) == 0) {
        view->setBufferAsAttr(grid_buf, 1,(char *)"vPosition");

        //Rotation test
        cout << "**Rotation test**" << endl;
        for(int i = 0; i < 10; i ++){
            Vector3d r = { 1, 0, 0};
            //Vector3d t = { 0, 0, -0.1 * i};
            AngleAxisd rotation_vector(M_PI/20*i, r);
            Isometry3d T = Isometry3d::Identity();
            T.rotate(rotation_vector);
            //T.pretranslate(t);
            auto modelMatrix = T.matrix();
            //cout << "Transform matrix = \n" << modelMatrix << endl;
            float mvp_matrix[16];
            for(int i = 0; i < 4; i ++)
                for(int j = 0; j < 4; j ++){
                    mvp_matrix[i *4 + j] = (float)modelMatrix(i, j);
                }

            view->cleanView();
            view->setMVPMatrix(1, mvp_matrix);
	        view->renderBuffer(grid_buf, 0, data_num / SV_VERTEX_NUM);
            eglSwapBuffers(dpy, surface);
            usleep(300000);
        }
        sleep(3);

        cout << "**Rotation and transform test**" << endl;
        //Rotation and transform test
        for(int i = 0; i < 10; i ++){
            Vector3d r = { 1, 0, 0};
            Vector3d t = { 0, 0, 0.1 * i};
            AngleAxisd rotation_vector(M_PI/2, r);
            Isometry3d T = Isometry3d::Identity();
            T.rotate(rotation_vector);
            T.pretranslate(t);
            auto modelMatrix = T.matrix();
            //cout << "Transform matrix = \n" << modelMatrix << endl;
            float mvp_matrix[16];
            for(int i = 0; i < 4; i ++)
                for(int j = 0; j < 4; j ++){
                    mvp_matrix[i *4 + j] = (float)modelMatrix(i, j);
                }

            view->cleanView();
            view->setMVPMatrix(1, mvp_matrix);
	        view->renderBuffer(grid_buf, 0, data_num / SV_VERTEX_NUM);
            eglSwapBuffers(dpy, surface);
            usleep(300000);
        }

        cout << "**Rotation, transform  and Rotation test**" << endl;
        //Rotation and transform test
        for(int i = 0; i < 10; i ++){
            Vector3d r = { 1, 0, 0};
            Vector3d t = { 0, 0, 0.1};
            AngleAxisd rotation_vector(M_PI/2, r);
            Isometry3d T = Isometry3d::Identity();
            T.rotate(rotation_vector);
            T.pretranslate(t);
            auto modelMatrix = T.matrix();

            Vector3d view_r = { 0, 0, 1};
            AngleAxisd view_vector(M_PI/20*i, view_r);

            Isometry3d viewT = Isometry3d::Identity();
            viewT.rotate(view_vector);
            auto viewMatrix = viewT.matrix();

            auto mpvMatrix = viewMatrix * modelMatrix;

            //cout << "Transform matrix = \n" << modelMatrix << endl;
            float mvp_matrix[16];
            for(int i = 0; i < 4; i ++)
                for(int j = 0; j < 4; j ++){
                    mvp_matrix[i *4 + j] = (float)mpvMatrix(i, j);
                }

            view->cleanView();
            view->setMVPMatrix(1, mvp_matrix);
	        view->renderBuffer(grid_buf, 0, data_num / SV_VERTEX_NUM);
            eglSwapBuffers(dpy, surface);
            usleep(300000);
        }

    }

    sleep(2);
    ALOGI("End of the 3D Grids test");
    glFinish();
    if(data != nullptr)
        free(data);
}

TEST(ImxSV, 3DSurroundViewTextures) {
    EGLint majorVersion;
    EGLint minorVersion;
    EGLSurface surface;
    EGLint w, h;
    EGLDisplay dpy;

    WindowSurface windowSurface;
    EGLNativeWindowType window = windowSurface.getSurface();

    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        ALOGE("Failed to get egl display");
        return;
    }

    if(!eglInitialize(dpy, &majorVersion, &minorVersion)) {
        ALOGE("Failed to initialize EGL: %s", getEGLError());
        return;
    }

    // Hardcoded to RGBx output display
    const EGLint config_attribs[] = {
        // Tag                  Value
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,           8,
        EGL_GREEN_SIZE,         8,
        EGL_BLUE_SIZE,          8,
        EGL_NONE
    };

    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLConfig egl_config;
    EGLint num_configs;
    if (!eglChooseConfig(dpy, config_attribs, &egl_config, 1, &num_configs)) {
        ALOGE("eglChooseConfig() failed with error: %s", getEGLError());
        return;
    }

    surface = eglCreateWindowSurface(dpy, egl_config, window, NULL);
    EGLContext context = eglCreateContext(dpy, egl_config,
            EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        ALOGE("Failed to create OpenGL ES Context: %s", getEGLError());
        return;
    }


    // Activate our render target for drawing
    if (!eglMakeCurrent(dpy, surface, surface, context)) {
        ALOGE("Failed to make the OpenGL ES Context current: %s", getEGLError());
        return;
    } else {
        ALOGI("We made our context current!  :)");
    }


    eglMakeCurrent(dpy, surface, surface, context);
    eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);

    Imx3DView *view = new Imx3DView();
    if (view->addProgram(s_v_shader, s_f_shader) == -1)
        return;
    if (view->addProgram(s_v_shader_line, s_f_shader_line) == -1)
        return;
    if (view->addProgram(s_v_shader_bowl, s_f_shader_bowl) == -1)
        return;
    if (view->setProgram(0) == -1)
        return;


    char input[128];
    memset(input, 0, sizeof(input));
    sprintf(input, "/sdcard/%d.png", 0);
    uint32_t width = 0, height=0, stride=0;
    bool retValue = getImageInfo(input, &width,
                    &height, &stride);
    if(!retValue)
        return;

    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;

    initCameraParameters(evsRotations, evsTransforms,
        Ks, Ds);

	auto grid = CurvilinearGrid(SV_ANGLES_IN_PI, SV_Z_NOP, SV_X_STEP,
                                w, h, evsRotations, evsTransforms, Ks, Ds);
	grid.createGrid(SV_RADIUS); // Calculate grid points

    view->cleanView();

    if(view->setProgram(2) == 0) {
        //prepare input buffers
        vector<shared_ptr<char>> distorts;
        vector<shared_ptr<unsigned char>> images;
        prepareFisheyeImages(distorts);

        //RGB24 bits
        for(uint32_t index = 0; index < 4; index ++) {
            shared_ptr<unsigned char> image_outbuf(new unsigned char[height *width * 3],
                std::default_delete<unsigned char[]>());
            if(image_outbuf == nullptr)
                return;
            unsigned char *image = image_outbuf.get();
            auto pixels = distorts[index].get();
            for(int i = 0; i < height; i ++)
                for(int j = 0; j < width; j ++) {
                    unsigned char *originP = (unsigned char *)((uint32_t *)pixels+ i * width + j);
                    image[(i * width + j)*3] = *originP;
                    image[(i * width + j)*3 + 1] = *(originP + 1);
                    image[(i * width + j)*3 + 2] = *(originP + 2);
            }
            images.push_back(image_outbuf);
        }

        vector<int> mashes;
        for(uint32_t index = 0; index < 4; index ++) {
            float* data = nullptr;
            int data_num = getMashes(grid, &data, index);
            int grid_buf = view->addMesh(data, data_num / SV_ATTRIBUTE_NUM);
            if(data != nullptr) {
                free(data);
                data = nullptr;
            }
            mashes.push_back(grid_buf);
        }

        cout << "**Rotation test**" << endl;
        for(int i = 0; i < 10; i ++){
            Vector3d r = { 1, 0, 0};
            AngleAxisd rotation_vector(M_PI/20*i, r);
            Isometry3d T = Isometry3d::Identity();
            T.rotate(rotation_vector);
            auto modelMatrix = T.matrix();
            float mvp_matrix[16];
            for(int i = 0; i < 4; i ++)
                for(int j = 0; j < 4; j ++){
                    mvp_matrix[i *4 + j] = (float)modelMatrix(i, j);
                }

            view->cleanView();
            view->setMVPMatrix(2, mvp_matrix);

            for(uint32_t index = 0; index < 4; index ++) {
		        view->renderView(images[index], width, height, mashes[index]);
            }

            eglSwapBuffers(dpy, surface);
            usleep(300000);
        }

        cout << "**Rotation and transform test**" << endl;
        //Rotation and transform test
        for(int i = 0; i < 10; i ++){
            Vector3d r = { 1, 0, 0};
            Vector3d t = { 0, 0, 0.1 * i};
            AngleAxisd rotation_vector(M_PI/2, r);
            Isometry3d T = Isometry3d::Identity();
            T.rotate(rotation_vector);
            T.pretranslate(t);
            auto modelMatrix = T.matrix();
            //cout << "Transform matrix = \n" << modelMatrix << endl;
            float mvp_matrix[16];
            for(int i = 0; i < 4; i ++)
                for(int j = 0; j < 4; j ++){
                    mvp_matrix[i *4 + j] = (float)modelMatrix(i, j);
                }

            view->cleanView();
            view->setMVPMatrix(2, mvp_matrix);

            for(uint32_t index = 0; index < 4; index ++) {
		        view->renderView(images[index], width, height, mashes[index]);
            }

            eglSwapBuffers(dpy, surface);
            usleep(300000);
        }

        cout << "**X Rotation, transform  and Rotation test**" << endl;
        //Rotation and transform test
        for(int i = 0; i < 10; i ++){
            Vector3d r = { 1, 0, 0};
            Vector3d t = { 0, 0, 0.1};
            AngleAxisd rotation_vector(M_PI/2, r);
            Isometry3d T = Isometry3d::Identity();
            T.rotate(rotation_vector);
            T.pretranslate(t);
            auto modelMatrix = T.matrix();

            Vector3d view_r = { 0, 0, 1};
            AngleAxisd view_vector(M_PI/20*i, view_r);

            Isometry3d viewT = Isometry3d::Identity();
            viewT.rotate(view_vector);
            auto viewMatrix = viewT.matrix();

            auto mpvMatrix = viewMatrix * modelMatrix;

            //cout << "Transform matrix = \n" << modelMatrix << endl;
            float mvp_matrix[16];
            for(int i = 0; i < 4; i ++)
                for(int j = 0; j < 4; j ++){
                    mvp_matrix[i *4 + j] = (float)mpvMatrix(i, j);
                }

            view->cleanView();
            view->setMVPMatrix(2, mvp_matrix);

            for(uint32_t index = 0; index < 4; index ++) {
		        view->renderView(images[index], width, height, mashes[index]);
            }

            eglSwapBuffers(dpy, surface);
            usleep(300000);
        }

        cout << "**Z Rotation, transform  and Rotation test**" << endl;
        //Rotation and transform test
        for(int i = 0; i < 10; i ++){
            Vector3d r = { 0, 0, 1};
            Vector3d t = { 0, 0, 0};
            AngleAxisd rotation_vector(M_PI/10 * i, r);
            Isometry3d T = Isometry3d::Identity();
            T.rotate(rotation_vector);
            T.pretranslate(t);
            auto modelMatrix = T.matrix();

            //cout << "Transform matrix = \n" << modelMatrix << endl;
            float mvp_matrix[16];
            for(int i = 0; i < 4; i ++)
                for(int j = 0; j < 4; j ++){
                    mvp_matrix[i *4 + j] = (float)modelMatrix(i, j);
                }

            view->cleanView();
            view->setMVPMatrix(2, mvp_matrix);

            for(uint32_t index = 0; index < 4; index ++) {
		        view->renderView(images[index], width, height, mashes[index]);
            }

            eglSwapBuffers(dpy, surface);
            usleep(300000);
        }

    }

    sleep(2);
    ALOGI("End of the SV Textures test");
    glFinish();
}

TEST(ImxSV, 3DSurroundViewMashes) {
    EGLint majorVersion;
    EGLint minorVersion;
    EGLSurface surface;
    EGLint w, h;
    EGLDisplay dpy;

    WindowSurface windowSurface;
    EGLNativeWindowType window = windowSurface.getSurface();

    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        ALOGE("Failed to get egl display");
        return;
    }

    if(!eglInitialize(dpy, &majorVersion, &minorVersion)) {
        ALOGE("Failed to initialize EGL: %s", getEGLError());
        return;
    }

    // Hardcoded to RGBx output display
    const EGLint config_attribs[] = {
        // Tag                  Value
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,           8,
        EGL_GREEN_SIZE,         8,
        EGL_BLUE_SIZE,          8,
        EGL_NONE
    };

    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLConfig egl_config;
    EGLint num_configs;
    if (!eglChooseConfig(dpy, config_attribs, &egl_config, 1, &num_configs)) {
        ALOGE("eglChooseConfig() failed with error: %s", getEGLError());
        return;
    }

    surface = eglCreateWindowSurface(dpy, egl_config, window, NULL);
    EGLContext context = eglCreateContext(dpy, egl_config,
            EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        ALOGE("Failed to create OpenGL ES Context: %s", getEGLError());
        return;
    }

    // Activate our render target for drawing
    if (!eglMakeCurrent(dpy, surface, surface, context)) {
        ALOGE("Failed to make the OpenGL ES Context current: %s", getEGLError());
        return;
    } else {
        ALOGI("We made our context current!  :)");
    }

    eglMakeCurrent(dpy, surface, surface, context);
    eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);

    Imx3DView *view = new Imx3DView();
    if (view->addProgram(s_v_shader, s_f_shader) == -1)
        return;
    if (view->addProgram(s_v_shader_line, s_f_shader_line) == -1)
        return;
    if (view->addProgram(s_v_shader_bowl, s_f_shader_bowl) == -1)
        return;
    if (view->setProgram(0) == -1)
        return;

    char input[128];
    memset(input, 0, sizeof(input));
    sprintf(input, "/sdcard/%d.png", 0);
    uint32_t width = 0, height=0, stride=0;
    bool retValue = getImageInfo(input, &width,
                    &height, &stride);
    if(!retValue)
        return;

    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;

    initCameraParameters(evsRotations, evsTransforms,
        Ks, Ds);

	auto grid = CurvilinearGrid(SV_ANGLES_IN_PI, SV_Z_NOP, SV_X_STEP,
                                w, h, evsRotations, evsTransforms, Ks, Ds);
	grid.createGrid(SV_RADIUS); // Calculate grid points

    view->cleanView();

    if(view->setProgram(2) == 0) {
        //prepare input buffers
        vector<shared_ptr<unsigned char>> images;

        //RGB24 bits
        for(uint32_t index = 0; index < 4; index ++) {
            shared_ptr<unsigned char> image_outbuf(new unsigned char[height *width * 3],
                std::default_delete<unsigned char[]>());
            if(image_outbuf == nullptr)
                return;
            unsigned char *image = image_outbuf.get();
            for(int i = 0; i < height; i ++)
                for(int j = 0; j < width; j ++) {
                    if(index == 0) {
                        image[(i * width + j)*3] = 0xff;
                        image[(i * width + j)*3 + 1] = 0;
                        image[(i * width + j)*3 + 2] = 0;
                    } else if(index == 1) {
                        image[(i * width + j)*3] = 0;
                        image[(i * width + j)*3 + 1] = 0xff;
                        image[(i * width + j)*3 + 2] = 0;
                    } else if(index == 2) {
                        image[(i * width + j)*3] = 0;
                        image[(i * width + j)*3 + 1] = 0;
                        image[(i * width + j)*3 + 2] = 0xff;
                    } else {
                        image[(i * width + j)*3] = 0;
                        image[(i * width + j)*3 + 1] = 0xff;
                        image[(i * width + j)*3 + 2] = 0xff;
                    }

            }
            images.push_back(image_outbuf);
        }

        vector<int> mashes;
        for(uint32_t index = 0; index < 4; index ++) {
            float* data = nullptr;
            int data_num = getMashes(grid, &data, index);
            int grid_buf = view->addMesh(data, data_num / SV_ATTRIBUTE_NUM);
            if(data != nullptr) {
                free(data);
                data = nullptr;
            }
            mashes.push_back(grid_buf);
        }

        cout << "**Z Rotation, transform  and Rotation test**" << endl;
        //Rotation and transform test
        for(int i = 0; i < 10; i ++){
            Vector3d r = { 0, 0, 1};
            Vector3d t = { 0, 0, 0};
            AngleAxisd rotation_vector(M_PI/10 * i, r);
            Isometry3d T = Isometry3d::Identity();
            T.rotate(rotation_vector);
            T.pretranslate(t);
            auto modelMatrix = T.matrix();

            //cout << "Transform matrix = \n" << modelMatrix << endl;
            float mvp_matrix[16];
            for(int i = 0; i < 4; i ++)
                for(int j = 0; j < 4; j ++){
                    mvp_matrix[i *4 + j] = (float)modelMatrix(i, j);
                }

            view->cleanView();
            view->setMVPMatrix(2, mvp_matrix);

            for(uint32_t index = 0; index < 4; index ++) {
		        view->renderView(images[index], width, height, mashes[index]);
            }

            eglSwapBuffers(dpy, surface);
            usleep(300000);
        }

    }

    sleep(2);
    ALOGI("End of the SV Mashes test");
    glFinish();
}

TEST(ImxSV, 3DSurroundViewCar) {
    EGLint majorVersion;
    EGLint minorVersion;
    EGLSurface surface;
    EGLint w, h;
    EGLDisplay dpy;

    WindowSurface windowSurface;
    EGLNativeWindowType window = windowSurface.getSurface();

    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        ALOGE("Failed to get egl display");
        return;
    }

    if(!eglInitialize(dpy, &majorVersion, &minorVersion)) {
        ALOGE("Failed to initialize EGL: %s", getEGLError());
        return;
    }

    // Hardcoded to RGBx output display
    const EGLint config_attribs[] = {
        // Tag                  Value
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,           8,
        EGL_GREEN_SIZE,         8,
        EGL_BLUE_SIZE,          8,
        EGL_NONE
    };

    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLConfig egl_config;
    EGLint num_configs;
    if (!eglChooseConfig(dpy, config_attribs, &egl_config, 1, &num_configs)) {
        ALOGE("eglChooseConfig() failed with error: %s", getEGLError());
        return;
    }

    surface = eglCreateWindowSurface(dpy, egl_config, window, NULL);
    EGLContext context = eglCreateContext(dpy, egl_config,
            EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        ALOGE("Failed to create OpenGL ES Context: %s", getEGLError());
        return;
    }

    // Activate our render target for drawing
    if (!eglMakeCurrent(dpy, surface, surface, context)) {
        ALOGE("Failed to make the OpenGL ES Context current: %s", getEGLError());
        return;
    } else {
        ALOGI("We made our context current!  :)");
    }

    eglMakeCurrent(dpy, surface, surface, context);
    eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
        
    ModelLoader modelLoader;
    Programs carModelProgram;

	if (!modelLoader.Initialize()) {
        cout << "Car model was not initialized" << endl;
        return;
    }
	// Car Model
	if (carModelProgram.loadShaders(s_v_shader_model, s_f_shader_model) == -1) // Car image
	{
	    cout << "Car rendering program was not loaded" << endl;
	    return;
	}

    GLuint mvpUniform, mvUniform, mnUniform;
	mvpUniform = glGetUniformLocation(carModelProgram.getHandle(), "mvp");
	mvUniform = glGetUniformLocation(carModelProgram.getHandle(), "mv");
	mnUniform = glGetUniformLocation(carModelProgram.getHandle(), "mn");
	
	glUseProgram(carModelProgram.getHandle());
	glEnable(GL_DEPTH_TEST);

    float rx = 0.0f, ry = 0.0f, px = 0.0f, py = 0.0f, pz = -10.0f;
    glm::mat4 gProjection = glm::perspective(45.0f,
                            (float) w / (float)h, 0.1f, 100.0f);
    for(int i = 0; i < 10; i++) {
	    // Render car model
	    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glm::vec3 car_scale = glm::vec3(i*1.0f, i * 1.0f, i * 1.0f);
	    glm::mat4 mv = glm::rotate(glm::rotate(glm::translate(glm::mat4(1.0f),
                            glm::vec3(px, py, pz)), ry, glm::vec3(1, 0, 0)),
                            rx, glm::vec3(0, 0, 1));
	    glm::mat4 carModelMatrix = glm::rotate(glm::rotate(glm::scale(glm::mat4(1.0f),
                        car_scale), glm::radians(CAR_ORIENTATION_X),
                        glm::vec3(1, 0, 0)), glm::radians(CAR_ORIENTATION_Y),
                        glm::vec3(0, 1, 0));
	    auto mvp = gProjection * mv * carModelMatrix;
	    auto mn = glm::mat3(glm::rotate(glm::rotate(glm::rotate(glm::rotate(glm::mat4(1.0f), ry, glm::vec3(1, 0, 0)), rx, glm::vec3(0, 0, 1)),
		glm::radians(CAR_ORIENTATION_X), glm::vec3(1, 0, 0)), glm::radians(CAR_ORIENTATION_Y), glm::vec3(0, 1, 0)));
	    // Set matrices
	    glUniformMatrix4fv(mvpUniform, 1, GL_FALSE, glm::value_ptr(mvp)); 
	    glUniformMatrix4fv(mvUniform, 1, GL_FALSE, glm::value_ptr(mv)); 
	    glUniformMatrix3fv(mnUniform, 1, GL_FALSE, glm::value_ptr(mn)); 

	    modelLoader.Draw(carModelProgram.getHandle());
	    glFinish();
        eglSwapBuffers(dpy, surface);
        usleep(300000);
    }

    sleep(6);
    ALOGI("End of the SV Car test");
    glFinish();
}

TEST(ImxSV, 3DSurroundViewMashesCar) {
    EGLint majorVersion;
    EGLint minorVersion;
    EGLSurface surface;
    EGLint w, h;
    EGLDisplay dpy;

    WindowSurface windowSurface;
    EGLNativeWindowType window = windowSurface.getSurface();

    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        ALOGE("Failed to get egl display");
        return;
    }

    if(!eglInitialize(dpy, &majorVersion, &minorVersion)) {
        ALOGE("Failed to initialize EGL: %s", getEGLError());
        return;
    }

    // Hardcoded to RGBx output display
    const EGLint config_attribs[] = {
        // Tag                  Value
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,           8,
        EGL_GREEN_SIZE,         8,
        EGL_BLUE_SIZE,          8,
        EGL_NONE
    };

    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLConfig egl_config;
    EGLint num_configs;
    if (!eglChooseConfig(dpy, config_attribs, &egl_config, 1, &num_configs)) {
        ALOGE("eglChooseConfig() failed with error: %s", getEGLError());
        return;
    }

    surface = eglCreateWindowSurface(dpy, egl_config, window, NULL);
    EGLContext context = eglCreateContext(dpy, egl_config,
            EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        ALOGE("Failed to create OpenGL ES Context: %s", getEGLError());
        return;
    }

    // Activate our render target for drawing
    if (!eglMakeCurrent(dpy, surface, surface, context)) {
        ALOGE("Failed to make the OpenGL ES Context current: %s", getEGLError());
        return;
    } else {
        ALOGI("We made our context current!  :)");
    }

    eglMakeCurrent(dpy, surface, surface, context);
    eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);

    Imx3DView *view = new Imx3DView();
    if (view->addProgram(s_v_shader, s_f_shader) == -1)
        return;
    if (view->addProgram(s_v_shader_line, s_f_shader_line) == -1)
        return;
    if (view->addProgram(s_v_shader_bowl, s_f_shader_bowl) == -1)
        return;
    if (view->setProgram(0) == -1)
        return;

    char input[128];
    memset(input, 0, sizeof(input));
    sprintf(input, "/sdcard/%d.png", 0);
    uint32_t width = 0, height=0, stride=0;
    bool retValue = getImageInfo(input, &width,
                    &height, &stride);
    if(!retValue)
        return;

    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;

    initCameraParameters(evsRotations, evsTransforms,
        Ks, Ds);

	auto grid = CurvilinearGrid(SV_ANGLES_IN_PI, SV_Z_NOP, SV_X_STEP,
                                w, h, evsRotations, evsTransforms, Ks, Ds);
	grid.createGrid(SV_RADIUS); // Calculate grid points

    view->cleanView();
   
    //Car render init
    ModelLoader modelLoader;
    Programs carModelProgram;
	if (!modelLoader.Initialize()) {
        cout << "Car model was not initialized" << endl;
        return;
    }
	// Car Model
	if (carModelProgram.loadShaders(s_v_shader_model, s_f_shader_model) == -1) // Car image
	{
	    cout << "Car rendering program was not loaded" << endl;
	    return;
	}
    GLuint mvpUniform, mvUniform, mnUniform;
	mvpUniform = glGetUniformLocation(carModelProgram.getHandle(), "mvp");
	mvUniform = glGetUniformLocation(carModelProgram.getHandle(), "mv");
	mnUniform = glGetUniformLocation(carModelProgram.getHandle(), "mn");
    float rx = 0.0f, ry = 0.0f, px = 0.0f, py = 0.0f, pz = -10.0f;
    glm::mat4 gProjection = glm::perspective(45.0f,
                            (float) w / (float)h, 0.1f, 100.0f);

    //prepare input buffers
    vector<shared_ptr<unsigned char>> images;

    //RGB24 bits
    for(uint32_t index = 0; index < 4; index ++) {
        shared_ptr<unsigned char> image_outbuf(new unsigned char[height *width * 3],
            std::default_delete<unsigned char[]>());
        if(image_outbuf == nullptr)
            return;
        unsigned char *image = image_outbuf.get();
        for(int i = 0; i < height; i ++)
            for(int j = 0; j < width; j ++) {
                if(index == 0) {
                    image[(i * width + j)*3] = 0xff;
                    image[(i * width + j)*3 + 1] = 0;
                    image[(i * width + j)*3 + 2] = 0;
                } else if(index == 1) {
                    image[(i * width + j)*3] = 0;
                    image[(i * width + j)*3 + 1] = 0xff;
                    image[(i * width + j)*3 + 2] = 0;
                } else if(index == 2) {
                    image[(i * width + j)*3] = 0;
                    image[(i * width + j)*3 + 1] = 0;
                    image[(i * width + j)*3 + 2] = 0xff;
                } else {
                    image[(i * width + j)*3] = 0;
                    image[(i * width + j)*3 + 1] = 0xff;
                    image[(i * width + j)*3 + 2] = 0xff;
                }

        }
        images.push_back(image_outbuf);
    }

    vector<int> mashes;
    view->setProgram(2);
    for(uint32_t index = 0; index < 4; index ++) {
        float* data = nullptr;
        int data_num = getMashes(grid, &data, index);
        int grid_buf = view->addMesh(data, data_num / SV_ATTRIBUTE_NUM);
        if(data != nullptr) {
            free(data);
            data = nullptr;
        }
        mashes.push_back(grid_buf);
    }   

    //Rotation and transform test
    for(int i = 0; i < 10; i ++){
        //car and sv share same view and projection matrix
	    glm::mat4 mv = glm::rotate(glm::rotate(glm::translate(glm::mat4(1.0f),
                        glm::vec3(px, py, pz)), glm::radians(ry + 30.0f * i), glm::vec3(1, 0, 0)),
                        rx, glm::vec3(0, 0, 1));

        //Car mvp matrixes
        //the dae car model we used is around 5 times of sv view
        glm::vec3 car_scale = glm::vec3(i*0.2f, i * 0.2f, i * 0.2f);
	    glm::mat4 carModelMatrix = glm::rotate(glm::rotate(glm::scale(glm::mat4(1.0f),
                    car_scale), glm::radians(CAR_ORIENTATION_X),
                    glm::vec3(1, 0, 0)), glm::radians(CAR_ORIENTATION_Y),
                    glm::vec3(0, 1, 0));
	    auto car_mvp = gProjection * mv * carModelMatrix;
	    auto car_mn = glm::mat3(glm::rotate(glm::rotate(glm::rotate(glm::rotate(glm::mat4(1.0f),
                    ry, glm::vec3(1, 0, 0)), rx, glm::vec3(0, 0, 1)),
	                glm::radians(CAR_ORIENTATION_X), glm::vec3(1, 0, 0)),
                    glm::radians(CAR_ORIENTATION_Y), glm::vec3(0, 1, 0)));

        //SV mv matrixes
        glm::vec3 sv_scale = glm::vec3(i*1.0f, i * 1.0f, i * 1.0f);
	    glm::mat4 svModelMatrix = glm::rotate(glm::rotate(glm::scale(glm::mat4(1.0f),
                    sv_scale), glm::radians(SV_ORIENTATION_X),
                    glm::vec3(1, 0, 0)), glm::radians(SV_ORIENTATION_Y),
                    glm::vec3(0, 1, 0));
	    auto sv_mvp = gProjection * mv * svModelMatrix;

        view->setProgram(2);
        view->cleanView();
        view->setMVPMatrix(2, glm::value_ptr(sv_mvp));

        for(uint32_t index = 0; index < 4; index ++) {
	        view->renderView(images[index], width, height, mashes[index]);
        }
        glUseProgram(carModelProgram.getHandle());
        // Set matrices
	    glUniformMatrix4fv(mvpUniform, 1, GL_FALSE, glm::value_ptr(car_mvp)); 
	    glUniformMatrix4fv(mvUniform, 1, GL_FALSE, glm::value_ptr(mv)); 
	    glUniformMatrix3fv(mnUniform, 1, GL_FALSE, glm::value_ptr(car_mn)); 

	    modelLoader.Draw(carModelProgram.getHandle());
	    glFinish();

        eglSwapBuffers(dpy, surface);
        usleep(300000);
    }

    //Rotation and transform test
    for(int i = 0; i < 10; i ++){
        //car and sv share same view and projection matrix
	    glm::mat4 mv = glm::rotate(glm::rotate(glm::translate(glm::mat4(1.0f),
                        glm::vec3(px, py, pz)), ry, glm::vec3(1, 0, 0)),
                        glm::radians(rx + 30.0f * i), glm::vec3(0, 0, 1));

        //Car mvp matrixes
        //the dae car model we used is around 5 times of sv view
        glm::vec3 car_scale = glm::vec3(8*0.2f, 8 * 0.2f, 8 * 0.2f);
	    glm::mat4 carModelMatrix = glm::rotate(glm::rotate(glm::scale(glm::mat4(1.0f),
                    car_scale), glm::radians(CAR_ORIENTATION_X),
                    glm::vec3(1, 0, 0)), glm::radians(CAR_ORIENTATION_Y),
                    glm::vec3(0, 1, 0));
	    auto car_mvp = gProjection * mv * carModelMatrix;
	    auto car_mn = glm::mat3(glm::rotate(glm::rotate(glm::rotate(glm::rotate(glm::mat4(1.0f),
                    ry, glm::vec3(1, 0, 0)), rx, glm::vec3(0, 0, 1)),
	                glm::radians(CAR_ORIENTATION_X), glm::vec3(1, 0, 0)),
                    glm::radians(CAR_ORIENTATION_Y), glm::vec3(0, 1, 0)));

        //SV mv matrixes
        glm::vec3 sv_scale = glm::vec3(8*1.0f, 8 * 1.0f, 8 * 1.0f);
	    glm::mat4 svModelMatrix = glm::rotate(glm::rotate(glm::scale(glm::mat4(1.0f),
                    sv_scale), glm::radians(SV_ORIENTATION_X),
                    glm::vec3(1, 0, 0)), glm::radians(SV_ORIENTATION_Y),
                    glm::vec3(0, 1, 0));
	    auto sv_mvp = gProjection * mv * svModelMatrix;

        view->setProgram(2);
        view->cleanView();
        view->setMVPMatrix(2, glm::value_ptr(sv_mvp));

        for(uint32_t index = 0; index < 4; index ++) {
	        view->renderView(images[index], width, height, mashes[index]);
        }
        glUseProgram(carModelProgram.getHandle());
        // Set matrices
	    glUniformMatrix4fv(mvpUniform, 1, GL_FALSE, glm::value_ptr(car_mvp)); 
	    glUniformMatrix4fv(mvUniform, 1, GL_FALSE, glm::value_ptr(mv)); 
	    glUniformMatrix3fv(mnUniform, 1, GL_FALSE, glm::value_ptr(car_mn)); 

	    modelLoader.Draw(carModelProgram.getHandle());
	    glFinish();

        eglSwapBuffers(dpy, surface);
        usleep(300000);
    }


    sleep(2);
    ALOGI("End of the SV Mashes with Car test");
    glFinish();
}

TEST(ImxSV, 3DSurroundViewTexturesCar) {
    EGLint majorVersion;
    EGLint minorVersion;
    EGLSurface surface;
    EGLint w, h;
    EGLDisplay dpy;

    WindowSurface windowSurface;
    EGLNativeWindowType window = windowSurface.getSurface();

    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        ALOGE("Failed to get egl display");
        return;
    }

    if(!eglInitialize(dpy, &majorVersion, &minorVersion)) {
        ALOGE("Failed to initialize EGL: %s", getEGLError());
        return;
    }

    // Hardcoded to RGBx output display
    const EGLint config_attribs[] = {
        // Tag                  Value
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,           8,
        EGL_GREEN_SIZE,         8,
        EGL_BLUE_SIZE,          8,
        EGL_NONE
    };

    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLConfig egl_config;
    EGLint num_configs;
    if (!eglChooseConfig(dpy, config_attribs, &egl_config, 1, &num_configs)) {
        ALOGE("eglChooseConfig() failed with error: %s", getEGLError());
        return;
    }

    surface = eglCreateWindowSurface(dpy, egl_config, window, NULL);
    EGLContext context = eglCreateContext(dpy, egl_config,
            EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        ALOGE("Failed to create OpenGL ES Context: %s", getEGLError());
        return;
    }


    // Activate our render target for drawing
    if (!eglMakeCurrent(dpy, surface, surface, context)) {
        ALOGE("Failed to make the OpenGL ES Context current: %s", getEGLError());
        return;
    } else {
        ALOGI("We made our context current!  :)");
    }


    eglMakeCurrent(dpy, surface, surface, context);
    eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);

    Imx3DView *view = new Imx3DView();
    if (view->addProgram(s_v_shader, s_f_shader) == -1)
        return;
    if (view->addProgram(s_v_shader_line, s_f_shader_line) == -1)
        return;
    if (view->addProgram(s_v_shader_bowl, s_f_shader_bowl) == -1)
        return;
    if (view->setProgram(0) == -1)
        return;


    char input[128];
    memset(input, 0, sizeof(input));
    sprintf(input, "/sdcard/%d.png", 0);
    uint32_t width = 0, height=0, stride=0;
    bool retValue = getImageInfo(input, &width,
                    &height, &stride);
    if(!retValue)
        return;

    vector<Vector3d> evsRotations;
    vector<Vector3d> evsTransforms;
    vector<Matrix<double, 3, 3>> Ks;
    vector<Matrix<double, 1, 4>> Ds;

    initCameraParameters(evsRotations, evsTransforms,
        Ks, Ds);

	auto grid = CurvilinearGrid(SV_ANGLES_IN_PI, SV_Z_NOP, SV_X_STEP,
                                w, h, evsRotations, evsTransforms, Ks, Ds);
	grid.createGrid(SV_RADIUS); // Calculate grid points

    view->cleanView();

    //Car render init
    ModelLoader modelLoader;
    Programs carModelProgram;
	if (!modelLoader.Initialize()) {
        cout << "Car model was not initialized" << endl;
        return;
    }
	// Car Model
	if (carModelProgram.loadShaders(s_v_shader_model, s_f_shader_model) == -1) // Car image
	{
	    cout << "Car rendering program was not loaded" << endl;
	    return;
	}
    GLuint mvpUniform, mvUniform, mnUniform;
	mvpUniform = glGetUniformLocation(carModelProgram.getHandle(), "mvp");
	mvUniform = glGetUniformLocation(carModelProgram.getHandle(), "mv");
	mnUniform = glGetUniformLocation(carModelProgram.getHandle(), "mn");
    float rx = 0.0f, ry = 0.0f, px = 0.0f, py = 0.0f, pz = -10.0f;
    glm::mat4 gProjection = glm::perspective(45.0f,
                            (float) w / (float)h, 0.1f, 100.0f);

    //prepare input buffers
    vector<shared_ptr<char>> distorts;
    vector<shared_ptr<unsigned char>> images;
    prepareFisheyeImages(distorts);

    //RGB24 bits
    for(uint32_t index = 0; index < 4; index ++) {
        shared_ptr<unsigned char> image_outbuf(new unsigned char[height *width * 3],
            std::default_delete<unsigned char[]>());
        if(image_outbuf == nullptr)
            return;
        unsigned char *image = image_outbuf.get();
        auto pixels = distorts[index].get();
        for(int i = 0; i < height; i ++)
            for(int j = 0; j < width; j ++) {
                unsigned char *originP = (unsigned char *)((uint32_t *)pixels+ i * width + j);
                image[(i * width + j)*3] = *originP;
                image[(i * width + j)*3 + 1] = *(originP + 1);
                image[(i * width + j)*3 + 2] = *(originP + 2);
        }
        images.push_back(image_outbuf);
    }

    vector<int> mashes;
    view->setProgram(2);
    for(uint32_t index = 0; index < 4; index ++) {
        float* data = nullptr;
        int data_num = getMashes(grid, &data, index);
        int grid_buf = view->addMesh(data, data_num / SV_ATTRIBUTE_NUM);
        if(data != nullptr) {
            free(data);
            data = nullptr;
        }
        mashes.push_back(grid_buf);
    }

    cout << "**Rotation test**" << endl;
    for(int i = 0; i < 180; i ++){
        //car and sv share same view and projection matrix
	    glm::mat4 mv = glm::rotate(glm::rotate(glm::translate(glm::mat4(1.0f),
                        glm::vec3(px, py, pz)), ry, glm::vec3(1, 0, 0)),
                        glm::radians(rx + 5.0f * i), glm::vec3(0, 0, 1));

        //Car mvp matrixes
        //the dae car model we used is around 5 times of sv view
        glm::vec3 car_scale = glm::vec3(8*0.2f, 8 * 0.2f, 8 * 0.2f);
	    glm::mat4 carModelMatrix = glm::rotate(glm::rotate(glm::scale(glm::mat4(1.0f),
                    car_scale), glm::radians(CAR_ORIENTATION_X),
                    glm::vec3(1, 0, 0)), glm::radians(CAR_ORIENTATION_Y),
                    glm::vec3(0, 1, 0));
	    auto car_mvp = gProjection * mv * carModelMatrix;
	    auto car_mn = glm::mat3(glm::rotate(glm::rotate(glm::rotate(glm::rotate(glm::mat4(1.0f),
                    ry, glm::vec3(1, 0, 0)), rx, glm::vec3(0, 0, 1)),
	                glm::radians(CAR_ORIENTATION_X), glm::vec3(1, 0, 0)),
                    glm::radians(CAR_ORIENTATION_Y), glm::vec3(0, 1, 0)));

        //SV mv matrixes
        glm::vec3 sv_scale = glm::vec3(8*1.0f, 8 * 1.0f, 8 * 1.0f);
	    glm::mat4 svModelMatrix = glm::rotate(glm::rotate(glm::scale(glm::mat4(1.0f),
                    sv_scale), glm::radians(SV_ORIENTATION_X),
                    glm::vec3(1, 0, 0)), glm::radians(SV_ORIENTATION_Y),
                    glm::vec3(0, 1, 0));
	    auto sv_mvp = gProjection * mv * svModelMatrix;

        //draw 4 sv camera view
        view->setProgram(2);
        view->cleanView();
        view->setMVPMatrix(2, glm::value_ptr(sv_mvp));
        for(uint32_t index = 0; index < 4; index ++) {
	        view->renderView(images[index], width, height, mashes[index]);
        }

        glUseProgram(carModelProgram.getHandle());
        // Set matrices
	    glUniformMatrix4fv(mvpUniform, 1, GL_FALSE, glm::value_ptr(car_mvp)); 
	    glUniformMatrix4fv(mvUniform, 1, GL_FALSE, glm::value_ptr(mv)); 
	    glUniformMatrix3fv(mnUniform, 1, GL_FALSE, glm::value_ptr(car_mn)); 

	    modelLoader.Draw(carModelProgram.getHandle());
	    glFinish();

        eglSwapBuffers(dpy, surface);
        usleep(5000);
    }

    for(int i = 0; i < 180; i ++){
        //car and sv share same view and projection matrix
	    glm::mat4 mv = glm::rotate(glm::rotate(glm::translate(glm::mat4(1.0f),
                        glm::vec3(px, py, pz)), glm::radians(ry - 0.2f * i), glm::vec3(1, 0, 0)),
                        glm::radians(rx), glm::vec3(0, 0, 1));

        //Car mvp matrixes
        //the dae car model we used is around 5 times of sv view
        glm::vec3 car_scale = glm::vec3(8*0.2f, 8 * 0.2f, 8 * 0.2f);
	    glm::mat4 carModelMatrix = glm::rotate(glm::rotate(glm::scale(glm::mat4(1.0f),
                    car_scale), glm::radians(CAR_ORIENTATION_X),
                    glm::vec3(1, 0, 0)), glm::radians(CAR_ORIENTATION_Y),
                    glm::vec3(0, 1, 0));
	    auto car_mvp = gProjection * mv * carModelMatrix;
	    auto car_mn = glm::mat3(glm::rotate(glm::rotate(glm::rotate(glm::rotate(glm::mat4(1.0f),
                    ry, glm::vec3(1, 0, 0)), rx, glm::vec3(0, 0, 1)),
	                glm::radians(CAR_ORIENTATION_X), glm::vec3(1, 0, 0)),
                    glm::radians(CAR_ORIENTATION_Y), glm::vec3(0, 1, 0)));

        //SV mv matrixes
        glm::vec3 sv_scale = glm::vec3(8*1.0f, 8 * 1.0f, 8 * 1.0f);
	    glm::mat4 svModelMatrix = glm::rotate(glm::rotate(glm::scale(glm::mat4(1.0f),
                    sv_scale), glm::radians(SV_ORIENTATION_X),
                    glm::vec3(1, 0, 0)), glm::radians(SV_ORIENTATION_Y),
                    glm::vec3(0, 1, 0));
	    auto sv_mvp = gProjection * mv * svModelMatrix;

        //draw 4 sv camera view
        view->setProgram(2);
        view->cleanView();
        view->setMVPMatrix(2, glm::value_ptr(sv_mvp));
        for(uint32_t index = 0; index < 4; index ++) {
	        view->renderView(images[index], width, height, mashes[index]);
        }

        glUseProgram(carModelProgram.getHandle());
        // Set matrices
	    glUniformMatrix4fv(mvpUniform, 1, GL_FALSE, glm::value_ptr(car_mvp)); 
	    glUniformMatrix4fv(mvUniform, 1, GL_FALSE, glm::value_ptr(mv)); 
	    glUniformMatrix3fv(mnUniform, 1, GL_FALSE, glm::value_ptr(car_mn)); 
	    modelLoader.Draw(carModelProgram.getHandle());
	    glFinish();

        eglSwapBuffers(dpy, surface);
        usleep(5000);
    }

    for(int i = 0; i < 360; i ++){
        //car and sv share same view and projection matrix
	    glm::mat4 mv = glm::rotate(glm::rotate(glm::translate(glm::mat4(1.0f),
                        glm::vec3(px, py, pz)), glm::radians(-60.0f), glm::vec3(1, 0, 0)),
                        glm::radians(rx + 1.0f * i), glm::vec3(0, 0, 1));

        //Car mvp matrixes
        //the dae car model we used is around 5 times of sv view
        glm::vec3 car_scale = glm::vec3(8*0.2f, 8 * 0.2f, 8 * 0.2f);
	    glm::mat4 carModelMatrix = glm::rotate(glm::rotate(glm::scale(glm::mat4(1.0f),
                    car_scale), glm::radians(CAR_ORIENTATION_X),
                    glm::vec3(1, 0, 0)), glm::radians(CAR_ORIENTATION_Y),
                    glm::vec3(0, 1, 0));
	    auto car_mvp = gProjection * mv * carModelMatrix;
	    auto car_mn = glm::mat3(glm::rotate(glm::rotate(glm::rotate(glm::rotate(glm::mat4(1.0f),
                    ry, glm::vec3(1, 0, 0)), rx, glm::vec3(0, 0, 1)),
	                glm::radians(CAR_ORIENTATION_X), glm::vec3(1, 0, 0)),
                    glm::radians(CAR_ORIENTATION_Y), glm::vec3(0, 1, 0)));

        //SV mv matrixes
        glm::vec3 sv_scale = glm::vec3(8*1.0f, 8 * 1.0f, 8 * 1.0f);
	    glm::mat4 svModelMatrix = glm::rotate(glm::rotate(glm::scale(glm::mat4(1.0f),
                    sv_scale), glm::radians(SV_ORIENTATION_X),
                    glm::vec3(1, 0, 0)), glm::radians(SV_ORIENTATION_Y),
                    glm::vec3(0, 1, 0));
	    auto sv_mvp = gProjection * mv * svModelMatrix;

        //draw 4 sv camera view
        view->setProgram(2);
        view->cleanView();
        view->setMVPMatrix(2, glm::value_ptr(sv_mvp));
        for(uint32_t index = 0; index < 4; index ++) {
	        view->renderView(images[index], width, height, mashes[index]);
        }

        glUseProgram(carModelProgram.getHandle());
        // Set matrices
	    glUniformMatrix4fv(mvpUniform, 1, GL_FALSE, glm::value_ptr(car_mvp)); 
	    glUniformMatrix4fv(mvUniform, 1, GL_FALSE, glm::value_ptr(mv)); 
	    glUniformMatrix3fv(mnUniform, 1, GL_FALSE, glm::value_ptr(car_mn)); 
	    modelLoader.Draw(carModelProgram.getHandle());
	    glFinish();

        eglSwapBuffers(dpy, surface);
        usleep(5000);
    }

    sleep(2);
    ALOGI("End of the SV Textures with car test");
    glFinish();
}


