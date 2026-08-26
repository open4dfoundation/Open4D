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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <istream>
#include <ostream>
#include <vector>
#include <limits>

#if defined(WIN32) || defined(_WIN32)
#  define NOMINMAX
#  include <Windows.h>
#endif

namespace vmesh {

//============================================================================

template<typename T>
class Vec2 {
public:
  T*       data() { return _vec; }
  const T* data() const { return _vec; }

  T*       begin() { return &_vec[0]; }
  const T* begin() const { return &_vec[0]; }

  T*       end() { return &_vec[2]; }
  const T* end() const { return &_vec[2]; }

  T& operator[](int32_t i) {
    assert(i < 2);
    return _vec[i];
  }

  const T& operator[](int32_t i) const {
    assert(i < 2);
    return _vec[i];
  }

  int32_t size() const { return 2; }

  T& r() { return _vec[0]; }
  T& g() { return _vec[1]; }

  const T& r() const { return _vec[0]; }
  const T& g() const { return _vec[1]; }

  T& x() { return _vec[0]; }
  T& y() { return _vec[1]; }

  const T& x() const { return _vec[0]; }
  const T& y() const { return _vec[1]; }

  void normalize() {
    const T norm2 = _vec[0] * _vec[0] + _vec[1] * _vec[1];
    if (norm2 != T(0)) {
      T invNorm = static_cast<T>(1) / std::sqrt(norm2);
      (*this) *= invNorm;
    }
  }

  T norm() const { return static_cast<T>(std::sqrt(norm2())); }
  T norm2() const { return (*this) * (*this); }

  T normInf() const {
    return std::max(std::max(std::abs(_vec[0]), std::abs(_vec[1])),
                    std::abs(_vec[1]));
  }

  Vec2& operator=(const Vec2& rhs) = default;

  Vec2& operator+=(const Vec2& rhs) {
    _vec[0] += rhs._vec[0];
    _vec[1] += rhs._vec[1];
    return *this;
  }

  Vec2& operator-=(const Vec2& rhs) {
    _vec[0] -= rhs._vec[0];
    _vec[1] -= rhs._vec[1];
    return *this;
  }

  Vec2& operator-=(const T a) {
    _vec[0] -= a;
    _vec[1] -= a;
    return *this;
  }

  Vec2& operator+=(const T a) {
    _vec[0] += a;
    _vec[1] += a;
    return *this;
  }

  Vec2& operator/=(const T a) {
    assert(a != 0);
    _vec[0] /= a;
    _vec[1] /= a;
    return *this;
  }

  Vec2& operator*=(const T a) {
    _vec[0] *= a;
    _vec[1] *= a;
    return *this;
  }

  Vec2& operator=(const T a) {
    _vec[0] = a;
    _vec[1] = a;
    return *this;
  }

  Vec2& operator=(const T* const rhs) {
    _vec[0] = rhs[0];
    _vec[1] = rhs[1];
    return *this;
  }

  T operator*(const Vec2& rhs) const {
    return (_vec[0] * rhs._vec[0] + _vec[1] * rhs._vec[1]);
  }

  T operator^(const Vec2& rhs) const {
    return _vec[0] * rhs._vec[1] - _vec[1] * rhs._vec[0];
  }

  Vec2 operator-() const { return Vec2<T>(-_vec[0], -_vec[1]); }

  friend Vec2 operator+(const Vec2& lhs, const Vec2& rhs) {
    return Vec2<T>(lhs._vec[0] + rhs._vec[0], lhs._vec[1] + rhs._vec[1]);
  }

  friend Vec2 operator+(const T lhs, const Vec2& rhs) {
    return Vec2<T>(lhs + rhs._vec[0], lhs + rhs._vec[1]);
  }

  friend Vec2 operator+(const Vec2& lhs, const T rhs) {
    return Vec2<T>(lhs._vec[0] + rhs, lhs._vec[1] + rhs);
  }

  friend Vec2 operator-(const Vec2& lhs, const Vec2& rhs) {
    return Vec2<T>(lhs._vec[0] - rhs._vec[0], lhs._vec[1] - rhs._vec[1]);
  }

  friend Vec2 operator-(const T lhs, const Vec2& rhs) {
    return Vec2<T>(lhs - rhs._vec[0], lhs - rhs._vec[1]);
  }

