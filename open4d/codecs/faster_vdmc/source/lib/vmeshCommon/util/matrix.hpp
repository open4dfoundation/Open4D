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

#include <cassert>
#include <cstdint>
#include <ostream>

#include "util/vector.hpp"

namespace vmesh {

//============================================================================

template<typename T>
class Mat4 {
public:
  T* operator[](const int32_t rowIndex) {
    assert(rowIndex < 4);
    return data[rowIndex];
  }

  const T* operator[](const int32_t rowIndex) const {
    assert(rowIndex < 4);
    return data[rowIndex];
  }

  int32_t columnCount() const { return 4; }
  int32_t rowCount() const { return 4; }

  Mat4& operator=(const Mat4& rhs) {
    memcpy(data, rhs.data, sizeof(data));
    return *this;
  }

  void operator+=(const Mat4& rhs) {
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) { this->data[i][j] += rhs.data[i][j]; }
    }
  }

  void operator-=(const Mat4& rhs) {
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) { this->data[i][j] -= rhs.data[i][j]; }
    }
  }

  void operator-=(const T a) {
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) { this->data[i][j] -= a; }
    }
  }

  void operator+=(const T a) {
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) { this->data[i][j] += a; }
    }
  }

  void operator/=(const T a) {
    assert(a != 0);
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) { this->data[i][j] /= a; }
    }
  }

  void operator*=(const T a) {
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) { this->data[i][j] *= a; }
    }
  }

  Vec3<T> operator*(const Vec3<T>& rhs) const {
    Vec3<T> res;
    for (int32_t i = 0; i < 3; ++i) {
      res[i] = 0;
      for (int32_t j = 0; j < 3; ++j) { res[i] += this->data[i][j] * rhs[j]; }
      res[i] += this->data[i][3];
    }

    T w = 0;
    for (int32_t j = 0; j < 3; ++j) { w += this->data[3][j] * rhs[j]; }
    w += this->data[3][3];
    assert(w != 0);
    for (int32_t i = 0; i < 3; ++i) { res[i] /= w; }
    return res;
  }

  Vec4<T> operator*(const Vec4<T>& rhs) const {
    Vec4<T> res;
    for (int32_t i = 0; i < 4; ++i) {
      res[i] = 0;
      for (int32_t j = 0; j < 4; ++j) { res[i] += this->data[i][j] * rhs[j]; }
    }
    return res;
  }

  Mat4 operator*(const Mat4& rhs) const {
    Mat4<T> res;
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) {
        res.data[i][j] = 0;
        for (int32_t k = 0; k < 4; ++k) {
          res.data[i][j] += this->data[i][k] * rhs.data[k][j];
        }
      }
    }
    return res;
  }

  Mat4 operator+(const Mat4& rhs) const {
    Mat4<T> res;
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) {
        res.data[i][j] = this->data[i][j] + rhs.data[i][j];
      }
    }
    return res;
  }

  Mat4 operator-(const Mat4& rhs) const {
    Mat4<T> res;
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) {
        res.data[i][j] = this->data[i][j] - rhs.data[i][j];
      }
    }
    return res;
  }

  Mat4 operator-() const {
    Mat4<T> res;
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) { res.data[i][j] = -this->data[i][j]; }
    }
    return res;
  }

  Mat4 operator*(T rhs) const {
    Mat4<T> res;
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) {
        res.data[i][j] = this->data[i][j] * rhs;
      }
    }
    return res;
  }

  Mat4 operator/(T rhs) const {
    assert(rhs != 0);
    Mat4<T> res;
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) {
        res.data[i][j] = this->data[i][j] / rhs;
      }
    }
    return res;
  }

  Mat4 transpose() const {
    Mat4<T> res;
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) { res.data[i][j] = this->data[j][i]; }
    }
    return res;
  }

  Mat4() = default;
  Mat4(const T a) {
    for (auto& i : data) {
      for (int32_t j = 0; j < 4; ++j) { i[j] = a; }
    }
  }

  Mat4(const Mat4& rhs) { memcpy(data, rhs.data, sizeof(data)); }
  ~Mat4() = default;

  friend Mat4 operator*(T lhs, const Mat4<T>& rhs) {
    Mat4<T> res;
    for (int32_t i = 0; i < 4; ++i) {
      for (int32_t j = 0; j < 4; ++j) {
        res.data[i][j] = lhs * rhs.data[i][j];
      }
    }
    return res;
  }

  static void makeIdentity(Mat4<T>& mat) {
    memset(mat.data, 0, sizeof(mat.data));
    for (int32_t i = 0; i < 4; ++i) { mat[i][i] = 1; }
  }

  static void makeTranslation(const T tx, const T ty, const T tz, Mat4& mat) {
    makeIdentity(mat);
    mat[0][3] = tx;
    mat[1][3] = ty;
    mat[2][3] = tz;
  }

  static void makeScale(const T sx, const T sy, const T sz, Mat4& mat) {
    makeIdentity(mat);
    mat[0][0] = sx;
    mat[1][1] = sy;
    mat[2][2] = sz;
  }

  static void makeUniformScale(const T s, Mat4& mat) {
    makeScale(s, s, s, mat);
  }

  static void
  makeRotation(const T angle, const T ax, const T ay, const T az, Mat4& mat) {
    T c       = cos(angle);
    T l_c     = T(1) - c;
    T s       = sin(angle);
    mat[0][0] = ax * ax + (1 - ax * ax) * c;
    mat[0][1] = ax * ay * l_c - az * s;
    mat[0][2] = ax * az * l_c + ay * s;
    mat[0][3] = T(0);
    mat[1][0] = ax * ay * l_c + az * s;
    mat[1][1] = ay * ay + (1 - ay * ay) * c;
    mat[1][2] = ay * az * l_c - ax * s;
    mat[1][3] = T(0);
    mat[2][0] = ax * az * l_c - ay * s;
    mat[2][1] = ay * az * l_c + ax * s;
    mat[2][2] = az * az + (1 - az * az) * c;
    mat[2][3] = T(0);
    mat[3][0] = T(0);
    mat[3][1] = T(0);
    mat[3][2] = T(0);
    mat[3][3] = T(1);
  }

  static void makeRotationX(const T angle, Mat4<T>& mat) {
    T c       = cos(angle);
    T s       = sin(angle);
    mat[0][0] = T(1);
    mat[0][1] = T(0);
    mat[0][2] = T(0);
    mat[0][3] = T(0);

    mat[1][0] = T(0);
    mat[1][1] = c;
    mat[1][2] = -s;
    mat[1][3] = T(0);

    mat[2][0] = T(0);
    mat[2][1] = s;
    mat[2][2] = c;
    mat[2][3] = T(0);

    mat[3][0] = T(0);
    mat[3][1] = T(0);
    mat[3][2] = T(0);
    mat[3][3] = T(1);
  }

  static void makeRotationY(const T angle, Mat4<T>& mat) {
    T c       = cos(angle);
    T s       = sin(angle);
    mat[0][0] = c;
    mat[0][1] = T(0);
    mat[0][2] = s;
    mat[0][3] = T(0);

    mat[1][0] = T(0);
    mat[1][1] = T(1);
    mat[1][2] = T(0);
    mat[1][3] = T(0);

    mat[2][0] = -s;
    mat[2][1] = T(0);
    mat[2][2] = c;
    mat[2][3] = T(0);

    mat[3][0] = T(0);
    mat[3][1] = T(0);
    mat[3][2] = T(0);
    mat[3][3] = T(1);
  }

  static void makeRotationZ(const T angle, Mat4<T>& mat) {
    T c       = cos(angle);
    T s       = sin(angle);
    mat[0][0] = c;
    mat[0][1] = -s;
    mat[0][2] = T(0);
    mat[0][3] = T(0);

    mat[1][0] = s;
    mat[1][1] = c;
    mat[1][2] = T(0);
    mat[1][3] = T(0);

    mat[2][0] = T(0);
    mat[2][1] = T(0);
    mat[2][2] = T(1);
    mat[2][3] = T(0);

    mat[3][0] = T(0);
    mat[3][1] = T(0);
    mat[3][2] = T(0);
    mat[3][3] = T(1);
  }

  static void makeRotationEulerAngles(const T  angleX,
                                      const T  angleY,
                                      const T  angleZ,
                                      Mat4<T>& mat) {
    Mat4<T> Rx;
    Mat4<T> Ry;
    Mat4<T> Rz;
    makeRotationX(angleX, Rx);
    makeRotationY(angleY, Ry);
    makeRotationZ(angleZ, Rz);
    mat = Rz * Ry * Rx;
  }

