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

#include <cstdint>
#include <vector>

#include "util/vector.hpp"

namespace vmesh {

//============================================================================

template<class T>
class SparseMatrix {
public:
  int32_t                     columnCount() const { return _columnCount; }
  int32_t                     rowCount() const { return _rowCount; }
  std::vector<T>&             buffer() { return _data; }
  const std::vector<T>&       buffer() const { return _data; }
  std::vector<int32_t>&       pointers() { return _pointers; }
  const std::vector<int32_t>& pointers() const { return _pointers; }
  std::vector<int32_t>&       indexes() { return _indexes; }
  const std::vector<int32_t>& indexes() const { return _indexes; }

  void addElementInOrder(const int32_t rowIndex,
                         const int32_t columnIndex,
                         const T       value) {
    assert(rowIndex < _rowCount);
    assert(columnIndex < _columnCount);
    ++_pointers[rowIndex + 1];
    _indexes.push_back(columnIndex);
    _data.push_back(value);
  }

  void updatePointers() {
    for (int i = 1; i <= _rowCount; ++i) { _pointers[i] += _pointers[i - 1]; }
  }

  int32_t rowStart(const int32_t rowIndex) const {
    assert(rowIndex < _rowCount);
    return _pointers[rowIndex];
  }

  int32_t rowEnd(const int32_t rowIndex) const {
    assert(rowIndex < _rowCount);
    return _pointers[rowIndex + 1];
  }

  SparseMatrix transpose() const {
    SparseMatrix res(_columnCount, _rowCount);
    auto&        rpointers = res.pointers();
    for (const auto j : _indexes) { ++rpointers[j + 1]; }
    res.updatePointers();
    const auto nonZeroCount = int32_t(rpointers.back());
    auto&      rindexes     = res.indexes();
    auto&      rvalues      = res.buffer();
    rindexes.resize(nonZeroCount);
    rvalues.resize(nonZeroCount);
    for (int32_t y = 0; y < _rowCount; ++y) {
      for (int32_t k = rowStart(y), end = rowEnd(y); k < end; ++k) {
        const auto p = rpointers[_indexes[k]]++;
        rindexes[p]  = y;
        rvalues[p]   = _data[k];
      }
    }
    for (int32_t x = _columnCount - 1; x >= 0; --x) {
      rpointers[x + 1] = rpointers[x];
    }
    rpointers[0] = 0;
    return res;
  }

  friend VecN<T> operator*(const SparseMatrix<T>& lhs, const VecN<T>& rhs) {
    assert(lhs.columnCount() == rhs.size());
    VecN<T> res(lhs._rowCount);
    for (int32_t y = 0; y < lhs._rowCount; ++y) {
      T sum = T(0);
      for (int32_t k = lhs.rowStart(y), end = lhs.rowEnd(y); k < end; ++k) {
        sum += lhs._data[k] * rhs[lhs._indexes[k]];
      }
      res[y] = sum;
    }
    return res;
  }

  friend VecN<T> operator*(const SparseMatrix<T>& lhs, const T* rhs) {
    assert(rhs);
    VecN<T> res(lhs._rowCount);
    for (int32_t y = 0; y < lhs._rowCount; ++y) {
      T sum = T(0);
      for (int32_t k = lhs.rowStart(y), end = lhs.rowEnd(y); k < end; ++k) {
        sum += lhs._data[k] * rhs[lhs._indexes[k]];
      }
      res[y] = sum;
    }
    return res;
  }

  friend SparseMatrix<T> operator*(const SparseMatrix<T>& lhs,
                                   const SparseMatrix<T>& rhs) {
    assert(lhs.columnCount() == rhs.rowCount());
    const auto      trhs = rhs.transpose();
    SparseMatrix<T> res(lhs._rowCount, rhs._columnCount);
    VecN<T>         tmp(rhs._rowCount);
    tmp = T(0);
    for (int32_t y = 0; y < res._rowCount; ++y) {
      for (int32_t x = 0; x < res._columnCount; ++x) {
        for (int32_t k = trhs.rowStart(x), end = trhs.rowEnd(x); k < end;
             ++k) {
          tmp[trhs._indexes[k]] = trhs._data[k];
        }

        T sum = T(0);
        for (int32_t k = lhs.rowStart(y), end = lhs.rowEnd(y); k < end; ++k) {
          sum += lhs._data[k] * tmp[lhs._indexes[k]];
        }

        if (sum != T(0)) { res.addElementInOrder(y, x, sum); }

        for (int32_t k = trhs.rowStart(x), end = trhs.rowEnd(x); k < end;
             ++k) {
          tmp[trhs._indexes[k]] = T(0);
        }
      }
    }

    res.updatePointers();
    return res;
  }

  void initialize(const int32_t rowCount, const int32_t columnCount) {
    _columnCount = columnCount;
    _rowCount    = rowCount;
    _data.resize(0);
    _pointers.resize(0);
    _indexes.resize(0);
    _pointers.resize(_rowCount + 1, 0);
  }

  SparseMatrix() {
    _columnCount = 0;
    _rowCount    = 0;
  }

  SparseMatrix(const int32_t rowCount, const int32_t columnCount) {
    _columnCount = columnCount;
    _rowCount    = rowCount;
    _pointers.resize(_rowCount + 1, 0);
  }

  SparseMatrix(const SparseMatrix& matrix) { *this = matrix; }
  ~SparseMatrix() = default;

private:
  std::vector<T>       _data;
  std::vector<int32_t> _pointers;
  std::vector<int32_t> _indexes;
  int32_t              _columnCount{};
  int32_t              _rowCount{};
};

//============================================================================

}  // namespace vmesh