  friend Vec2 operator-(const Vec2& lhs, const T rhs) {
    return Vec2<T>(lhs._vec[0] - rhs, lhs._vec[1] - rhs);
  }

  friend Vec2 operator*(const T lhs, const Vec2& rhs) {
    return Vec2<T>(lhs * rhs._vec[0], lhs * rhs._vec[1]);
  }

  friend Vec2 operator*(const Vec2& lhs, const T rhs) {
    return Vec2<T>(lhs._vec[0] * rhs, lhs._vec[1] * rhs);
  }

  friend Vec2 operator/(const Vec2& lhs, const T rhs) {
    assert(rhs != 0);
    return Vec2<T>(lhs._vec[0] / rhs, lhs._vec[1] / rhs);
  }

  bool operator<(const Vec2& rhs) const {
    if (_vec[0] == rhs._vec[0]) { return (_vec[1] < rhs._vec[1]); }
    return (_vec[0] < rhs._vec[0]);
  }

  bool operator>(const Vec2& rhs) const {
    if (_vec[0] == rhs._vec[0]) { return (_vec[1] > rhs._vec[1]); }
    return (_vec[0] > rhs._vec[0]);
  }

  bool operator==(const Vec2& rhs) const {
    return (_vec[0] == rhs._vec[0] && _vec[1] == rhs._vec[1]);
  }

  bool operator!=(const Vec2& rhs) const {
    return (_vec[0] != rhs._vec[0] || _vec[1] != rhs._vec[1]);
  }

  friend std::ostream& operator<<(std::ostream& os, const Vec2& vec) {
    os << vec[0] << ' ' << vec[1];
    return os;
  }

  friend std::istream& operator>>(std::istream& is, Vec2& vec) {
    is >> vec[0] >> vec[1];
    return is;
  }

  Vec2(const T a) { _vec[0] = _vec[1] = a; }

  Vec2(const T x, const T y) {
    _vec[0] = x;
    _vec[1] = y;
  }
  Vec2(const Vec2& vec) {
    _vec[0] = vec._vec[0];
    _vec[1] = vec._vec[1];
  }

  Vec2 min(const Vec2& vec) {
    Vec2 min;
    min.x() = std::min(vec.x(), x());
    min.y() = std::min(vec.y(), y());
    return min;
  }

  Vec2 max(const Vec2& vec) {
    Vec2 max;
    max.x() = std::max(vec.x(), x());
    max.y() = std::max(vec.y(), y());
    return max;
  }

  Vec2(const T* vec) : Vec2(vec[0], vec[1]){};

  Vec2()  = default;
  ~Vec2() = default;

  template<typename S>
  Vec2(const Vec2<S>& vec) {
    _vec[0] = (T)vec.x();
    _vec[1] = (T)vec.y();
  }

private:
  T _vec[2];
};

//============================================================================

template<typename T>
class Vec3 {
public:
  T*       data() { return _vec; }
  const T* data() const { return _vec; }

  T*       begin() { return &_vec[0]; }
  const T* begin() const { return &_vec[0]; }

  T*       end() { return &_vec[3]; }
  const T* end() const { return &_vec[3]; }

  const uint8_t* ptr() {
    return reinterpret_cast<const uint8_t*>(
      reinterpret_cast<const void*>(_vec));
  }

  T& operator[](const int32_t index) {
    assert(index < 3);
    return _vec[index];
  }

  const T& operator[](const int32_t index) const {
    assert(index < 3);
    return _vec[index];
  }

  int32_t size() const { return 3; }

  T& i() { return _vec[0]; }
  T& j() { return _vec[1]; }
  T& k() { return _vec[2]; }

  const T& i() const { return _vec[0]; }
  const T& j() const { return _vec[1]; }
  const T& k() const { return _vec[2]; }
  T&       r() { return _vec[0]; }
  T&       g() { return _vec[1]; }
  T&       b() { return _vec[2]; }

  const T& r() const { return _vec[0]; }
  const T& g() const { return _vec[1]; }
  const T& b() const { return _vec[2]; }
  T&       x() { return _vec[0]; }
  T&       y() { return _vec[1]; }
  T&       z() { return _vec[2]; }