private:
  T data[4][4];
};

//============================================================================

//!    3x3 Matrix
template<typename T>
class Mat3 {
public:
  T operator()(int32_t rowIndex, int32_t columnIndex) const {
    return data[rowIndex][columnIndex];
  }

  T& operator()(int32_t rowIndex, int32_t columnIndex) {
    return data[rowIndex][columnIndex];
  }

  T* operator[](const int32_t rowIndex) {
    assert(rowIndex < 3);
    return data[rowIndex];
  }

  const T* operator[](const int32_t rowIndex) const {
    assert(rowIndex < 3);
    return data[rowIndex];
  }

  int32_t columnCount() const { return 3; }
  int32_t rowCount() const { return 3; }

  Mat3& operator=(const Mat3& rhs) {
    memcpy(data, rhs.data, sizeof(data));
    return *this;
  }

  void operator+=(const Mat3& rhs) {
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) { this->data[i][j] += rhs.data[i][j]; }
    }
  }

  void operator-=(const Mat3& rhs) {
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) { this->data[i][j] -= rhs.data[i][j]; }
    }
  }

  void operator-=(const T a) {
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) { this->data[i][j] -= a; }
    }
  }

  void operator+=(const T a) {
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) { this->data[i][j] += a; }
    }
  }

  void operator/=(const T a) {
    assert(a != 0);
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) { this->data[i][j] /= a; }
    }
  }

  void operator*=(const T a) {
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) { this->data[i][j] *= a; }
    }
  }

  Vec3<T> operator*(const Vec3<T>& rhs) const {
    Vec3<T> res;
    for (int32_t i = 0; i < 3; ++i) {
      res[i] = 0;
      for (int32_t j = 0; j < 3; ++j) { res[i] += this->data[i][j] * rhs[j]; }
    }
    return res;
  }

  Mat3 operator*(const Mat3& rhs) const {
    Mat3<T> res;
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) {
        res.data[i][j] = 0;
        for (int32_t k = 0; k < 3; ++k) {
          res.data[i][j] += this->data[i][k] * rhs.data[k][j];
        }
      }
    }
    return res;
  }

  Mat3 operator+(const Mat3& rhs) const {
    Mat3<T> res;
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) {
        res.data[i][j] = this->data[i][j] + rhs.data[i][j];
      }
    }
    return res;
  }

  Mat3 operator-(const Mat3& rhs) const {
    Mat3<T> res;
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) {
        res.data[i][j] = this->data[i][j] - rhs.data[i][j];
      }
    }
    return res;
  }

  Mat3 operator-() const {
    Mat3<T> res;
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) { res.data[i][j] = -this->data[i][j]; }
    }
    return res;
  }

  Mat3 operator*(T rhs) const {
    Mat3<T> res;
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) {
        res.data[i][j] = this->data[i][j] * rhs;
      }
    }
    return res;
  }

  Mat3 operator/(T rhs) const {
    assert(rhs != 0);
    Mat3<T> res;
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) {
        res.data[i][j] = this->data[i][j] / rhs;
      }
    }
    return res;
  }

  Mat3 transpose() const {
    Mat3<T> res;
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) { res.data[i][j] = this->data[j][i]; }
    }
    return res;
  }

  bool inverse(Mat3<T>& mat) const {
    // compute determinant
    const auto& m   = (*this);
    const T     det = m(0, 0) * (m(1, 1) * m(2, 2) - m(2, 1) * m(1, 2))
                  - m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0))
                  + m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
    // Threshold for determinant
    const T threshold = 1e-24;
    if (std::abs(det) < threshold) {
        return false; // determinant is too small, matrix is nearly singular
    }
    const T invDet = T(1) / det;

    // compute inverse
    mat(0, 0) = (m(1, 1) * m(2, 2) - m(2, 1) * m(1, 2)) * invDet;
    mat(0, 1) = (m(0, 2) * m(2, 1) - m(0, 1) * m(2, 2)) * invDet;
    mat(0, 2) = (m(0, 1) * m(1, 2) - m(0, 2) * m(1, 1)) * invDet;
    mat(1, 0) = (m(1, 2) * m(2, 0) - m(1, 0) * m(2, 2)) * invDet;
    mat(1, 1) = (m(0, 0) * m(2, 2) - m(0, 2) * m(2, 0)) * invDet;
    mat(1, 2) = (m(1, 0) * m(0, 2) - m(0, 0) * m(1, 2)) * invDet;
    mat(2, 0) = (m(1, 0) * m(2, 1) - m(2, 0) * m(1, 1)) * invDet;
    mat(2, 1) = (m(2, 0) * m(0, 1) - m(0, 0) * m(2, 1)) * invDet;
    mat(2, 2) = (m(0, 0) * m(1, 1) - m(1, 0) * m(0, 1)) * invDet;
    return true;
  }

  Mat3() = default;

  Mat3(const T a) {
    for (auto& i : data) {
      for (int32_t j = 0; j < 3; ++j) { i[j] = a; }
    }
  }

  Mat3(const Mat3& rhs) { memcpy(data, rhs.data, sizeof(data)); }

  ~Mat3() = default;

  friend inline Mat3<T> operator*(T lhs, const Mat3<T>& rhs) {
    Mat3<T> res;
    for (int32_t i = 0; i < 3; ++i) {
      for (int32_t j = 0; j < 3; ++j) {
        res.data[i][j] = lhs * rhs.data[i][j];
      }
    }
    return res;
  }

  static void makeIdentity(Mat3<T>& mat) {
    memset(mat.data, 0, sizeof(mat.data));
    for (int32_t i = 0; i < 3; ++i) { mat[i][i] = T(1); }
  }

  static void makeScale(const T sx, const T sy, const T sz, Mat3<T>& mat) {
    makeIdentity(mat);
    mat[0][0] = sx;
    mat[1][1] = sy;
    mat[2][2] = sz;
  }

  static void makeUniformScale(const T s, Mat3<T>& mat) {
    makeScale(s, s, s, mat);
  }

  static void makeRotation(const T  angle,
                           const T  ax,
                           const T  ay,
                           const T  az,
                           Mat3<T>& mat) {
    T c       = cos(angle);
    T l_c     = 1 - c;
    T s       = sin(angle);
    mat[0][0] = ax * ax + (1 - ax * ax) * c;
    mat[0][1] = ax * ay * l_c - az * s;
    mat[0][2] = ax * az * l_c + ay * s;
    mat[1][0] = ax * ay * l_c + az * s;
    mat[1][1] = ay * ay + (1 - ay * ay) * c;
    mat[1][2] = ay * az * l_c - ax * s;
    mat[2][0] = ax * az * l_c - ay * s;
    mat[2][1] = ay * az * l_c + ax * s;
    mat[2][2] = az * az + (1 - az * az) * c;
  }

  static void makeRotationX(const T angle, Mat3<T>& mat) {
    T c       = cos(angle);
    T s       = sin(angle);
    mat[0][0] = T(1);
    mat[0][1] = T(0);
    mat[0][2] = T(0);
    mat[1][0] = T(0);
    mat[1][1] = c;
    mat[1][2] = -s;
    mat[2][0] = T(0);
    mat[2][1] = s;
    mat[2][2] = c;
  }

  static void makeRotationY(const T angle, Mat3<T>& mat) {
    T c       = cos(angle);
    T s       = sin(angle);
    mat[0][0] = c;
    mat[0][1] = T(0);
    mat[0][2] = s;
    mat[1][0] = T(0);
    mat[1][1] = T(1);
    mat[1][2] = T(0);
    mat[2][0] = -s;
    mat[2][1] = T(0);
    mat[2][2] = c;
  }

  static void makeRotationZ(const T angle, Mat3<T>& mat) {
    T c       = cos(angle);
    T s       = sin(angle);
    mat[0][0] = c;
    mat[0][1] = -s;
    mat[0][2] = T(0);
    mat[1][0] = s;
    mat[1][1] = c;
    mat[1][2] = T(0);
    mat[2][0] = T(0);
    mat[2][1] = T(0);
    mat[2][2] = T(1);
  }

  static void makeRotationEulerAngles(const T  angleX,
                                      const T  angleY,
                                      const T  angleZ,
                                      Mat3<T>& mat) {
    Mat3<T> Rx;
    Mat3<T> Ry;
    Mat3<T> Rz;
    makeRotationX(angleX, Rx);
    makeRotationY(angleY, Ry);
    makeRotationZ(angleZ, Rz);
    mat = Rz * Ry * Rx;
  }

  friend std::ostream& operator<<(std::ostream& os, const Mat3<T>& mat) {
    os << mat[0][0] << ' ' << mat[0][1] << ' ' << mat[0][2] << '\n'
       << mat[1][0] << ' ' << mat[1][1] << ' ' << mat[1][2] << '\n'
       << mat[2][0] << ' ' << mat[2][1] << ' ' << mat[2][2] << '\n';
    return os;
  }

  friend std::istream& operator>>(std::istream& is, Mat3<T>& mat) {
    is >> mat[0][0] >> mat[0][1] >> mat[0][2] >> mat[1][0] >> mat[1][1]
      >> mat[1][2] >> mat[2][0] >> mat[2][1] >> mat[2][2];
    return is;
  }

