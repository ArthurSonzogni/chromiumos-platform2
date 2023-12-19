// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_MIDDLEWARE_SUBCLASS_HELPER_H_
#define LIBHWSEC_MIDDLEWARE_SUBCLASS_HELPER_H_

#include <concepts>
#include <type_traits>

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
};

// SubClass helper for the asynchronous backend call.
template <typename R, typename S, typename... Args>
  requires(std::constructible_from<R, Status>)
struct SubClassHelper<void (S::*)(base::OnceCallback<void(R)>, Args...)> {
  inline constexpr static CallType type = CallType::kAsync;
  using Result = R;
  using SubClass = S;
  using Callback = base::OnceCallback<void(R)>;
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

}  // namespace hwsec

#endif  // LIBHWSEC_MIDDLEWARE_SUBCLASS_HELPER_H_
