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

#include "gl_shaders.hpp"

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
void Programs::destroyShaders() {
    if (program.programHandle) {
        glDeleteShader(program.vertShaderNum);
        glDeleteShader(program.pixelShaderNum);
        glDeleteProgram(program.programHandle);
        glUseProgram(0);
        program.programHandle = 0;
    }
}

/**************************************************************************************************************
 *
 * @brief  			Load shaders
 *
 * @param   		const char* v_shader - vertex shader name
 *					const char* p_shader - pixel shader name
 *
 * @return 			The function returns 0 if shaders were loaded successfully. Otherwise
 *-1 has been returned.
 *
 * @remarks 		The function loads and compiles vertex/pixel shaders and links the program
 *object specified by program.
 *
 **************************************************************************************************************/
int Programs::loadShaders(const char* v_shader, const char* p_shader) {
    program.vertShaderNum = glCreateShader(GL_VERTEX_SHADER);
    program.pixelShaderNum = glCreateShader(GL_FRAGMENT_SHADER);

    if (compileShader(v_shader, program.vertShaderNum) == -1) {
        return (-1);
    }

    if (compileShader(p_shader, program.pixelShaderNum) == -1) {
        return (-1);
    }

    program.programHandle = glCreateProgram();
    if (program.programHandle == 0) {
        cout << "Error creating shader program object" << endl;
        return (-1);
    }

    glAttachShader(program.programHandle, program.vertShaderNum);
    glAttachShader(program.programHandle, program.pixelShaderNum);

    glLinkProgram(program.programHandle);
    // Check if linking succeeded.
    GLint linked = false;
    glGetProgramiv(program.programHandle, GL_LINK_STATUS, &linked);
    if (!linked) {
        // Retrieve error buffer size.
        GLint errorBufSize, errorLength;
        glGetShaderiv(program.programHandle, GL_INFO_LOG_LENGTH, &errorBufSize);
        if (errorBufSize) {
            char* infoLog = (char*)malloc(errorBufSize * sizeof(char) + 1);
            if (infoLog != NULL) {
                // Retrieve error.
                glGetProgramInfoLog(program.programHandle, errorBufSize, &errorLength, infoLog);
                cout << infoLog << endl;
                if (infoLog) free(infoLog);
            }
        }
        return (-1);
    }
    return (0);
}

/**************************************************************************************************************
 *
 * @brief  			Compile a vertex or pixel shader
 *
 * @param   		const char* source - vertix or pixel shader name
 *					GLuint num - vertix or pixel shader number
 *
 * @return 			The function returns 0 if shaders were compilled successfully.
 *Otherwise -1 has been returned.
 *
 * @remarks 		The function compiles vertex or pixel shader.
 *
 **************************************************************************************************************/
int Programs::compileShader(const char* source, GLuint num) {
    glShaderSource(num, 1, (const char**)&source, NULL);
    glCompileShader(num);

    GLint compiled = 0;
    glGetShaderiv(num, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        // Retrieve error buffer size.
        GLint errorBufSize, errorLength;
        glGetShaderiv(num, GL_INFO_LOG_LENGTH, &errorBufSize);

        char* infoLog = (char*)malloc(errorBufSize * sizeof(char) + 1);
        if (infoLog) {
            // Retrieve error.
            glGetShaderInfoLog(num, errorBufSize, &errorLength, infoLog);
            cout << infoLog << endl;
            if (infoLog) free(infoLog);
        }
        return (-1);
    }
    return 0;
}