  const T& x() const { return _vec[0]; }
  const T& y() const { return _vec[1]; }
  const T& z() const { return _vec[2]; }

  T norm() const { return static_cast<T>(std::sqrt(norm2())); }
  T norm2() const { return (*this) * (*this); }
  T normInf() const { return std::max(_vec[0], std::max(_vec[1], _vec[2])); }

  void normalize() {
    const T n2 = norm2();
    if (n2 != T(0)) {
      T invNorm = static_cast<T>(1) / std::sqrt(n2);
      (*this) *= invNorm;
    }
  }

   Vec3<T> cross(const Vec3<T>& b) const {
    return Vec3<T>(_vec[1] * b[2] - _vec[2] * b[1],
                   _vec[2] * b[0] - _vec[0] * b[2],
                   _vec[0] * b[1] - _vec[1] * b[0]);
  }

  Vec3<T> cross(const Vec3<T>& b) {
    return Vec3<T>(_vec[1] * b[2] - _vec[2] * b[1],
                   _vec[2] * b[0] - _vec[0] * b[2],
                   _vec[0] * b[1] - _vec[1] * b[0]);
  }


  Vec3& operator=(const Vec3& rhs) = default;

  template<typename D>
  Vec3& operator=(const Vec3<D>& rhs) {
    _vec[0] = rhs[0];
    _vec[1] = rhs[1];
    _vec[2] = rhs[2];
    return *this;
  }

  Vec3& operator+=(const Vec3& rhs) {
    _vec[0] += rhs._vec[0];
    _vec[1] += rhs._vec[1];
    _vec[2] += rhs._vec[2];
    return *this;
  }

  Vec3& operator-=(const Vec3& rhs) {
    _vec[0] -= rhs._vec[0];
    _vec[1] -= rhs._vec[1];
    _vec[2] -= rhs._vec[2];
    return *this;
  }

  Vec3& operator-=(const T a) {
    _vec[0] -= a;
    _vec[1] -= a;
    _vec[2] -= a;
    return *this;
  }

  Vec3& operator+=(const T a) {
    _vec[0] += a;
    _vec[1] += a;
    _vec[2] += a;
    return *this;
  }

  Vec3& operator<<=(int val) {
    _vec[0] <<= val;
    _vec[1] <<= val;
    _vec[2] <<= val;
    return *this;
  }

  Vec3& operator>>=(int val) {
    _vec[0] >>= val;
    _vec[1] >>= val;
    _vec[2] >>= val;
    return *this;
  }

  Vec3& operator/=(const T a) {
    assert(a != 0);
    _vec[0] /= a;
    _vec[1] /= a;
    _vec[2] /= a;
    return *this;
  }

  Vec3& operator*=(const T a) {
    _vec[0] *= a;
    _vec[1] *= a;
    _vec[2] *= a;
    return *this;
  }

  Vec3& operator=(const T a) {
    _vec[0] = a;
    _vec[1] = a;
    _vec[2] = a;
    return *this;
  }

  Vec3& operator=(const T* const rhs) {
    _vec[0] = rhs[0];
    _vec[1] = rhs[1];
    _vec[2] = rhs[2];
    return *this;
  }

  Vec3 operator^(const Vec3& rhs) const {
    return Vec3<T>(_vec[1] * rhs._vec[2] - _vec[2] * rhs._vec[1],
                   _vec[2] * rhs._vec[0] - _vec[0] * rhs._vec[2],
                   _vec[0] * rhs._vec[1] - _vec[1] * rhs._vec[0]);
  }

  T operator*(const Vec3& rhs) const {
    return (_vec[0] * rhs._vec[0] + _vec[1] * rhs._vec[1]
            + _vec[2] * rhs._vec[2]);
  }

  Vec3 operator-() const { return Vec3<T>(-_vec[0], -_vec[1], -_vec[2]); }

  friend Vec3 operator+(const Vec3& lhs, const Vec3& rhs) {
    return Vec3<T>(lhs._vec[0] + rhs._vec[0],
                   lhs._vec[1] + rhs._vec[1],
                   lhs._vec[2] + rhs._vec[2]);
  }

  friend Vec3 operator+(const T lhs, const Vec3& rhs) {
    return Vec3<T>(lhs + rhs._vec[0], lhs + rhs._vec[1], lhs + rhs._vec[2]);
  }

