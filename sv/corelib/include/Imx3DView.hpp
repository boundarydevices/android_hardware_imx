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


#ifndef SRC_RENDER_HPP_
#define SRC_RENDER_HPP_



#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

/*******************************************************************************************
 * Includes
 *******************************************************************************************/
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <vector>

// String
#include <string>

//OpenGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

//Shaders
#include "gl_shaders.hpp"
#include "shaders.hpp"

#include "ImxSurroundViewTypes.hpp"
#include "Imx3DGrid.hpp"

/**********************************************************************************************************************
 * Macros
 **********************************************************************************************************************/
#define GL_PIXEL_TYPE GL_RGBA
#define CAM_PIXEL_TYPE V4L2_PIX_FMT_RGB32

//3 * float for vetex
#define SV_VERTEX_NUM 3
//2 * float for vetex
#define SV_TEXTURE_NUM 2
//1 * float for alpha 
#define SV_ALPHA_NUM 1

#define SV_ATTRIBUTE_NUM (SV_VERTEX_NUM + SV_TEXTURE_NUM + SV_ALPHA_NUM)

#define SV_X_STEP (0.2)
#define SV_Z_NOP (16)
#define SV_ANGLES_IN_PI (64)
#define SV_RADIUS (0.75)


namespace imx {
/**********************************************************************************************************************
 * Types
 **********************************************************************************************************************/
struct vertices_obj
{
	GLuint	vao;
	GLuint	vbo;
	GLuint	tex;
	int		num;
};

/*******************************************************************************************
 * Classes
 *******************************************************************************************/
/* Render class */
class Imx3DView {
public:
	Imx3DView();
	Imx3DView(vector<Vector3d> &evsRotations, vector<Vector3d> &evsTransforms,
                  vector<Matrix<double, 3, 3>> &Ks, vector<Matrix<double, 1, 4>> &Ds);
	~Imx3DView();

        string getEGLError(void);
	int addProgram(const char* v_shader, const char* f_shader);
	int setProgram(uint index);
	void cleanView();
	int addMesh(string filename);
	void reloadMesh(int index, string filename);
	int getVerticesNum(uint num) {if (num < v_obj.size()) return (v_obj[num].num); return (-1);}
	int addBuffer(GLfloat* buf, int num);
	int setBufferAsAttr(int buf_num, int prog_num, char* atr_name);
	int setMVPMatrix(int prog_num, float* mvpMatrix);
	void renderBuffer(int buf_num, int type, int vert_num);
	void updateBuffer(int buf_num, GLfloat* buf, int num);
	void renderView(shared_ptr<unsigned char> distort,
	                  uint32_t w, uint32_t h,
	                  int mesh);
	int addMesh(float *data, int data_num);
	bool prepareGL(uint32_t output_w, uint32_t output_h);
	bool renderSV(vector<shared_ptr<unsigned char>> images, char *outbuf,
                                        uint32_t input_w, uint32_t input_h,
                                        uint32_t output_w, uint32_t output_h);


private:
	int current_prog;
	std::vector<Programs> render_prog;
	std::vector<vertices_obj> v_obj;

	vector<int> mAshes;
	imx::CurvilinearGrid *mGrid;
	void vLoad(GLfloat** vert, int* num, string filename);
	void bufferObjectInit(GLuint* text_vao, GLuint* text_vbo, GLfloat* vert, int num);
	void texture2dInit(GLuint* texture);
	bool mInitial;
	vector<Vector3d> mEvsRotations;
	vector<Vector3d> mEvsTransforms;
	vector<Matrix<double, 3, 3>> mKs;
	vector<Matrix<double, 1, 4>> mDs;
};
} //namespace imx
#endif /* SRC_RENDER_HPP_ */