private:
  T data[3][3];
};

//============================================================================

template<typename T>
class Matrix {
public:
  int32_t columnCount() const { return _columnCount; }
  int32_t rowCount() const { return _rowCount; }

  T*       buffer() { return _data.data(); }
  const T* buffer() const { return _data.data(); }

  T* row(const int32_t rowIndex) {
    assert(rowIndex < _rowCount);
    return _data.data() + (rowIndex * _columnCount);
  }

  const T* row(const int32_t rowIndex) const {
    assert(rowIndex < _rowCount);
    return _data.data() + (rowIndex * _columnCount);
  }

  T*       operator[](const int32_t rowIndex) { return row(rowIndex); }
  const T* operator[](const int32_t rowIndex) const { return row(rowIndex); }

  const T& operator()(const int32_t rowIndex,
                      const int32_t columnIndex) const {
    return row(rowIndex)[columnIndex];
  }

  T& operator()(const int32_t rowIndex, const int32_t columnIndex) {
    return row(rowIndex)[columnIndex];
  }

  void resize(const int32_t rowCount, const int32_t columnCount) {
    assert(columnCount > 0 && rowCount > 0);
    _columnCount = columnCount;
    _rowCount    = rowCount;
    _data.resize(rowCount * columnCount);
  }

  void clear() {
    _columnCount = _rowCount = 0;
    _data.clear();
  }

