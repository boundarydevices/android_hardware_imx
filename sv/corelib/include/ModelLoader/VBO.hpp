/*
*
* Copyright © 2017 NXP
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

#pragma once

#include <GLES3/gl3.h>
#include <assimp/scene.h>

///vertex buffer indices
enum VBOindices{P_VERTEX,P_NORMAL,P_TEXCOORD,P_INDEX};
///data structure to store vertex/texture/faces data
typedef GLfloat Vertex[3];
///data structure to store texture data
typedef GLfloat Coord[2];

///@brief Vertex buffer objects structure
class VBO
{
protected:
    ///pointers to all buffers: vertex, normal, texcoordinate and faces
    GLuint buffer[4] = {0};
    ///pointer vertex array
    GLuint vao;
    ///number of indice
    //GLuint indices;

    GLuint count;
    int matId;

public:
	VBO(Vertex *vertices, Vertex *normals, Coord *texcoords, int count, int matId);
	~VBO();

    void CreateRenderableObject(int id, aiMesh* mesh);

	GLuint GetVAO(){ return vao; }
	GLuint GetCount(){ return count; }
	int GetMatId(){ return matId; }
};
