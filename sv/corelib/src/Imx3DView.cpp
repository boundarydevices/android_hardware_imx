/*
 *
 * Copyright © 2017-2020 NXP
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "Imx3DView.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <cutils/log.h>
#include <stdio.h>
#include <unistd.h>

using namespace std;

namespace imx {
/***************************************************************************************
***************************************************************************************/
string Imx3DView::getEGLError(void) {
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

Imx3DView::Imx3DView() {
    current_prog = 0;
    // glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA);
    // glBlendFunc(GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    // glEnable(GL_CULL_FACE);
    // glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

Imx3DView::Imx3DView(vector<Vector3d>& evsRotations, vector<Vector3d>& evsTransforms,
                     vector<Matrix<double, 3, 3>>& Ks, vector<Matrix<double, 1, 4>>& Ds) {
    current_prog = 0;
    // glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA);
    // glBlendFunc(GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    mInitial = false;
    // glEnable(GL_CULL_FACE);
    // glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mEvsRotations = evsRotations;
    mEvsTransforms = evsTransforms;
    mKs = Ks;
    mDs = Ds;
}

/***************************************************************************************
***************************************************************************************/
Imx3DView::~Imx3DView() {
    for (int i = v_obj.size() - 1; i >= 0; i--) {
        glDeleteTextures(1, &v_obj[i].tex);
        glDeleteBuffers(1, &v_obj[i].vbo);
    }
    for (int i = render_prog.size() - 1; i >= 0; i--) render_prog[i].destroyShaders();
}

/***************************************************************************************
***************************************************************************************/
int Imx3DView::addProgram(const char* v_shader, const char* f_shader) {
    // load and compiler vertex/fragment shaders.
    Programs new_prog;
    if (new_prog.loadShaders(v_shader, f_shader) == -1) // Non-overlap regions
    {
        cout << "Render program was not loaded" << endl;
        return (-1);
    }
    render_prog.push_back(new_prog);
    return (render_prog.size() - 1);
}

/***************************************************************************************
***************************************************************************************/
int Imx3DView::setProgram(uint index) {
    if (index >= render_prog.size()) {
        cout << "A program with index " << index << " doesn't exist" << endl;
        return (-1);
    }
    current_prog = index;
    glUseProgram(render_prog[index].getHandle());
    return (0);
}

/***************************************************************************************
***************************************************************************************/
void Imx3DView::cleanView() {
    // Clear background.
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/***************************************************************************************
***************************************************************************************/
int Imx3DView::addMesh(string filename) {
    ///////////////////////////////// Load vertices arrays ///////////////////////////////
    vertices_obj vo_tmp;
    GLfloat* vert;
    vLoad(&vert, &vo_tmp.num, filename);

    //////////////////////// Camera textures initialization /////////////////////////////

    glGenVertexArrays(1, &vo_tmp.vao);
    glGenBuffers(1, &vo_tmp.vbo);

    bufferObjectInit(&vo_tmp.vao, &vo_tmp.vbo, vert, vo_tmp.num);
    texture2dInit(&vo_tmp.tex);

    v_obj.push_back(vo_tmp);

    if (vert)
        free(vert);

    return (v_obj.size() - 1);
}

int Imx3DView::addMesh(float* data, int data_num) {
    ///////////////////////////////// Load vertices arrays ///////////////////////////////
    vertices_obj vo_tmp;
    GLfloat* vert = data;
    vo_tmp.num = data_num;

    //////////////////////// Camera textures initialization /////////////////////////////
    glGenVertexArrays(1, &vo_tmp.vao);
    glGenBuffers(1, &vo_tmp.vbo);

    bufferObjectInit(&vo_tmp.vao, &vo_tmp.vbo, vert, vo_tmp.num);
    texture2dInit(&vo_tmp.tex);
    ALOGI("addMash tex %d, vbo %d, vao %d", vo_tmp.tex, vo_tmp.vbo, vo_tmp.vao);

    v_obj.push_back(vo_tmp);

    return (v_obj.size() - 1);
}

/***************************************************************************************
***************************************************************************************/
// 2D texture init
void Imx3DView::texture2dInit(GLuint* texture) {
    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/***************************************************************************************
***************************************************************************************/
void Imx3DView::bufferObjectInit(GLuint* text_vao, GLuint* text_vbo, GLfloat* vert, int num) {
    // rectangle
    glBindBuffer(GL_ARRAY_BUFFER, *text_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * SV_ATTRIBUTE_NUM * num, &vert[0],
                 GL_DYNAMIC_DRAW);
    glBindVertexArray(*text_vao);

    // Position attribute
    glVertexAttribPointer(0, SV_VERTEX_NUM, GL_FLOAT, GL_FALSE, SV_ATTRIBUTE_NUM * sizeof(GLfloat),
                          (GLvoid*)0);
    glEnableVertexAttribArray(0);

    // TexCoord attribute
    glVertexAttribPointer(1, SV_TEXTURE_NUM, GL_FLOAT, GL_FALSE, SV_ATTRIBUTE_NUM * sizeof(GLfloat),
                          (GLvoid*)(SV_VERTEX_NUM * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    // TexCoord attribute
    glVertexAttribPointer(2, SV_ALPHA_NUM, GL_FLOAT, GL_FALSE, SV_ATTRIBUTE_NUM * sizeof(GLfloat),
                          (GLvoid*)((SV_VERTEX_NUM + SV_TEXTURE_NUM) * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

/***************************************************************************************
***************************************************************************************/
// Load vertices arrays
void Imx3DView::vLoad(GLfloat** vert, int* num, string filename) {
    ifstream input(filename.c_str());
    *num = count(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>(),
                 '\n'); // Get line number from the array file
    input.clear();
    input.seekg(0, ios::beg); // Returning to the beginning of fstream

    *vert = NULL;
    *vert = (GLfloat*)malloc((*num) * SV_ATTRIBUTE_NUM * sizeof(GLfloat));
    if (*vert == NULL) {
        cout << "Memory allocation did not complete successfully" << endl;
    }
    for (int k = 0; k < (*num) * SV_ATTRIBUTE_NUM; k++) {
        input >> (*vert)[k];
    }
    input.close();
}

/***************************************************************************************
***************************************************************************************/

void Imx3DView::reloadMesh(int index, string filename) {
    GLfloat* vert;
    vLoad(&vert, &v_obj[index].num, filename);
    glBindBuffer(GL_ARRAY_BUFFER, v_obj[index].vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 5 * v_obj[index].num, &vert[0],
                 GL_DYNAMIC_DRAW);
    if (vert)
        free(vert);
}

/***************************************************************************************
***************************************************************************************/
int Imx3DView::addBuffer(GLfloat* buf, int num) {
    ///////////////////////////////// Load vertices arrays ///////////////////////////////
    vertices_obj vo_tmp;
    vo_tmp.num = num;

    //////////////////////// Camera textures initialization /////////////////////////////
    glGenVertexArrays(1, &vo_tmp.vao);
    glGenBuffers(1, &vo_tmp.vbo);

    glBindBuffer(GL_ARRAY_BUFFER, vo_tmp.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * SV_VERTEX_NUM * num, &buf[0], GL_DYNAMIC_DRAW);

    v_obj.push_back(vo_tmp);

    return (v_obj.size() - 1);
}

/***************************************************************************************
***************************************************************************************/
int Imx3DView::setBufferAsAttr(int buf_num, int prog_num, char* atr_name) {
    if ((buf_num < 0) || (buf_num >= (int)v_obj.size()) || (prog_num < 0) ||
        (prog_num >= (int)render_prog.size()))
        return (-1);
    glBindBuffer(GL_ARRAY_BUFFER, v_obj[buf_num].vbo);
    glBindVertexArray(v_obj[buf_num].vao);
    GLint position_attribute = glGetAttribLocation(render_prog[prog_num].getHandle(), atr_name);
    glVertexAttribPointer(position_attribute, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(position_attribute);
    return (0);
}

/***************************************************************************************
***************************************************************************************/
int Imx3DView::setMVPMatrix(int prog_num, float* mvpMatrix) {
    if ((prog_num < 0) || (prog_num >= (int)render_prog.size()))
        return (-1);
    GLint mvpLocation = glGetUniformLocation(render_prog[prog_num].getHandle(), "mvpMatrix");
    glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, (GLfloat*)mvpMatrix);
    return (0);
}

/***************************************************************************************
***************************************************************************************/
void Imx3DView::renderBuffer(int buf_num, int type, int vert_num) {
    glBindVertexArray(v_obj[buf_num].vao);
    switch (type) {
        case 0:
            glLineWidth(2.0);
            glDrawArrays(GL_LINES, 0, vert_num);
            break;
        case 1:
            glBeginTransformFeedback(GL_POINTS);
            glDrawArrays(GL_POINTS, 0, vert_num);
            glEndTransformFeedback();
            break;
        default:
            break;
    }
}

/***************************************************************************************
***************************************************************************************/
void Imx3DView::updateBuffer(int buf_num, GLfloat* buf, int num) {
    v_obj[buf_num].num = num;
    glBindBuffer(GL_ARRAY_BUFFER, v_obj[buf_num].vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * SV_VERTEX_NUM * num, &buf[0], GL_DYNAMIC_DRAW);
}

/***************************************************************************************
***************************************************************************************/
void Imx3DView::renderView(shared_ptr<unsigned char> distort, uint32_t w, uint32_t h, int mesh) {
    ALOGI("renderView with w %d, h %d", w, h);

    glBindVertexArray(v_obj[mesh].vao);

    GLenum texture;
    char textureName[32];
    auto image = distort.get();
    texture = GL_TEXTURE0;
    sprintf(textureName, "myTexture%d", 0);
    ALOGI("Texture: %s with image %p", textureName, image);
    glActiveTexture(texture);
    glBindTexture(GL_TEXTURE_2D, v_obj[mesh].tex);
    glUniform1i(glGetUniformLocation(render_prog[current_prog].getHandle(),
                                     (const GLchar*)textureName),
                0);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, image);

    glDrawArrays(GL_TRIANGLES, 0, v_obj[mesh].num);
    glBindVertexArray(0);
    glFinish();
}

bool Imx3DView::prepareGL(uint32_t output_w, uint32_t output_h) {
    EGLDisplay dpy;
    EGLSurface surface;
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        ALOGE("Failed to get egl display: %s", getEGLError().c_str());
        return false;
    }

    EGLint majorVersion;
    EGLint minorVersion;
    if (!eglInitialize(dpy, &majorVersion, &minorVersion)) {
        ALOGE("Failed to initialize EGL: %s", getEGLError().c_str());
        return false;
    }

    // Hardcoded to RGBx output display
    const EGLint config_attribs[] = {// Tag                  Value
                                     EGL_RENDERABLE_TYPE,
                                     EGL_OPENGL_ES2_BIT,
                                     EGL_RED_SIZE,
                                     8,
                                     EGL_GREEN_SIZE,
                                     8,
                                     EGL_BLUE_SIZE,
                                     8,
                                     EGL_NONE};

    // Select OpenGL ES v 3
    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};

    // Select the configuration that "best" matches our desired characteristics
    EGLConfig egl_config;
    EGLint num_configs;
    if (!eglChooseConfig(dpy, config_attribs, &egl_config, 1, &num_configs)) {
        ALOGE("eglChooseConfig() failed with error %s", getEGLError().c_str());
        return false;
    }

    EGLint surface_attribs[] = {EGL_WIDTH, (EGLint)output_w, EGL_HEIGHT, (EGLint)output_h,
                                EGL_NONE};
    surface = eglCreatePbufferSurface(dpy, egl_config, surface_attribs);
    if (surface == EGL_NO_SURFACE) {
        ALOGE("Failed to create OpenGL ES Context %s", getEGLError().c_str());
        return false;
    }

    EGLContext context = eglCreateContext(dpy, egl_config, EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        ALOGE("Failed to create OpenGL ES Context %s", getEGLError().c_str());
        return false;
    }

    // Activate our render target for drawing
    if (!eglMakeCurrent(dpy, surface, surface, context)) {
        ALOGE("Failed to make the OpenGL ES Context current %s", getEGLError().c_str());
        return false;
    } else {
        ALOGI("We made our context current!  :)");
    }

    if (addProgram(s_v_shader, s_f_shader) == -1)
        return false;
    if (addProgram(s_v_shader_line, s_f_shader_line) == -1)
        return false;
    if (addProgram(s_v_shader_bowl, s_f_shader_bowl) == -1)
        return false;
    if (setProgram(0) == -1)
        return false;

    mGrid = new CurvilinearGrid(SV_ANGLES_IN_PI, SV_Z_NOP, SV_X_STEP, output_w, output_h,
                                mEvsRotations, mEvsTransforms, mKs, mDs);

    mGrid->createGrid(SV_RADIUS); // Calculate grid points
    cleanView();

    if (setProgram(2) == 0) {
        for (uint32_t index = 0; index < 4; index++) {
            float* data = nullptr;
            int data_num = mGrid->getMashes(&data, index);
            int grid_buf = addMesh(data, data_num / SV_ATTRIBUTE_NUM);
            if (data != nullptr) {
                free(data);
                data = nullptr;
            }
            mAshes.push_back(grid_buf);
        }

        Vector3d r = {1, 0, 0};
        AngleAxisd rotation_vector(0, r);
        Isometry3d T = Isometry3d::Identity();
        T.rotate(rotation_vector);
        auto modelMatrix = T.matrix();
        float mvp_matrix[16];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                mvp_matrix[i * 4 + j] = (float)modelMatrix(i, j);
            }

        setMVPMatrix(2, mvp_matrix);
    }
    return true;
}

bool Imx3DView::renderSV(vector<shared_ptr<unsigned char>> images, char* outbuf, uint32_t input_w,
                         uint32_t input_h, uint32_t output_w, uint32_t output_h) {
    if (!mInitial) {
        prepareGL(output_w, output_h);
        mInitial = true;
    }

    for (uint32_t index = 0; index < 4; index++)
        renderView(images[index], input_w, input_h, mAshes[index]);

    // read the buffer from surface
    glReadPixels(0, 0, output_w, output_h, GL_RGBA, GL_UNSIGNED_BYTE, outbuf);

    return true;
}
} // namespace imx