  Matrix& operator=(const Matrix& rhs) = default;

  Matrix& operator=(const T a) {
    std::fill(_data.begin(), _data.end(), a);
    return *this;
  }

  void abs() {
    for (int32_t y = 0; y < _rowCount; ++y) {
      T* row = row(y);
      for (int32_t x = 0; x < _columnCount; ++x) { row[x] = std::abs(row[x]); }
    }
  }

  Matrix& operator+=(const Matrix& rhs) {
    assert(_columnCount == rhs._columnCount && _rowCount == rhs._rowCount);
    for (int32_t y = 0; y < _rowCount; ++y) {
      T*       row    = row(y);
      const T* rowRhs = rhs.row(y);
      for (int32_t x = 0; x < _columnCount; ++x) { row[x] += rowRhs[x]; }
    }
    return *this;
  }

  Matrix& operator-=(const Matrix& rhs) {
    assert(_columnCount == rhs._columnCount && _rowCount == rhs._rowCount);
    for (int32_t y = 0; y < _rowCount; ++y) {
      T*       r      = row(y);
      const T* rowRhs = rhs.row(y);
      for (int32_t x = 0; x < _columnCount; ++x) { r[x] -= rowRhs[x]; }
    }
    return *this;
  }

  Matrix& operator-=(T a) {
    for (int32_t y = 0; y < _rowCount; ++y) {
      T* r = row(y);
      for (int32_t x = 0; x < _columnCount; ++x) { r[x] -= a; }
    }
    return *this;
  }

