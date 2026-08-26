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

#include "util/vector.hpp"

namespace vmesh {

//============================================================================

template<typename T>
struct Box2 {
  Vec2<T> min;
  Vec2<T> max;

  Box2() {
    max = Vec2<T>(std::numeric_limits<T>::lowest(),
                  std::numeric_limits<T>::lowest());
    min =
      Vec2<T>(std::numeric_limits<T>::max(), std::numeric_limits<T>::max());
  }

  Box2(const Vec2<T>& min_, const Vec2<T>& max_) : min(min_), max(max_) {}

  void enclose(const Vec2<T> p) {
    min = min.min(p);
    max = max.max(p);
  }

  bool contains(const Vec2<T> point) const {
    return !(point.x() < min.x() || point.x() > max.x() || point.y() < min.y()
             || point.y() > max.y());
  }

  Box2 merge(const Box2& box) {
    Box2 output;

    output.min = min.min(box.min);
    output.max = max.max(box.max);
    return output;
  }

  bool intersects(const Box2& box) {
    return max.x() >= box.min.x() && min.x() <= box.max.x()
           && max.y() >= box.min.y() && min.y() <= box.max.y();
  }

  friend std::ostream& operator<<(std::ostream& os, const Box2& box) {
    os << box.min[0] << ' ' << box.min[1] << ' ' << box.max[0] << ' '
       << box.max[1];
    return os;
  }

  friend std::istream& operator>>(std::istream& is, Box2& box) {
    is >> box.min[0] >> box.min[1] >> box.max[0] >> box.max[1];
    return is;
  }
};

//============================================================================

template<typename T>
struct Box3 {
  Vec3<T> min;
  Vec3<T> max;
  bool    contains(const Vec3<T> point) const {
    return !(point.x() < min.x() || point.x() > max.x() || point.y() < min.y()
             || point.y() > max.y() || point.z() < min.z()
             || point.z() > max.z());
  }

  void insert(const Vec3<T> point) {
    min.x() = std::min(min.x(), point.x());
    min.y() = std::min(min.y(), point.y());
    min.z() = std::min(min.z(), point.z());
    max.x() = std::max(max.x(), point.x());
    max.y() = std::max(max.y(), point.y());
    max.z() = std::max(max.z(), point.z());
  }
  Box3 merge(const Box3& box) {
    min.x() = std::min(min.x(), box.min.x());
    min.y() = std::min(min.y(), box.min.y());
    min.z() = std::min(min.z(), box.min.z());
    max.x() = std::max(max.x(), box.max.x());
    max.y() = std::max(max.y(), box.max.y());
    max.z() = std::max(max.z(), box.max.z());
    return box;
  }

  void enclose(const Vec3<T> p) {
    min = min.min(p);
    max = max.max(p);
  }

  bool intersects(const Box3& box) const {
    return max.x() >= box.min.x() && min.x() <= box.max.x()
           && max.y() >= box.min.y() && min.y() <= box.max.y()
           && max.z() >= box.min.z() && min.z() <= box.max.z();
  }

  T dist2(const Vec3<T>& point) const {
    const T dx = std::max(std::max(min[0] - point[0], 0.0), point[0] - max[0]);
    const T dy = std::max(std::max(min[1] - point[1], 0.0), point[1] - max[1]);
    const T dz = std::max(std::max(min[2] - point[2], 0.0), point[2] - max[2]);
    return dx * dx + dy * dy + dz * dz;
  }

  friend std::ostream& operator<<(std::ostream& os, const Box3& box) {
    os << box.min[0] << ' ' << box.min[1] << ' ' << box.min[2] << ' '
       << box.max[0] << ' ' << box.max[1] << ' ' << box.max[2];
    return os;
  }
  friend std::istream& operator>>(std::istream& is, Box3& box) {
    is >> box.min[0] >> box.min[1] >> box.min[2] >> box.max[0] >> box.max[1]
      >> box.max[2];
    return is;
  }

  Box3 operator&(const Box3& rhs) {
    Box3 c;
    for (int i = 0; i < 3; i++) {
      c.min[i] = (min[i] > rhs.min[i]) ? min[i] : rhs.min[i];
      c.max[i] = (((min[i] + max[i]) < (rhs.min[i] + rhs.max[i]))
                    ? (min[i] + max[i])
                    : (rhs.min[i] + rhs.max[i]))
                 - c.min[i];
    }
    for (int i = 0; i < 3; i++) {
      if (c.max[i] <= 0) { c.min = c.max = Vec3<T>(T(0)); }
    }
    return Box3(c);
  }
  T volume() {
    return ((max[0] - min[0]) * (max[1] - min[1]) * (max[2] - min[2]));
  }
};

//============================================================================

}  // namespace vmesh