  friend Vec3 operator+(const Vec3& lhs, const T rhs) {
    return Vec3<T>(lhs._vec[0] + rhs, lhs._vec[1] + rhs, lhs._vec[2] + rhs);
  }

  friend Vec3 operator-(const Vec3& lhs, const Vec3& rhs) {
    return Vec3<T>(lhs._vec[0] - rhs._vec[0],
                   lhs._vec[1] - rhs._vec[1],
                   lhs._vec[2] - rhs._vec[2]);
  }

  friend Vec3 operator-(const T lhs, const Vec3& rhs) {
    return Vec3<T>(lhs - rhs._vec[0], lhs - rhs._vec[1], lhs - rhs._vec[2]);
  }

  friend Vec3 operator-(const Vec3& lhs, const T rhs) {
    return Vec3<T>(lhs._vec[0] - rhs, lhs._vec[1] - rhs, lhs._vec[2] - rhs);
  }

  friend Vec3 operator*(const T lhs, const Vec3& rhs) {
    return Vec3<T>(lhs * rhs._vec[0], lhs * rhs._vec[1], lhs * rhs._vec[2]);
  }

  friend Vec3 operator*(const Vec3& lhs, const T rhs) {
    return Vec3<T>(lhs._vec[0] * rhs, lhs._vec[1] * rhs, lhs._vec[2] * rhs);
  }

  friend Vec3 operator/(const Vec3& lhs, const T rhs) {
    assert(rhs != 0);
    return Vec3<T>(lhs._vec[0] / rhs, lhs._vec[1] / rhs, lhs._vec[2] / rhs);
  }

  friend Vec3 operator<<(const Vec3& lhs, int val) {
    return Vec3<T>(lhs._vec[0] << val, lhs._vec[1] << val, lhs._vec[2] << val);
  }

  friend Vec3 operator>>(const Vec3& lhs, int val) {
    return Vec3<T>(lhs._vec[0] >> val, lhs._vec[1] >> val, lhs._vec[2] >> val);
  }

  bool operator<(const Vec3& rhs) const {
    if (_vec[0] == rhs._vec[0]) {
      if (_vec[1] == rhs._vec[1]) { return (_vec[2] < rhs._vec[2]); }
      return (_vec[1] < rhs._vec[1]);
    }
    return (_vec[0] < rhs._vec[0]);
  }

  bool operator>(const Vec3& rhs) const {
    if (_vec[0] == rhs._vec[0]) {
      if (_vec[1] == rhs._vec[1]) { return (_vec[2] > rhs._vec[2]); }
      return (_vec[1] > rhs._vec[1]);
    }
    return (_vec[0] > rhs._vec[0]);
  }

  bool operator==(const Vec3& rhs) const {
    return (_vec[0] == rhs._vec[0] && _vec[1] == rhs._vec[1]
            && _vec[2] == rhs._vec[2]);
  }

  bool operator!=(const Vec3& rhs) const {
    return (_vec[0] != rhs._vec[0] || _vec[1] != rhs._vec[1]
            || _vec[2] != rhs._vec[2]);
  }

  friend std::ostream& operator<<(std::ostream& os, const Vec3& vec) {
    os << vec[0] << ' ' << vec[1] << ' ' << vec[2];
    return os;
  }

  friend std::istream& operator>>(std::istream& is, Vec3& vec) {
    is >> vec[0] >> vec[1] >> vec[2];
    return is;
  }

  Vec3 min(const Vec3& vec) {
    Vec3 min;
    min.x() = std::min(vec.x(), x());
    min.y() = std::min(vec.y(), y());
    min.z() = std::min(vec.z(), z());
    return min;
  }

  Vec3 max(const Vec3& vec) {
    Vec3 max;
    max.x() = std::max(vec.x(), x());
    max.y() = std::max(vec.y(), y());
    max.z() = std::max(vec.z(), z());
    return max;
  }

  Vec3(const T a) { _vec[0] = _vec[1] = _vec[2] = a; }

  Vec3(const T x0, const T x1, const T x2) {
    _vec[0] = x0;
    _vec[1] = x1;
    _vec[2] = x2;
  }

