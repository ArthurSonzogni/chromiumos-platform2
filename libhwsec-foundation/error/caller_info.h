// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_ERROR_CALLER_INFO_H_
#define LIBHWSEC_FOUNDATION_ERROR_CALLER_INFO_H_

#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "libhwsec-foundation/error/error.h"

namespace hwsec_foundation {
namespace error {

/* A helper object to include file and line info in the error object.
 *
 * Example Usage:
 *
 * using TPM1CallerError = CallerInfoError<TPM1Error>;
 *
 * auto err = CreateError<TPM1CallerError>(CallerInfoArgs, 0x87);
 *
 * if (err) {
 *   LOG(INFO) << *err;
 * }
 */
template <typename ErrorType>
class CallerInfoObj : public UnwarpErrorType<ErrorType>::type {
  static_assert(is_error_type<ErrorType>::value,
                "ErrorType isn't a valid error type.");

 public:
  using ObjType = typename UnwarpErrorType<ErrorType>::type;
  using ErrType = ErrorType;
  CallerInfoObj(const char* func, const char* file, int line, ErrorType err)
      : ObjType(std::move(*err)), func_(func), file_(file), line_(line) {}

  std::string ToReadableString() const override {
    return base::StringPrintf("%s:%d %s: %s", file_, line_, func_,
                              ObjType::ToReadableString().c_str());
  }

  ErrorBase SelfCopy() const override {
    ErrorBase err = ObjType::SelfCopy();
    ErrorType convert(dynamic_cast<ObjType*>(err.get()));
    if (!convert) {
      return nullptr;
    }
    err.release();
    return std::make_unique<CallerInfoObj>(func_, file_, line_,
                                           std::move(convert));
  }

 private:
  const char* const func_;
  const char* const file_;
  const int line_;
};

template <typename ErrorType>
using CallerInfoError = std::unique_ptr<CallerInfoObj<ErrorType>>;

template <typename ErrorType,
          typename std::enable_if<
              std::is_base_of<ErrorBaseObj, ErrorType>::value>::type* = nullptr>
std::true_type is_caller_info_error_type_impl(const CallerInfoObj<ErrorType>&);

template <typename T>
std::false_type is_caller_info_error_type_impl(const T&);

template <typename T>
using is_caller_info_error_type =
    decltype(is_caller_info_error_type_impl(std::declval<T>()));

template <
    typename CallerInfoErrorType,
    typename... Args,
    typename is_caller_info_error_type<CallerInfoErrorType>::type* = nullptr,
    decltype(typename UnwarpErrorType<CallerInfoErrorType>::type::ObjType(
        std::forward<Args>(std::declval<Args&&>())...))* = nullptr>
CallerInfoErrorType CreateError(const char* func,
                                const char* file,
                                int line,
                                Args&&... args) {
  auto result =
      CreateError<typename UnwarpErrorType<CallerInfoErrorType>::type::ErrType>(
          std::forward<Args>(args)...);
  if (!result) {
    return nullptr;
  }
  return std::make_unique<CallerInfoObj<
      typename UnwarpErrorType<CallerInfoErrorType>::type::ErrType>>(
      func, file, line, std::move(result));
}

#define CALLER_INFO_ARGS __func__, __FILE__, __LINE__

}  // namespace error
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_ERROR_CALLER_INFO_H_
