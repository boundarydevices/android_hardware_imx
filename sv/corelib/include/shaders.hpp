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

/******************************* Vertices shaders ************************************/

// Vertices shader without view and projection parameters for exposure correction
static const char s_v_shader[] = " #version 300 es \n "
                                 " layout(location = 0) in vec4 vPosition; \n "
                                 " layout(location = 1) in vec2 vTexCoord; \n "
                                 " out vec2 TexCoord; \n "
                                 " void main() \n "
                                 " { \n "
                                 " gl_Position = vPosition; \n "
                                 " TexCoord = vTexCoord; \n "
                                 " } \n ";

static const char s_f_shader[] = "#version 300 es \n"
                                 " precision mediump float;\n "
                                 " in vec2 TexCoord; \n "
                                 " out vec4 fragColor; \n "
                                 " uniform sampler2D myTexture; \n "
                                 " void main() \n "
                                 " {\n "
#if (defined(IMX6QP) && defined(CAMERAS)) || defined(IMAGES)
                                 " fragColor = texture(myTexture, TexCoord); \n "
#else
                                 " fragColor = vec4(texture(myTexture, TexCoord).bgr, 0); \n "
#endif
                                 " }\n ";

static const char s_v_shader_line[] = " #version 300 es \n "
                                      "uniform mat4 mvpMatrix;                   \n"
                                      " layout(location = 0) in vec3 vPosition; \n "
                                      " void main() \n "
                                      " { \n "
                                      " gl_Position = mvpMatrix * vec4(vPosition, 1.0f); \n "
                                      " gl_PointSize = 3.0f;\n"
                                      " } \n ";

static const char s_f_shader_line[] = "#version 300 es \n"
                                      "precision mediump float;"
                                      "layout(location = 0) out vec4 fragColor; \n "
                                      "void main() {"
                                      "  fragColor = vec4(1.0, 1.0, 0.0, 1.0);"
                                      "}";

static const char s_v_shader_bowl[] = " #version 300 es \n "
                                      "uniform mat4 mvpMatrix;                  \n"
                                      " layout(location = 0) in vec3 vPosition; \n "
                                      " layout(location = 1) in vec2 vTexCoord; \n "
                                      " layout(location = 2) in float vTexAlpha; \n "
                                      " out vec2 TexCoord; \n "
                                      " out float TexAlpha; \n "
                                      " void main() \n "
                                      " { \n "
                                      " gl_Position = mvpMatrix * vec4(vPosition.xyz, 1.0f); \n "
                                      " TexCoord = vTexCoord; \n "
                                      " TexAlpha = vTexAlpha; \n "
                                      " } \n ";

static const char s_f_shader_bowl[] =
        "#version 300 es \n"
        " precision mediump float;\n "
        " in vec2 TexCoord; \n "
        " in float TexAlpha; \n "
        " out vec4 fragColor; \n "
        " uniform sampler2D myTexture0; \n "
        " void main() \n "
        " {\n "
        " fragColor = vec4(texture(myTexture0, TexCoord).rgb, TexAlpha); \n "
        " }\n ";

static const char s_v_shader_model[] = "#version 300 es \n"
                                       " \n"
                                       "layout(location = 0) in vec3 position; \n"
                                       "layout(location = 1) in vec3 normal; \n"
                                       " \n"
                                       "uniform mat4 mvp, mv; \n"
                                       "uniform mat3 mn; \n"
                                       " \n"
                                       "//light \n"
                                       "const vec3 lightPosition = vec3( 0.0, 0.0, 20.0 ); \n"
                                       " \n"
                                       "//material \n"
                                       " \n"
                                       "out vec3 eyePosition, eyeNormal, eyeLight; \n"
                                       "out vec4 normPosition; \n"
                                       " \n"
                                       "void main() \n"
                                       "{ \n"
                                       "    normPosition = mvp*vec4(position,1); \n"
                                       "    gl_Position = normPosition;	 \n"
                                       " \n"
                                       "    eyePosition = (mv*vec4(position,1)).xyz; \n"
                                       "    eyeLight = (mv*vec4(lightPosition,1)).xyz; \n"
                                       "    eyeNormal = normalize(mn*normal); \n"
                                       "} \n";

static const char s_f_shader_model[] =
        "#version 300 es \n"
        " precision mediump float;\n "
        " \n"
        "uniform vec3 ambient; \n"
        "uniform vec3 diffuse; \n"
        "uniform vec3 specular; \n"
        " \n"
        "in vec3 eyePosition, eyeNormal, eyeLight; \n"
        "in vec4 normPosition; \n"
        " \n"
        "out vec4 fragColor; \n"
        " \n"
        "vec3 lightColor = vec3(1.0); \n"
        "void main() \n"
        "{ \n"
        "	vec3 normPos = normPosition.xyz/normPosition.w; \n"
        "    vec3 N = normalize(eyeNormal); \n"
        "    vec3 L = normalize(eyeLight-eyePosition); \n"
        "    vec3 finalColor = vec3(0.0); \n"
        " \n"
        "    //Blin-Phong model \n"
        "    finalColor = ambient; \n"
        "    float lambertTerm = dot(L, N); \n"
        "    if(lambertTerm >= 0.0) \n"
        "    { \n"
        "        finalColor += lightColor * diffuse * lambertTerm; \n"
        " \n"
        "        //Phong model \n"
        "        vec3 V = normalize(-eyePosition); \n"
        "        vec3 R = reflect(-L, N); \n"
        "        float spec = pow(dot(R, V), 128.0); \n"
        " \n"
        "        if(spec >= 0.0) \n"
        "        { \n"
        "            finalColor += lightColor * specular * spec; \n"
        "        } \n"
        "    } \n"
        " \n"
        "    fragColor = vec4(finalColor, 1.0); \n"
        "} \n";
