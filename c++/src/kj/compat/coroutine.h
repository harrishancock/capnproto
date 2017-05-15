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

// =======================================================================================
// coroutine_traits<kj::Promise<T>, ...>::promise_type implementation

namespace kj {
namespace _ {

struct CoroutineAdapter;

template <typename T>
class CoroutineImplBase {
public:
  Promise<T> get_return_object() {
    return newAdaptedPromise<T, CoroutineAdapter>(*this);
  }

  auto initial_suspend() { return std::experimental::suspend_never{}; }
  auto final_suspend() { return std::experimental::suspend_never{}; }

  void unhandled_exception() {
    fulfiller->rejectIfThrows([] { std::rethrow_exception(std::current_exception()); });
  }

  void set_exception(std::exception_ptr e) {
    // TODO(msvc): Remove when MSVC updates to use unhandled_exception().
    fulfiller->rejectIfThrows([e = mv(e)] { std::rethrow_exception(mv(e)); });
  }

  ~CoroutineImplBase() { adapter->coroutine = nullptr; }

protected:
  friend struct CoroutineAdapter;
  CoroutineAdapter* adapter;
  PromiseFulfiller<T>* fulfiller;
};

template <typename T>
class CoroutineImpl: public CoroutineImplBase<T> {
public:
  void return_value(T&& value) { fulfiller->fulfill(mv(value)); }
};

template <>
class CoroutineImpl<void>: public CoroutineImplBase<void> {
public:
  void return_void() { fulfiller->fulfill(); }
};

struct CoroutineAdapter {
  template <typename T>
  CoroutineAdapter(PromiseFulfiller<T>& f, CoroutineImplBase<T>& impl)
      : coroutine(std::experimental::coroutine_handle<>::from_address(&impl))
  {
    impl.adapter = this;
    impl.fulfiller = &f;
  }

  ~CoroutineAdapter() noexcept(false) { if (coroutine) { coroutine.destroy(); } }

  std::experimental::coroutine_handle<> coroutine;
};

}  // namespace _ (private)
}  // namespace kj

namespace std {
namespace experimental {

template <class T, class... Args>
struct coroutine_traits<kj::Promise<T>, Args...> {
  using promise_type = kj::_::CoroutineImpl<T>;
};

}  // namespace experimental
}  // namespace std

// =======================================================================================
// co_await kj::Promise implementation

namespace kj {
namespace _ {

template <typename T>
class PromiseAwaiter {
public:
  PromiseAwaiter(Promise<T>&& p): promise(mv(p)) {}

  bool await_ready() const { return false; }

  T await_resume() {
    // Copied from Promise::wait() implementation.
    KJ_IF_MAYBE(value, result.value) {
      KJ_IF_MAYBE(exception, result.exception) {
        throwRecoverableException(kj::mv(*exception));
      }
      return _::returnMaybeVoid(kj::mv(*value));
    } else KJ_IF_MAYBE(exception, result.exception) {
      throwFatalException(kj::mv(*exception));
    } else {
      // Result contained neither a value nor an exception?
      KJ_UNREACHABLE;
    }
  }

  void await_suspend(std::experimental::coroutine_handle<> c) {
    promise2 = promise.then([this, c](T&& r) {
      return wakeUp(c, mv(r));
    }).eagerlyEvaluate([this, c](Exception&& e) {
      return wakeUp(c, {false, mv(e)});
    });
  }

private:
  Promise<void> wakeUp(std::experimental::coroutine_handle<> c, ExceptionOr<FixVoid<T>>&& r) {
    result = mv(r);
    KJ_DEFER(c.resume());
    return mv(promise2);
  }

  Promise<T> promise;
  Promise<void> promise2{NEVER_DONE};
  ExceptionOr<FixVoid<T>> result;
};

template <>
void PromiseAwaiter<void>::await_suspend(std::experimental::coroutine_handle<> c) {
  promise2 = promise.then([this, c]() {
    return wakeUp(c, Void{});
  }).eagerlyEvaluate([this, c](Exception&& e) {
    return wakeUp(c, {false, mv(e)});
  });
}

}  // namespace _ (private)

template <class T>
auto operator co_await(Promise<T>& promise) { return _::PromiseAwaiter<T>{mv(promise)}; }
template <class T>
auto operator co_await(Promise<T>&& promise) { return _::PromiseAwaiter<T>{mv(promise)}; }
// Asynchronously wait for a promise inside of a coroutine returning kj::Promise. This operator is
// not (yet) supported inside any other coroutine type.
//
// Like .then() and friends, operator co_await consumes the promise passed to it, regardless of
// the promise's lvalue-ness. Instead of returning a new promise to you, it stores it inside an
// Awaitable, as defined by the Coroutines TS, which lives in the enclosing coroutine's context
// structure.

}  // namespace kj

#endif  // KJ_HAVE_COROUTINE

#endif  // KJ_COMPAT_COROUTINE_H_