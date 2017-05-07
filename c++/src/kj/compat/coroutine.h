// Copyright (c) 2017 Sandstorm Development Group, Inc. and contributors
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

#ifndef KJ_COMPAT_COROUTINE_H_
#define KJ_COMPAT_COROUTINE_H_

#if defined(__GNUC__) && !KJ_HEADER_WARNINGS
#pragma GCC system_header
#endif

#include <kj/async.h>

#ifdef __cpp_coroutines
// Clang uses the official symbol and header file.

#define KJ_HAVE_COROUTINE 1
#include <experimental/coroutine>

#elif defined(_RESUMABLE_FUNCTIONS_SUPPORTED)
// MSVC as of VS2017 still uses their old symbol and header.

#define KJ_HAVE_COROUTINE 1
#include <experimental/resumable>

#endif

#ifdef KJ_HAVE_COROUTINE

namespace kj {
namespace _ {

template <typename T>
class CoroutinePromise;

class CoroutineAdapter: public Event {
public:
  std::experimental::coroutine_handle<> coroutine;

  template <typename T>
  CoroutineAdapter(PromiseFulfiller<T>& fulfiller,
      std::experimental::coroutine_handle<CoroutinePromise<T>> c)
      : coroutine(c)
  {
    c.promise().event = this;
    c.promise().fulfiller = &fulfiller;
  }

  Maybe<Own<Event>> CoroutineAdapter::fire() override {
    coroutine();
    return nullptr;
  }
};

template <typename T>
class CoroutinePromise {
public:
  Promise<T> get_return_object() {
    return newAdaptedPromise<T, CoroutineAdapter>(
        std::experimental::coroutine_handle<CoroutinePromise>::from_promise(*this));
  }

  auto initial_suspend() { return std::experimental::suspend_never{}; }
  auto final_suspend() { return std::experimental::suspend_never{}; }

  template <typename U>
  void return_value(U&& value) {
    // TODO(soon): EnableIf on convertibility to T.
    fulfiller->fulfill(decayCp(value));
  }

  void return_value(T&& value) {
    // We need an explicit T overload in case we are passed a braced init list.
    fulfiller->fulfill(mv(value));
  }

  void set_exception(std::exception_ptr e) {
    // MSVC doesn't yet implement the unhandled_exception() change in the Coroutines TS.
    fulfiller->rejectIfThrows([e = mv(e)] { std::rethrow_exception(mv(e)); });
  }

  void unhandled_exception() {
    // Coroutines TS changed set_exception to unhandled_exception, and made it required.
    set_exception(std::current_exception());
  }

  Event* event;
  PromiseFulfiller<T>* fulfiller;
};

template <>
class CoroutinePromise<void> {
public:
  Promise<void> get_return_object() {
    return newAdaptedPromise<void, CoroutineAdapter>(
        std::experimental::coroutine_handle<CoroutinePromise>::from_promise(*this));
  }

  auto initial_suspend() { return std::experimental::suspend_never{}; }
  auto final_suspend() { return std::experimental::suspend_never{}; }

  void return_void() {
    fulfiller->fulfill();
  }

  void set_exception(std::exception_ptr e) {
    // MSVC doesn't yet implement the unhandled_exception() change in the Coroutines TS.
    fulfiller->rejectIfThrows([e = mv(e)] { std::rethrow_exception(mv(e)); });
  }

  void unhandled_exception() {
    // Coroutines TS changed set_exception to unhandled_exception, and made it required.
    set_exception(std::current_exception());
  }

  Event* event;
  PromiseFulfiller<void>* fulfiller;
};

template <class T>
class PromiseAwaiter {
public:
  Own<PromiseNode> node;

  bool await_ready() const { return false; }

  T await_resume();

  template <class CoroutineHandle>
  void await_suspend(CoroutineHandle c) {
    node->onReady(*c.promise().event);
  }
};

template <typename T>
inline T PromiseAwaiter<T>::await_resume() {
  ExceptionOr<FixVoid<T>> result;
  node->get(result);
  KJ_IF_MAYBE(exception, mv(result.exception)) {
    throwFatalException(mv(*exception));
  }
  KJ_IF_MAYBE(value, mv(result.value)) {
    return mv(*value);
  }
}

}  // namespace _ (private)

template <class T>
T operator co_await(Promise<T>& promise) {
  return _::PromiseAwaiter<T>{mv(promise.node)};
}

template <class T>
T operator co_await(Promise<T>&& promise) {
  return _::PromiseAwaiter<T>{mv(promise.node)};
}

}  // namespace kj

namespace std {
namespace experimental {

template <class T, class... Args>
struct coroutine_traits<kj::Promise<T>, Args...> {
  using promise_type = kj::_::CoroutinePromise<T>;
};

}  // namespace experimental
}  // namespace std

#endif  // KJ_HAVE_COROUTINE

#endif  // KJ_COMPAT_COROUTINE_H_