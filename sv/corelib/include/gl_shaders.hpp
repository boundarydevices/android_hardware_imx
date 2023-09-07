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

#ifndef GL_SHADERS_HPP_
#define GL_SHADERS_HPP_

/*********************************************************************************************************************
 * Includes
 *********************************************************************************************************************/
#include <stdio.h>
#include <stdlib.h>

#include <iostream>

// OpenGL
#include <GLES3/gl3.h>

using namespace std;
/*********************************************************************************************************************
 * Types
 *********************************************************************************************************************/
struct programInfo {
    GLuint vertShaderNum;  // Vertex shader id
    GLuint pixelShaderNum; // Pixel shader id
    GLuint programHandle;  // Program id
};

/*********************************************************************************************************************
 * Classes
 *********************************************************************************************************************/
// Programs class
class Programs {
public:
    /**************************************************************************************************************
     *
     * @brief  			Cleanup shaders
     *
     * @param   		-
     *
     * @return 			-
     *
     * @remarks 		The function frees the memory and invalidates the name associated with the
     *shader object specified by shader.
     *
     **************************************************************************************************************/
    void destroyShaders();
    /**************************************************************************************************************
     *
     * @brief  			Load shaders
     *
     * @param   		const char* v_shader - vertex shader name
     *					const char* p_shader - pixel shader name
     *
     * @return 			The function returns 0 if shaders were loaded successfully. Otherwise -1 has
     *been returned.
     *
     * @remarks 		The function loads and compiles vertex/pixel shaders and links the program
     *object specified by program.
     *
     **************************************************************************************************************/
    int loadShaders(const char* v_shader, const char* p_shader);
    GLuint getHandle() { return program.programHandle; };

private:
    programInfo program; // GL program
    /**************************************************************************************************************
     *
     * @brief  			Compile a vertex or pixel shader
     *
     * @param   		const char* source - vertix or pixel shader name
     *					GLuint num - vertix or pixel shader number
     *
     * @return 			The function returns 0 if shaders were compilled successfully. Otherwise -1 has
     *been returned.
     *
     * @remarks 		The function compiles vertex or pixel shader.
     *
     **************************************************************************************************************/
    int compileShader(const char* source, GLuint num);
};
#endif // GL_SHADERS_HPP_
