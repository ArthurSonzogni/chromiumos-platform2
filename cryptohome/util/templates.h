// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Various useful templates that are not really specific to any particular
// component within cryptohome.

#ifndef CRYPTOHOME_UTIL_TEMPLATES_H_
#define CRYPTOHOME_UTIL_TEMPLATES_H_

namespace cryptohome {

// Given a function pointer type, compute the return type of the function.
//
// Normally you'd want to use std::invoke_result for this as that supports
// arbitrary function-like objects, but in order to work correctly in the case
// of overloads it requires you to know the types of the arguments being passed.
// In the case where you have a pointer to a specific function already, this
// version avoids that dependency.
template <typename T>
struct FunctionPtrReturn;
template <typename R, typename... Args>
struct FunctionPtrReturn<R (*)(Args...)> {
  using type = R;
};
template <typename T>
using FunctionPtrReturnType = FunctionPtrReturn<T>::type;

}  // namespace cryptohome

#endif  // CRYPTOHOME_UTIL_TEMPLATES_H_