  Vec3(const Vec3& vec) {
    _vec[0] = vec._vec[0];
    _vec[1] = vec._vec[1];
    _vec[2] = vec._vec[2];
  }

  template<typename S>
  Vec3(const Vec3<S>& vec) {
    _vec[0] = (T)vec.x();
    _vec[1] = (T)vec.y();
    _vec[2] = (T)vec.z();
  }

  inline Vec3<int32_t> round() const {
    return Vec3<int32_t>(int32_t(std::round(_vec[0])),
                         int32_t(std::round(_vec[1])),
                         int32_t(std::round(_vec[2])));
  }

  Vec3()  = default;
  ~Vec3() = default;

private:
  T _vec[3];
};

//============================================================================

template<typename T>
class Vec4 {
public:
  T*       data() { return _vec; }
  const T* data() const { return _vec; }

  T*       begin() { return &_vec[0]; }
  const T* begin() const { return &_vec[0]; }

  T*       end() { return &_vec[4]; }
  const T* end() const { return &_vec[4]; }

  T& operator[](int32_t i) {
    assert(i < 4);
    return _vec[i];
  }

  const T& operator[](int32_t i) const {
    assert(i < 4);
    return _vec[i];
  }

  int32_t size() const { return 4; }

  T& r() { return _vec[0]; }
  T& g() { return _vec[1]; }
  T& b() { return _vec[2]; }
  T& a() { return _vec[3]; }

  const T& r() const { return _vec[0]; }
  const T& g() const { return _vec[1]; }
  const T& b() const { return _vec[2]; }
  const T& a() const { return _vec[3]; }

  T& x() { return _vec[0]; }
  T& y() { return _vec[1]; }
  T& z() { return _vec[2]; }
  T& w() { return _vec[3]; }

  const T& x() const { return _vec[0]; }
  const T& y() const { return _vec[1]; }
  const T& z() const { return _vec[2]; }
  const T& w() const { return _vec[3]; }

  void normalize() {
    const T norm2 = _vec[0] * _vec[0] + _vec[1] * _vec[1] + _vec[2] * _vec[2]
                    + _vec[3] * _vec[3];
    if (norm2 != T(0)) {
      T invNorm = static_cast<T>(1) / std::sqrt(norm2);
      (*this) *= invNorm;
    }
  }

  T norm() const { return static_cast<T>(std::sqrt(norm2())); }
  T norm2() const { return (*this) * (*this); }

  Vec4& operator=(const Vec4& rhs) = default;

  Vec4& operator+=(const Vec4& rhs) {
    _vec[0] += rhs._vec[0];
    _vec[1] += rhs._vec[1];
    _vec[2] += rhs._vec[2];
    _vec[3] += rhs._vec[3];
    return *this;
  }

  Vec4& operator-=(const Vec4& rhs) {
    _vec[0] -= rhs._vec[0];
    _vec[1] -= rhs._vec[1];
    _vec[2] -= rhs._vec[2];
    _vec[3] -= rhs._vec[3];
    return *this;
  }

  Vec4& operator-=(const T a) {
    _vec[0] -= a;
    _vec[1] -= a;
    _vec[2] -= a;
    _vec[3] -= a;
    return *this;
  }

  Vec4& operator+=(const T a) {
    _vec[0] += a;
    _vec[1] += a;
    _vec[2] += a;
    _vec[3] += a;
    return *this;
  }

  Vec4& operator/=(const T a) {
    assert(a != 0);
    _vec[0] /= a;
    _vec[1] /= a;
    _vec[2] /= a;
    _vec[3] /= a;
    return *this;
  }

  Vec4& operator*=(const T a) {
    _vec[0] *= a;
    _vec[1] *= a;
    _vec[2] *= a;
    _vec[3] *= a;
    return *this;
  }
  Vec4& operator=(const T a) {
    _vec[0] = a;
    _vec[1] = a;
    _vec[2] = a;
    _vec[3] = a;
    return *this;
  }

  Vec4& operator=(const T* const rhs) {
    _vec[0] = rhs[0];
    _vec[1] = rhs[1];
    _vec[2] = rhs[2];
    _vec[3] = rhs[3];
    return *this;
  }

