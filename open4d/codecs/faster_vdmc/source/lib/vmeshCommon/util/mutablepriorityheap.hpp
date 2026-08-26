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
#include <vector>

namespace vmesh {

//============================================================================

// T should define
//  heapPosition() returns the index in the heap or -1 if not in heap
//  heapKey() returns the key
//  setHeapPosition() sets the element position
//  setHeapKey() sets the key
template<typename T>
class MutablePriorityHeap {
public:
  MutablePriorityHeap()                                      = default;
  MutablePriorityHeap(const MutablePriorityHeap&)            = default;
  MutablePriorityHeap& operator=(const MutablePriorityHeap&) = default;
  ~MutablePriorityHeap()                                     = default;
  int32_t  size() const { return int32_t(_elements.size()); }
  const T* element(const int32_t index) {
    assert(index < size());
    return _elements[index];
  }
  bool empty() const { return _elements.empty(); }
  void reserve(const int32_t sz) { _elements.reserve(sz); }
  void clear() { _elements.resize(0); }
  T*   top() { return _elements.empty() ? nullptr : _elements[0]; }
  T*   remove(T& e) {
    if (e.heapPosition() < 0) { return nullptr; }
    const auto i = e.heapPosition();
    assert(i >= 0 && i < size());
    this->swap(i, size() - 1);
    _elements.pop_back();
    e.setHeapPosition(-1);  // not in heap
    if (i == size()) upheap(i);
    else if (_elements[i]->heapKey() < e.heapKey()) {
      downheap(i);
    } else {
      upheap(i);
    }
    return &e;
  }
  T* extract() {
    if (size() == 0) { return nullptr; }
    this->swap(0, size() - 1);
    T* dead = _elements.back();  //[size() - 1];
    dead->setHeapPosition(-1);
    _elements.pop_back();
    downheap(0);
    return dead;
  }
  void insert(T& e) {
    const auto i = size();
    e.setHeapPosition(i);
    _elements.push_back(&e);
    upheap(i);
  }
  void update(T& e) {
    const auto i = e.heapPosition();
    if (i < 0) { return; }
    assert(i >= 0 && i < size());
    if (i > 0 && e.heapKey() > _elements[parent(i)]->heapKey()) {
      upheap(i);
    } else {
      downheap(i);
    }
  }

private:
  void place(T& e, const int32_t position) {
    _elements[position] = &e;
    e.setHeapPosition(position);
  }

  void swap(const int32_t position1, const int32_t position2) {
    std::swap(_elements[position1], _elements[position2]);
    _elements[position1]->setHeapPosition(position1);
    _elements[position2]->setHeapPosition(position2);
  }

  int32_t parent(const int32_t i) const {
    assert(i >= 0 && i < size());
    return (i - 1) / 2;
  }

  int32_t left(const int32_t i) const {
    assert(i >= 0 && i < size());
    return 2 * i + 1;
  }

  int32_t right(const int32_t i) const {
    assert(i >= 0 && i < size());
    return 2 * i + 2;
  }

  void upheap(const int32_t i) {
    if (!size() || i == size()) { return; }

    assert(i >= 0 && i < size());
    T*   moving = _elements[i];
    auto index  = i;
    auto p      = parent(i);

    while (index > 0 && moving->heapKey() > _elements[p]->heapKey()) {
      place(*_elements[p], index);
      index = p;
      p     = parent(p);
    }

    if (index != i) { place(*moving, index); }
  }

  void downheap(const int32_t i) {
    if (!size()) { return; }

    assert(i >= 0 && i < size());
    T*      moving  = _elements[i];
    auto    index   = i;
    auto    l       = left(i);
    auto    r       = right(i);
    int32_t largest = 0;

    const auto elementCount = size();
    while (l < elementCount) {
      if (r < elementCount
          && _elements[l]->heapKey() < _elements[r]->heapKey()) {
        largest = r;
      } else {
        largest = l;
      }

      if (moving->heapKey() < _elements[largest]->heapKey()) {
        place(*_elements[largest], index);
        index = largest;
        l     = left(index);
        r     = right(index);
      } else {
        break;
      }
    }

    if (index != i) { place(*moving, index); }
  }

  std::vector<T*> _elements;
};

//============================================================================

}  // namespace vmesh
