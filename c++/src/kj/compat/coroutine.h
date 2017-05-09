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
struct CoroutineFulfiller;

struct CoroutineAdapter: public Event {
  std::experimental::coroutine_handle<> coroutine;

  template <typename T>
  CoroutineAdapter(PromiseFulfiller<T>& fulfiller,
      std::experimental::coroutine_handle<CoroutineFulfiller<T>> c)
      : coroutine(c)
  {
    c.promise().adapter = this;
    c.promise().fulfiller = &fulfiller;
  }

  ~CoroutineAdapter() noexcept(false) { if (coroutine) { coroutine.destroy(); } }

  Maybe<Own<Event>> fire() override {
    coroutine.resume();
    return nullptr;
  }
};

template <typename T>
struct CoroutineFulfillerBase {
  Promise<T> get_return_object() {
    return newAdaptedPromise<T, CoroutineAdapter>(
        std::experimental::coroutine_handle<CoroutineFulfiller<T>>::from_promise(
            static_cast<CoroutineFulfiller<T>&>(*this)));
  }

  auto initial_suspend() { return std::experimental::suspend_never{}; }
  auto final_suspend() { return std::experimental::suspend_never{}; }

  void unhandled_exception() {
    fulfiller->rejectIfThrows([] { std::rethrow_exception(std::current_exception()); });
  }

  void set_exception(std::exception_ptr e) {
    // TODO(msvc): MSVC as of VS2017 implements an older wording of the Coroutines TS, and uses
    //   this set_exception() instead of unhandled_exception(). Remove this when we can.
    fulfiller->rejectIfThrows([e = mv(e)] { std::rethrow_exception(mv(e)); });
  }

  CoroutineAdapter* adapter;
  PromiseFulfiller<T>* fulfiller;

  ~CoroutineFulfillerBase() { adapter->coroutine = nullptr; }
};

template <typename T>
struct CoroutineFulfiller: CoroutineFulfillerBase<T> {
  void return_value(T&& value) { this->fulfiller->fulfill(mv(value)); }
  template <typename U>
  void return_value(U&& value) { this->fulfiller->fulfill(decayCp(value)); }
};

template <>
struct CoroutineFulfiller<void>: CoroutineFulfillerBase<void> {
  void return_void() { fulfiller->fulfill(); }
};

namespace {
struct FriendAbuse;
}  // Anonymous namespace to avoid ODR violation in partial specialization of Promise, below.
}  // namespace _ (private)

template <>
class Promise<_::FriendAbuse> {
  // This class abuses partial specialization to gain friend access to a `kj::Promise`. Right now
  // all it supports is accessing the `kj::_::PromiseNode` pointer.
  //
  // TODO(soon): Find a better way to be friends with `kj::Promise`. This class is a hack.

public:
  template <typename T>
  static _::PromiseNode& getNode(Promise<T>& promise) { return *promise.node; }
};

namespace _ {

template <typename T>
class PromiseAwaiter {
  Promise<T> promise;
  PromiseNode& getNode() { return Promise<_::FriendAbuse>::getNode(promise); }

public:
  PromiseAwaiter(Promise<T>&& promise): promise(mv(promise)) {}

  bool await_ready() const { return false; }

  T await_resume();

  template <class CoroutineHandle>
  void await_suspend(CoroutineHandle c) {
    getNode().onReady(*c.promise().adapter);
  }
};

template <typename T>
inline T PromiseAwaiter<T>::await_resume() {
  _::ExceptionOr<T> result;
  getNode().get(result);
  KJ_IF_MAYBE(exception, mv(result.exception)) {
    throwFatalException(mv(*exception));
  }
  KJ_IF_MAYBE(value, mv(result.value)) {
    return mv(*value);
  }
  KJ_UNREACHABLE;
}

template <>
inline void PromiseAwaiter<void>::await_resume() {
  _::ExceptionOr<_::Void> result;
  getNode().get(result);
  KJ_IF_MAYBE(exception, mv(result.exception)) {
    throwFatalException(mv(*exception));
  }
}

}  // namespace _ (private)

template <class T>
auto operator co_await(Promise<T>& promise) {
  return _::PromiseAwaiter<T>{mv(promise)};
}

template <class T>
auto operator co_await(Promise<T>&& promise) {
  return _::PromiseAwaiter<T>{mv(promise)};
}
// Asynchronously wait for a promise inside of a coroutine returning kj::Promise. This operator is
// not (yet) supported inside any other coroutine type.
//
// Like .then() and friends, operator co_await consumes the promise passed to it, regardless of
// the promise's lvalue-ness. Instead of returning a new promise to you, it stores it inside an
// Awaitable, as defined by the Coroutines TS, which lives in the enclosing coroutine's context
// structure.

}  // namespace kj

namespace std {
namespace experimental {

template <class T, class... Args>
struct coroutine_traits<kj::Promise<T>, Args...> {
  using promise_type = kj::_::CoroutineFulfiller<T>;
};

}  // namespace experimental
}  // namespace std

#endif  // KJ_HAVE_COROUTINE

#endif  // KJ_COMPAT_COROUTINE_H_