  T operator*(const Vec4& rhs) const {
    return _vec[0] * rhs._vec[0] + _vec[1] * rhs._vec[1]
           + _vec[2] * rhs._vec[2] + _vec[3] * rhs._vec[3];
  }

  Vec4 operator-() const {
    return Vec4<T>(-_vec[0], -_vec[1], -_vec[2], -_vec[3]);
  }

  friend Vec4 operator+(const Vec4& lhs, const Vec4& rhs) {
    return Vec4<T>(lhs._vec[0] + rhs._vec[0],
                   lhs._vec[1] + rhs._vec[1],
                   lhs._vec[2] + rhs._vec[2],
                   lhs._vec[3] + rhs._vec[3]);
  }

  friend Vec4 operator+(const T lhs, const Vec4& rhs) {
    return Vec4<T>(lhs + rhs._vec[0],
                   lhs + rhs._vec[1],
                   lhs + rhs._vec[2],
                   lhs + rhs._vec[3]);
  }

  friend Vec4 operator+(const Vec4& lhs, const T rhs) {
    return Vec4<T>(lhs._vec[0] + rhs,
                   lhs._vec[1] + rhs,
                   lhs._vec[2] + rhs,
                   lhs._vec[3] + rhs);
  }

  friend Vec4 operator-(const Vec4& lhs, const Vec4& rhs) {
    return Vec4<T>(lhs._vec[0] - rhs._vec[0],
                   lhs._vec[1] - rhs._vec[1],
                   lhs._vec[2] - rhs._vec[2],
                   lhs._vec[3] - rhs._vec[3]);
  }

  friend Vec4 operator-(const T lhs, const Vec4& rhs) {
    return Vec4<T>(lhs - rhs._vec[0],
                   lhs - rhs._vec[1],
                   lhs - rhs._vec[2],
                   lhs - rhs._vec[3]);
  }

  friend Vec4 operator-(const Vec4& lhs, const T rhs) {
    return Vec4<T>(lhs._vec[0] - rhs,
                   lhs._vec[1] - rhs,
                   lhs._vec[2] - rhs,
                   lhs._vec[3] - rhs);
  }

  friend Vec4 operator*(const T lhs, const Vec4& rhs) {
    return Vec4<T>(lhs * rhs._vec[0],
                   lhs * rhs._vec[1],
                   lhs * rhs._vec[2],
                   lhs * rhs._vec[3]);
  }

  friend Vec4 operator*(const Vec4& lhs, const T rhs) {
    return Vec4<T>(lhs._vec[0] * rhs,
                   lhs._vec[1] * rhs,
                   lhs._vec[2] * rhs,
                   lhs._vec[3] * rhs);
  }

  friend Vec4 operator/(const Vec4& lhs, const T rhs) {
    assert(rhs != 0);
    return Vec4<T>(lhs._vec[0] / rhs,
                   lhs._vec[1] / rhs,
                   lhs._vec[2] / rhs,
                   lhs._vec[3] / rhs);
  }

  bool operator<(const Vec4& rhs) const {
    if (_vec[0] == rhs._vec[0]) {
      if (_vec[1] == rhs._vec[1]) {
        if (_vec[2] == rhs._vec[2]) { return (_vec[3] < rhs._vec[3]); }
        return (_vec[2] < rhs._vec[2]);
      }
      return (_vec[1] < rhs._vec[1]);
    }
    return (_vec[0] < rhs._vec[0]);
  }

  bool operator>(const Vec4& rhs) const {
    if (_vec[0] == rhs._vec[0]) {
      if (_vec[1] == rhs._vec[1]) {
        if (_vec[2] == rhs._vec[2]) { return (_vec[3] > rhs._vec[3]); }
        return (_vec[2] > rhs._vec[2]);
      }
      return (_vec[1] > rhs._vec[1]);
    }
    return (_vec[0] > rhs._vec[0]);
  }

  bool operator==(const Vec4& rhs) const {
    return (_vec[0] == rhs._vec[0] && _vec[1] == rhs._vec[1]
            && _vec[2] == rhs._vec[2] && _vec[3] == rhs._vec[3]);
  }

  bool operator!=(const Vec4& rhs) const {
    return (_vec[0] != rhs._vec[0] || _vec[1] != rhs._vec[1]
            || _vec[2] != rhs._vec[2] || _vec[3] != rhs._vec[3]);
  }

