/*
 * Copyright 2017 NXP.
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

#include "Layer.h"

namespace fsl {

Layer::Layer()
  : busy(false), zorder(0), origType(LAYER_TYPE_INVALID),
    type(LAYER_TYPE_INVALID), handle(NULL), lastHandle(NULL),
    transform(0), blendMode(BLENDING_NONE), planeAlpha(0),
    color(0), flags(0), acquireFence(-1),releaseFence(-1),
    index(-1), isHdrMode(false), isOverlay(false), priv(NULL)
{
    sourceCrop.clear();
    displayFrame.clear();
    visibleRegion.clear();
}

bool Layer::isSolidColor()
{
    return type == LAYER_TYPE_SOLID_COLOR;
}

LayerVector::LayerVector() {
}

LayerVector::LayerVector(const LayerVector& rhs)
    : SortedVector<Layer*>(rhs) {
}

int LayerVector::do_compare(const void* lhs, const void* rhs) const
{
    // sort layers per layer-stack, then by z-order and finally by sequence
    const Layer* l = (*(Layer**)lhs);
    const Layer* r = (*(Layer**)rhs);

    uint32_t ls = l->zorder;
    uint32_t rs = r->zorder;
    return ls - rs;
}

}
