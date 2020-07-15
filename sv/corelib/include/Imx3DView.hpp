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
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

//Shaders
#include "gl_shaders.hpp"
#include "shaders.hpp"

#include "ImxSurroundViewTypes.hpp"

/**********************************************************************************************************************
 * Macros
 **********************************************************************************************************************/
#define GL_PIXEL_TYPE GL_RGBA
#define CAM_PIXEL_TYPE V4L2_PIX_FMT_RGB32

//3 * float for vetex
#define SV_VERTEX_NUM 3
//2 * float for vetex
#define SV_TEXTURE_NUM 2

#define SV_ATTRIBUTE_NUM (SV_VERTEX_NUM + SV_TEXTURE_NUM)

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
	~Imx3DView();

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


private:
	int current_prog;
	std::vector<Programs> render_prog;
	std::vector<vertices_obj> v_obj;

	void vLoad(GLfloat** vert, int* num, string filename);
	void bufferObjectInit(GLuint* text_vao, GLuint* text_vbo, GLfloat* vert, int num);
	void texture2dInit(GLuint* texture);
};
} //namespace imx
#endif /* SRC_RENDER_HPP_ */
