/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SHADER_REVERSE_LINE_H
#define SHADER_REVERSE_LINE_H

static const char vtxShader_reverseLine[] =
    "#version 300 es\n"
    "layout(location = 0) in vec4 vPosition;\n"
    "layout(location = 1) in vec4 aColor;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "  vColor = aColor;\n"
    "  gl_Position = vPosition;\n"
    "}\n";

static const char pixShader_reverseLine[] =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec4 vColor;\n"
    "out vec4 gColor;\n"
    "void main() {\n"
    "  gColor = vColor;\n"
    "}\n";

#endif // SHADER_SIMPLE_TEX_H