  friend std::ostream& operator<<(std::ostream& os, const Vec4& vec) {
    os << vec[0] << ' ' << vec[1] << ' ' << vec[2] << ' ' << vec[3];
    return os;
  }

  friend std::istream& operator>>(std::istream& is, Vec4& vec) {
    is >> vec[0] >> vec[1] >> vec[2] >> vec[3];
    return is;
  }

  Vec4(const T a) { _vec[0] = _vec[1] = _vec[2] = _vec[3] = a; }

  Vec4(const T x, const T y, const T z, const T w) {
    _vec[0] = x;
    _vec[1] = y;
    _vec[2] = z;
    _vec[3] = w;
  }

  Vec4(const Vec4& vec) {
    _vec[0] = vec._vec[0];
    _vec[1] = vec._vec[1];
    _vec[2] = vec._vec[2];
    _vec[3] = vec._vec[3];
  }

  Vec4(const T* vec) : Vec4(vec[0], vec[1], vec[2], vec[3]) {}
  Vec4()  = default;
  ~Vec4() = default;

private:
  T _vec[4];
};

//============================================================================

template<typename T>
class VecN {
public:
  int32_t size() const { return int32_t(_vec.size()); }

  T*       data() { return _vec.data(); }
  const T* data() const { return _vec.data(); }

  T*       begin() { return _vec.begin(); }
  const T* begin() const { return _vec.begin(); }

  T*       end() { return _vec.end(); }
  const T* end() const { return _vec.end(); }

  T& operator[](const int32_t index) {
    assert(index < size());
    return _vec[index];
  }

  const T& operator[](const int32_t index) const {
    assert(index < size());
    return _vec[index];
  }

  void normalize() {
    T norm2 = T(0);
    for (int32_t i = 0, n = size(); i < n; ++i) { norm2 += _vec[i] * _vec[i]; }

    if (norm2 != T(0)) {
      T invNorm = T(1) / std::sqrt(norm2);
      (*this) *= invNorm;
    }
  }

  T norm() const { return static_cast<T>(std::sqrt(norm2())); }
  T norm2() const { return (*this) * (*this); }

  VecN& operator+=(const VecN& rhs) {
    assert(size() == rhs.size());
    for (int32_t i = 0, n = size(); i < n; ++i) { _vec[i] += rhs._vec[i]; }
    return *this;
  }

  VecN& operator-=(const VecN& rhs) {
    assert(size() == rhs.size());
    for (int32_t i = 0, n = size(); i < n; ++i) { _vec[i] -= rhs._vec[i]; }
    return *this;
  }

  VecN& operator-=(const T a) {
    for (int32_t i = 0, n = size(); i < n; ++i) { _vec[i] -= a; }
    return *this;
  }

  VecN& operator+=(const T a) {
    for (int32_t i = 0, n = size(); i < n; ++i) { _vec[i] += a; }
    return *this;
  }

  VecN& operator/=(const T a) {
    assert(a != 0);
    for (int32_t i = 0, n = size(); i < n; ++i) { _vec[i] /= a; }
    return *this;
  }

  VecN& operator*=(const T a) {
    for (int32_t i = 0, n = size(); i < n; ++i) { _vec[i] *= a; }
    return *this;
  }

  VecN& operator=(const T a) {
    for (int32_t i = 0, n = size(); i < n; ++i) { _vec[i] = a; }
    return *this;
  }

  T operator*(const VecN& rhs) const {
    assert(size() == rhs.size());
    T d = 0;
    for (int32_t i = 0, n = size(); i < n; ++i) { d += _vec[i] * rhs._vec[i]; }
    return d;
  }

  VecN operator-() const {
    VecN res;
    res.resize(size());
    for (int32_t i = 0, n = size(); i < n; ++i) { res._vec[i] = -_vec[i]; }
    return res;
  }

  friend VecN operator+(const VecN& lhs, const VecN& rhs) {
    VecN res(lhs);
    res += rhs;
    return res;
  }

  friend VecN operator+(const T lhs, const VecN& rhs) {
    VecN res(rhs);
    res += lhs;
    return res;
  }

  friend VecN operator+(const VecN& lhs, const T rhs) {
    VecN res(lhs);
    res += rhs;
    return res;
  }

