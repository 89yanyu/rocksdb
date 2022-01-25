// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once
#include <coroutine>
#include <type_traits>

#include "rocksdb/rocksdb_namespace.h"

namespace ROCKSDB_NAMESPACE {

namespace detail {
template <typename U>
struct value_t {
  U value;

  // implicit conversions
  operator U&() & { return this->value; }
  operator const U&() const& { return this->value; }
  operator U&&() && { return std::move(this->value); }
  U release() { return std::move(this->value); }
};
struct empty_t {};
};  // namespace detail

template <typename T = void>
struct AsyncResult
    : public std::conditional_t<std::is_void_v<T>, detail::empty_t,
                                detail::value_t<T>> {
  template <typename U>
  struct return_base_t {
    AsyncResult<U>* result;
    ~return_base_t() {
      if (result) {
        result->pr = nullptr;
      }
    }
    void release_result() { result = nullptr; }
  };
  template <typename U>
  struct return_value_t : public return_base_t<U> {
    template <typename V>
    void return_value(V&& value) {
      if (this->result) {
        this->result->value = std::forward<V>(value);
        this->result->done = true;
      }
    }
  };
  struct return_void_t : public return_base_t<void> {
    void return_void() {
      if (this->result) {
        this->result->done = true;
      }
    }
  };
  struct promise_type
      : public std::conditional_t<std::is_void_v<T>, return_void_t,
                                  return_value_t<T>> {
    void* prev = nullptr;

    AsyncResult<T> get_return_object() {
      AsyncResult<T> res(this);
      this->result = &res;
      return res;
    }
    auto initial_suspend() { return std::suspend_never{}; }
    auto final_suspend() {
      if (prev) {
        std::coroutine_handle<>::from_address(prev).resume();
      }
      return std::suspend_never{};
    }
    auto unhandled_exception() { abort(); }
  };
  promise_type* pr;
  bool done;

  AsyncResult(promise_type* promise) : pr(promise), done(false) {}
  ~AsyncResult() {
    if (pr) {
      pr->release_result();
    }
  }
  bool await_ready() const noexcept { return done; }
  template <typename U>
  void await_suspend(std::coroutine_handle<U> h) {
    if (pr) {
      pr->prev = h.address();
    }
  }
  void await_resume() const noexcept {}
};

}  // namespace ROCKSDB_NAMESPACE
