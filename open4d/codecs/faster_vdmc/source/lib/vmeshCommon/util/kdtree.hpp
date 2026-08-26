/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2022, ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of the ISO/IEC nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <nanoflann/nanoflann.hpp>
#include <nanoflann/KDTreeVectorOfVectorsAdaptor.h>
#include <vector>

#include "util/vector.hpp"

namespace vmesh {

//============================================================================

// A version of nanoflann::metric_L2 that forces metric (distance)
// calculations to be performed using double precision floats.
// NB: by default nanoflann::metric_L2 will be used with the metric
//     type of T = num_t (the coordinate type).

struct metric_L2_vmesh {
  template<class T, class DataSource>
  struct traits {
    using distance_t = nanoflann::L2_Adaptor<T, DataSource, T>;
  };
};

template<class T>
using KdTree = KDTreeVectorOfVectorsAdaptor<std::vector<Vec3<T>>,
                                            T,
                                            3,
                                            metric_L2_vmesh,
                                            int32_t>;
template<class T>
using KdTree_tex = KDTreeVectorOfVectorsAdaptor<std::vector<Vec2<T>>,
                                                T,
                                                2,
                                                metric_L2_vmesh,
                                                int32_t>;

//============================================================================

}  // namespace vmesh