  friend VecN operator-(const VecN& lhs, const VecN& rhs) {
    VecN res(lhs);
    res -= rhs;
    return res;
  }

  friend VecN operator-(const T lhs, const VecN& rhs) {
    VecN res;
    res.resize(rhs.size());
    for (int32_t i = 0, n = rhs.size(); i < n; ++i) {
      res._vec[i] = lhs - rhs._vec[i];
    }
    return res;
  }

  friend VecN operator-(const VecN& lhs, const T rhs) {
    VecN res(lhs);
    res -= rhs;
    return res;
  }

  friend VecN operator*(const T lhs, const VecN& rhs) {
    VecN res(rhs);
    res *= lhs;
    return res;
  }

  friend VecN operator*(const VecN& lhs, const T rhs) {
    VecN res(lhs);
    res *= rhs;
    return res;
  }

  friend VecN operator/(const VecN& lhs, const T rhs) {
    VecN res(lhs);
    res /= rhs;
    return res;
  }

  bool operator<(const VecN& rhs) const {
    assert(size() == rhs.size());
    for (int32_t i = 0, n = size(); i < n; ++i) {
      if (_vec[i] != rhs._vec[i]) { return _vec[i] < rhs._vec[i]; }
    }
    return false;
  }

  bool operator>(const VecN& rhs) const {
    assert(size() == rhs.size());
    for (int32_t i = 0, n = size(); i < n; ++i) {
      if (_vec[i] != rhs._vec[i]) { return _vec[i] > rhs._vec[i]; }
    }
    return false;
  }

  bool operator==(const VecN& rhs) const {
    assert(size() == rhs.size());
    for (int32_t i = 0, n = size(); i < n; ++i) {
      if (_vec[i] != rhs._vec[i]) { return false; }
    }
    return true;
  }

  bool operator!=(const VecN& rhs) const {
    assert(size() == rhs.size());
    for (int32_t i = 0, n = size(); i < n; ++i) {
      if (_vec[i] != rhs._vec[i]) { return true; }
    }
    return false;
  }

  void resize(const int32_t elementCount) { _vec.resize(elementCount); }
  void clear() { _vec.clear(); }

  VecN& operator=(const VecN& rhs) {
    _vec = rhs._vec;
    return *this;
  }

  friend std::ostream& operator<<(std::ostream& os, const VecN<T>& vec) {
    for (int32_t x = 0, count = vec.size(); x < count; ++x) {
      os << vec[x] << ' ';
    }
    return os;
  }

  friend std::istream& operator>>(std::istream& is, const VecN<T>& vec) {
    for (int32_t x = 0, count = vec.size(); x < count; ++x) { is >> vec[x]; }
    return is;
  }

  VecN(const int32_t size = 0) { _vec.resize(size); }
  VecN(const VecN& vector) { _vec = vector._vec; }
  VecN(const T* const rhs, const int32_t sz) {
    resize(sz);
    memcpy(_vec.data(), rhs, sz * sizeof(T));
  }
  ~VecN() = default;

private:
  std::vector<T> _vec;
};

//============================================================================

template<typename T>
struct HashVector3 {
  std::size_t operator()(Vec3<T> const& vec) const {
    auto seed = std::hash<T>()(vec[0]);
    seed ^= std::hash<T>()(vec[1]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<T>()(vec[2]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
  }
};

//============================================================================

template<class T>
Vec3<T>
Round(Vec3<T> vec) {
  vec[0] = std::round(vec[0]);
  vec[1] = std::round(vec[1]);
  vec[2] = std::round(vec[2]);
  return vec;
}

//----------------------------------------------------------------------------

template<class T>
Vec2<T>
Round(Vec2<T> vec) {
  vec[0] = std::round(vec[0]);
  vec[1] = std::round(vec[1]);
  return vec;
}

//============================================================================

template<class T>
T Clamp(const T& v, const T& lo, const T& hi);

template<class T>
Vec3<T>
Clamp(Vec3<T> vec, const T& lo, const T& hi) {
  vec[0] = Clamp(vec[0], lo, hi);
  vec[1] = Clamp(vec[1], lo, hi);
  vec[2] = Clamp(vec[2], lo, hi);
  return vec;
}

//============================================================================

}  // namespace vmesh