  Matrix& operator+=(T a) {
    for (int32_t y = 0; y < _rowCount; ++y) {
      T* r = row(y);
      for (int32_t x = 0; x < _columnCount; ++x) { r[x] += a; }
    }
    return *this;
  }

  Matrix& operator/=(T a) {
    assert(a != 0);
    for (int32_t y = 0; y < _rowCount; ++y) {
      T* r = row(y);
      for (int32_t x = 0; x < _columnCount; ++x) { r[x] /= a; }
    }
    return *this;
  }

  Matrix& operator*=(T a) {
    for (int32_t y = 0; y < _rowCount; ++y) {
      T* r = row(y);
      for (int32_t x = 0; x < _columnCount; ++x) { r[x] *= a; }
    }
    return *this;
  }

  Matrix operator-() const {
    Matrix res;
    res.resize(_columnCount, _rowCount);
    for (int32_t y = 0; y < _rowCount; ++y) {
      T*       rowRes = res.row(y);
      const T* r      = row(y);
      for (int32_t x = 0; x < _columnCount; ++x) { rowRes[x] = -r[x]; }
    }
    return res;
  }

  T norm() const { return static_cast<T>(std::sqrt(norm2())); }
  T norm2() const {
    T sum = T(0);
    for (int32_t y = 0; y < rowCount(); ++y) {
      const T* r = row(y);
      for (int32_t x = 0; x < columnCount(); ++x) { sum = r[x] * r[x]; }
    }
    return sum;
  }

