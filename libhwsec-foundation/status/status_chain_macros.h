// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_STATUS_STATUS_CHAIN_MACROS_H_
#define LIBHWSEC_FOUNDATION_STATUS_STATUS_CHAIN_MACROS_H_

#include <string>
#include <utility>

#include <base/logging.h>

#include "libhwsec-foundation/status/status_chain.h"

// Convenience macro to use with libhwsec StatusChain.
//
// RETURN_IF_ERROR replaces the need for an explicit check of the returned
// status if the code only needs to wrap it and propagate forward.
//
// The following example can be simplified by using the macro
//
// StatusChain<ErrorType> f() {}
// ...
// if (StatusChain<ErrorType> status = f(); !status.ok()) {
//  return MakeStatus<AnotherErrorType>(args).Wrap(std::move(status));
// }
// ...
// RETURN_IF_ERROR(f(), MakeStatus<AnotherErrorType>(args));
//
// If the code only needs to propagate the error without modification, AsIs()
// stub can be used.
//
// RETURN_IF_ERROR(f(), AsIs());
//
// if the returned value of the function is not StatusChain, AsValue() use as
// value.
//
// RETURN_IF_ERROR(f(), AsValue(42));
//
// *WithLog variant prints the error message and status.ToFullString before
// returning.
//
// RETURN_IF_ERROR(f(), AsIsWithLog("some log"));
//
// AsFalseWithLog is a short hand for AsValueWithLog<bool>(false, message).

#ifdef RETURN_IF_ERROR
#error "RETURN_IF_ERROR is defined in the scope."
#endif

namespace hwsec_foundation {
namespace status {

struct AsIs {
  template <typename _Et>
  _Et&& Wrap(_Et&& obj) {
    return std::move(obj);
  }
};

struct AsIsWithLog {
  explicit AsIsWithLog(std::string message) : message_(message) {}

  template <typename _Et>
  _Et&& Wrap(_Et&& obj) {
    LOG(ERROR) << message_ << ": " << obj;
    return std::move(obj);
  }

  std::string message_;
};

template <typename _Et, typename... Args>
inline auto AsStatus(Args&&... args) {
  return ::hwsec_foundation::status::MakeStatus<_Et>(
      std::forward<Args>(args)...);
}

struct WithLog {
  explicit WithLog(std::string message) : message_(message) {}

  template <typename _Et>
  void Wrap(_Et&& obj) {
    LOG(ERROR) << message_ << ": " << obj;
  }

  std::string message_;
};

template <typename _Rt>
struct AsValue {
  explicit AsValue(_Rt value) : value_(value) {}

  template <typename _Et>
  _Rt Wrap(_Et&& obj) {
    return value_;
  }

  _Rt value_;
};

template <typename _Rt>
struct AsValueWithLog {
  explicit AsValueWithLog(_Rt value, std::string message)
      : value_(value), message_(message) {}

  template <typename _Et>
  _Rt Wrap(_Et&& obj) {
    LOG(ERROR) << message_ << ": " << obj;
    return value_;
  }

  _Rt value_;
  std::string message_;
};

struct AsFalseWithLog {
  explicit AsFalseWithLog(std::string message) : message_(message) {}

  template <typename _Et>
  bool Wrap(_Et&& obj) {
    LOG(ERROR) << message_ << ": " << obj;
    return false;
  }

  std::string message_;
};

}  // namespace status
}  // namespace hwsec_foundation

#define RETURN_IF_ERROR(rexpr, wrapper)         \
  do {                                          \
    auto status = (rexpr);                      \
    if (!status.ok()) {                         \
      return (wrapper).Wrap(std::move(status)); \
    }                                           \
  } while (0)

#endif  // LIBHWSEC_FOUNDATION_STATUS_STATUS_CHAIN_MACROS_H_
