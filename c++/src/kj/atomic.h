// Copyright (c) 2016 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef KJ_ATOMIC_H_
#define KJ_ATOMIC_H_

#if !defined(__GNUC__)
// Fall back to std::atomic
#include <atomic>
#endif

namespace kj {

enum class MemoryOrder {
#if defined(__GNUC__)
  RELAXED = __ATOMIC_RELAXED,
  CONSUME = __ATOMIC_CONSUME,
  ACQUIRE = __ATOMIC_ACQUIRE,
  RELEASE = __ATOMIC_RELEASE,
  ACQ_REL = __ATOMIC_ACQ_REL,
  SEQ_CST = __ATOMIC_SEQ_CST
#else
  RELAXED = std::memory_order_relaxed,
  CONSUME = std::memory_order_consume,
  ACQUIRE = std::memory_order_acquire,
  RELEASE = std::memory_order_release,
  ACQ_REL = std::memory_order_acq_rel,
  SEQ_CST = std::memory_order_seq_cst
#endif  // defined(__GNUC__), else
};

template <class T>
class Atomic {
  // Like std::atomic<T>, but supports copy operations, and all of its load/store operations have
  // relaxed memory ordering by default rather than sequentially consistent, as relaxed loads are
  // the most common use case in the Cap'n Proto codebase.

#if defined(__GNUC__)
  T value;
#else
  std::atomic<T> value;
#endif

public:
  Atomic() = default;
  constexpr Atomic(T desired): value(desired) {}

  Atomic(const Atomic& other)
      : Atomic(other.load(kj::MemoryOrder::ACQUIRE)) {}

  Atomic& operator=(const Atomic& other) {
    this->store(other.load(kj::MemoryOrder::ACQUIRE), kj::MemoryOrder::RELEASE);
    return *this;
  }

  T load(MemoryOrder order = MemoryOrder::RELAXED) const;
  T load(MemoryOrder order = MemoryOrder::RELAXED) const volatile;
  void store(T desired, MemoryOrder order = MemoryOrder::RELAXED);
  void store(T desired, MemoryOrder order = MemoryOrder::RELAXED) volatile;
};

#if defined(__GNUC__)

template <class T>
inline T Atomic<T>::load(MemoryOrder order) const {
  return __atomic_load_n(&value, static_cast<int>(order));
}

template <class T>
inline T Atomic<T>::load(MemoryOrder order) const volatile {
  return __atomic_load_n(&value, static_cast<int>(order));
}

template <class T>
inline void Atomic<T>::store(T desired, MemoryOrder order) {
  __atomic_store_n(&value, desired, static_cast<int>(order));
}

template <class T>
inline void Atomic<T>::store(T desired, MemoryOrder order) volatile {
  __atomic_store_n(&value, desired, static_cast<int>(order));
}

#else  // defined(__GNUC__)

template <class T>
inline T Atomic<T>::load(MemoryOrder order) const {
  return value.load(static_cast<std::memory_order>(order));
}

template <class T>
inline T Atomic<T>::load(MemoryOrder order) const volatile {
  return value.load(static_cast<std::memory_order>(order));
}

template <class T>
inline void Atomic<T>::store(T desired, MemoryOrder order) {
  value.store(desired, static_cast<std::memory_order>(order));
}

template <class T>
inline void Atomic<T>::store(T desired, MemoryOrder order) volatile {
  value.store(desired, static_cast<std::memory_order>(order));
}

#endif  // defined(__GNUC__), else

}  // namespace kj

#endif  // KJ_ATOMIC_H_
