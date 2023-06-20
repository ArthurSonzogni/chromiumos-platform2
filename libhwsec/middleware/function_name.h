// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_MIDDLEWARE_FUNCTION_NAME_H_
#define LIBHWSEC_MIDDLEWARE_FUNCTION_NAME_H_

#include <deque>
#include <string>

#include <brillo/type_name_undecorate.h>

#ifndef BUILD_LIBHWSEC
#error "Don't include this file outside libhwsec!"
#endif

// This function helper can help us get the function name form the function
// type.
//
// Example usage:
//   bool MagicFunction() {
//     return GetFuncName<&MagicFunction>() == "MagicFunction";
//   }  // return true;

namespace hwsec {

template <auto Func>
struct FuncWrapper {};

// Input: hwsec::FuncWrapper<&hwsec::State::IsReady<...>>
// Output: hwsec::State::IsReady
std::string ExtractFuncName(const std::string& func_name);

// Input: hwsec::State::IsReady
// Output: State.IsReady
std::string SimplifyFuncName(const std::string& func_name);

template <auto Func>
inline std::string GetFuncName() {
  return ExtractFuncName(brillo::GetUndecoratedTypeName<FuncWrapper<Func>>());
}

}  // namespace hwsec

#endif  // LIBHWSEC_MIDDLEWARE_FUNCTION_NAME_H_
