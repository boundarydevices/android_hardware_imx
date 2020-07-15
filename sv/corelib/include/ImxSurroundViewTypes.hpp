/*
 *  Copyright 2020 NXP.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#ifndef IMX_SURROUNDVIEW_TYPES_HPP_
#define IMX_SURROUNDVIEW_TYPES_HPP_

using namespace std;
namespace imx {

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

// base size 2d type template.
template <typename T>
struct Size2dBase {
  // width of size.
  T width;

  // height of size.
  T height;

  Size2dBase() : width(0), height(0) {}

  Size2dBase(T width_, T height_) : width(width_), height(height_) {}

  bool IsValid() const { return width > 0 && height > 0; }

  bool operator==(const Size2dBase& rhs) const {
    return width == rhs.width && height == rhs.height;
  }

  Size2dBase& operator=(const Size2dBase& rhs) {
    width = rhs.width;
    height = rhs.height;
    return *this;
  }
};

// integer type size.
typedef Size2dBase<int> Size2dInteger;

// float type size.
typedef Size2dBase<float> Size2dFloat;

// base size 3d type template.
template <typename T>
struct Size3dBase {
  T x;
  T y;
  T z;

  Size3dBase() : x(0), y(0), z(0) {}

  Size3dBase(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}

  bool IsValid() const { return true; }

  bool operator==(const Size3dBase& rhs) const {
    return x == rhs.x && y == rhs.y && z == rhs.z;
  }

  Size3dBase& operator=(const Size3dBase& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    return *this;
  }
};

// integer type size.
typedef Size3dBase<int> Size3dInteger;

// float type size.
typedef Size3dBase<float> Size3dFloat;

} //namespace imx
#endif


