// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_MIDDLEWARE_SUBCLASS_HELPER_H_
#define LIBHWSEC_MIDDLEWARE_SUBCLASS_HELPER_H_

#include <concepts>
#include <type_traits>
#include <utility>

#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>

#include "libhwsec/status.h"

#ifndef BUILD_LIBHWSEC
#error "Don't include this file outside libhwsec!"
#endif

namespace hwsec {

// Type of the backend call.
enum class CallType {
  // Synchronous backend call, and the function signature will be:
  // Result SubClass::Function(Args...);
  kSync,

  // Asynchronous backend call, and the function signature will be:
  // void SubClass::Function(base::OnceCallback<void(Result)>. Args...);
  kAsync,
};

template <typename Func>
struct SubClassHelper {
  static_assert(sizeof(Func) == -1, "Unknown member function");
};

// SubClass helper for the synchronous backend call.
template <typename R, typename S, typename... Args>
  requires(std::constructible_from<R, Status>)
struct SubClassHelper<R (S::*)(Args...)> {
  inline constexpr static CallType type = CallType::kSync;
  using Result = R;
  using SubClass = S;
  using Callback = base::OnceCallback<void(R)>;
  static R Signature(Args...);
};

// SubClass helper for the asynchronous backend call.
template <typename R, typename S, typename... Args>
  requires(std::constructible_from<R, Status>)
struct SubClassHelper<void (S::*)(base::OnceCallback<void(R)>, Args...)> {
  inline constexpr static CallType type = CallType::kAsync;
  using Result = R;
  using SubClass = S;
  using Callback = base::OnceCallback<void(R)>;
  static R Signature(Args...);
};

template <typename Func>
using SubClassResult = typename SubClassHelper<Func>::Result;
template <typename Func>
using SubClassType = typename SubClassHelper<Func>::SubClass;
template <typename Func>
using SubClassCallback = typename SubClassHelper<Func>::Callback;

template <typename Func>
concept SyncBackendMethod = SubClassHelper<Func>::type == CallType::kSync;
template <typename Func>
concept AsyncBackendMethod = SubClassHelper<Func>::type == CallType::kAsync;
template <typename Func>
concept BackendMethod = SyncBackendMethod<Func> || AsyncBackendMethod<Func>;

// The custom parameter forwarding rules.
template <typename T>
  requires(!std::is_pointer_v<T>)
static T ForwardParameter(T&& t) {
  // The rvalue should still be rvalue, because we have the ownership.
  return t;
}

template <typename T>
  requires(!std::is_pointer_v<T>)
static const T& ForwardParameter(T& t) {
  // Add const for normal reference, because we don't have the ownership.
  // base::BindOnce will copy const reference parameter when binding.
  return t;
}

template <typename T>
  requires(!std::is_pointer_v<T>)
static const T& ForwardParameter(const T& t) {
  // The const reference would still be const reference.
  // base::BindOnce will copy const reference parameter when binding.
  return t;
}

template <typename Func, typename... Args>
concept ValidBackendMethodArgs =
    BackendMethod<Func> && requires(Args&&... args) {
      // Validate the function signature is callable with the forwarded
      // arguments.
      SubClassHelper<Func>::Signature(
          ForwardParameter(std::forward<Args>(args))...);
    };

}  // namespace hwsec

#endif  // LIBHWSEC_MIDDLEWARE_SUBCLASS_HELPER_H_