  T normInf() const {
    T maxValue = T(0);
    for (int32_t y = 0; y < rowCount(); ++y) {
      const T* r = row(y);
      for (int32_t x = 0; x < columnCount(); ++x) {
        maxValue = std::max(std::fabs(r[x]), maxValue);
      }
    }
    return maxValue;
  }

  friend std::ostream& operator<<(std::ostream& os, const Matrix<T>& mat) {
    for (int32_t y = 0; y < mat.rowCount(); ++y) {
      const T* r = mat.row(y);
      for (int32_t x = 0; x < mat.columnCount(); ++x) { os << r[x] << ' '; }
      os << ";\n";
    }
    return os;
  }

  friend std::istream& operator>>(std::istream& is, const Matrix<T>& mat) {
    for (int32_t y = 0; y < mat.rowCount(); ++y) {
      const T* r = mat.row(y);
      for (int32_t x = 0; x < mat.columnCount(); ++x) { is >> r[x]; }
    }
    return is;
  }

  friend Matrix operator+(const Matrix& lhs, const Matrix& rhs) {
    Matrix res(lhs);
    res += rhs;
    return res;
  }

  friend Matrix operator+(const T lhs, const Matrix& rhs) {
    Matrix res(rhs);
    res += lhs;
    return res;
  }

  friend Matrix operator+(const Matrix& lhs, const T rhs) {
    Matrix res(lhs);
    res += rhs;
    return res;
  }

  friend Matrix operator-(const Matrix& lhs, const Matrix& rhs) {
    Matrix res(lhs);
    res -= rhs;
    return res;
  }

  friend Matrix operator-(const T lhs, const Matrix& rhs) {
    Matrix res(-rhs);
    res += lhs;
    return res;
  }

  friend Matrix operator-(const Matrix& lhs, const T rhs) {
    Matrix res(lhs);
    res -= rhs;
    return res;
  }

  friend Matrix operator*(const T lhs, const Matrix& rhs) {
    Matrix res(rhs);
    res *= lhs;
    return res;
  }

  friend Matrix operator*(const Matrix& lhs, const T rhs) {
    Matrix res(lhs);
    res *= rhs;
    return res;
  }

  friend Matrix operator/(const Matrix& lhs, const T rhs) {
    Matrix res(lhs);
    res /= rhs;
    return res;
  }

  friend Matrix operator*(const Matrix& lhs, const Matrix& rhs) {
    assert(lhs._columnCount == rhs._rowCount);
    Matrix res(lhs._rowCount, rhs._columnCount);

    for (int32_t y = 0; y < res._rowCount; ++y) {
      const T* rowLhs = lhs.row(y);
      T*       rowRes = res.row(y);

      for (int32_t x = 0; x < res._columnCount; ++x) {
        T sum = T(0);
        for (int32_t z = 0; z < lhs._columnCount; ++z) {
          sum += rowLhs[z] * rhs(z, x);
        }
        rowRes[x] = sum;
      }
    }
    return res;
  }

  friend VecN<T> operator*(const Matrix<T>& lhs, const VecN<T>& rhs) {
    assert(lhs.columnCount() == rhs.size());
    VecN<T> res(lhs._rowCount);
    for (int32_t y = 0; y < lhs.rowCount(); ++y) {
      const T* rowLhs = lhs.row(y);
      T        sum    = T(0);
      for (int32_t x = 0; x < lhs.columnCount(); ++x) {
        sum += rowLhs[x] * rhs[x];
      }
      res[y] = sum;
    }
    return res;
  }

  static void makeIdentity(Matrix<T>& mat) {
    for (int32_t y = 0; y < mat.rowCount(); ++y) {
      T* row = mat.row(y);
      for (int32_t x = 0; x < mat.columnCount(); ++x) { row[x] = T(x == y); }
    }
  }

  Matrix transpose() const {
    Matrix res(_columnCount, _rowCount);
    for (int32_t y = 0; y < res._rowCount; ++y) {
      T* rowRes = res.row(y);
      for (int32_t x = 0; x < res._columnCount; ++x) {
        rowRes[x] = (*this)(x, y);
      }
    }
    return res;
  }

  Matrix(const int32_t rowCount = 1, const int32_t columnCount = 1) {
    resize(rowCount, columnCount);
  }

  Matrix(const Matrix& matrix) { *this = matrix; }

  ~Matrix() = default;

private:
  std::vector<T> _data;
  int32_t        _columnCount{};
  int32_t        _rowCount{};
};

//============================================================================

}  // namespace vmesh
