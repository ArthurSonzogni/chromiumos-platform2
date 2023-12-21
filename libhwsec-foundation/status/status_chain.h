// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_STATUS_STATUS_CHAIN_H_
#define LIBHWSEC_FOUNDATION_STATUS_STATUS_CHAIN_H_

#include <concepts>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

// error.h is the finalized header, do not include any other impl headers
// directly.
#include "libhwsec-foundation/status/impl/error.h"

namespace hwsec_foundation {
namespace status {

// Trait check for being a |StatusChain| holding type.
namespace _impl_ {
template <typename T>
struct is_status_chain : std::false_type {};
template <typename _Et>
struct is_status_chain<StackableError<_Et>> : std::true_type {};
}  // namespace _impl_

template <typename T>
concept IsStatusChain = _impl_::is_status_chain<T>::value;

// Declare base |Error| type
using Error = _impl_::Error;

// |StackableError| is the canonical Status holder for use in hwsec. Alias it
// to a Status resambling name.
template <typename _Et>
using StatusChain = _impl_::StackableError<_Et>;

// Make a usable discard tag.
inline constexpr _impl_::WrapTransformOnly WrapTransformOnly;

// Factory function for |StatusChain| which by-passes the trait overload for
// creating a status. While it is not enforceable, this function should ONLY be
// used from inside |MakeStatusTrait| customization.
template <typename _Et, typename... Args>
  requires(ErrorType<_Et>)
[[clang::return_typestate(unconsumed)]] StatusChain<_Et> NewStatus(
    Args&&... args) {
  return StatusChain<_Et>(new _Et(std::forward<Args>(args)...));
}

// Return |nullptr| error object in a typed |StatusChain| container.
template <typename _Et>
  requires(ErrorType<_Et>)
[[clang::return_typestate(consumed)]] StatusChain<_Et> OkStatus() {
  return StatusChain<_Et>();
}

// Return |nullptr| error object in a typed |const StatusChain| container.
template <typename _Et>
[[clang::return_typestate(consumed)]] const StatusChain<_Et>&
ConstRefOkStatus() {
  static const StatusChain<_Et> kOkStatus = OkStatus<_Et>();
  return kOkStatus;
}

// Indicates the MakeStatusTrait will always make not ok status.
struct AlwaysNotOk {};

// Specifies default behaviour of the MakeStatus on the object. Default is to
// pass the arguments to the constructor of |_Et|.
template <typename _Et>
struct DefaultMakeStatus : public AlwaysNotOk {
  template <typename... Args>
  [[clang::return_typestate(unconsumed)]] auto operator()(Args&&... args) {
    return StatusChain<_Et>(new _Et(std::forward<Args>(args)...));
  }
};

// Forbids MakeStatus on the object.
struct ForbidMakeStatus {};

template <typename MakeStatusTrait, typename... Args>
concept CanMakeUnconsumedStatus =
    std::derived_from<MakeStatusTrait, AlwaysNotOk> &&
    requires(Args&&... args) {
      // We can only set unconsumed to status chain object.
      { MakeStatusTrait()(std::forward<Args>(args)...) } -> IsStatusChain;
    };

// Creates a new error object, wrapped in |StatusChain|. Custom overloads for
// error types may return a different object type, that might need to be dealt
// with in a certain way to get an object convertible to status type.
template <typename _Et, typename... Args>
  requires(ErrorType<_Et> &&
           CanMakeUnconsumedStatus<typename _Et::MakeStatusTrait, Args...>)
[[clang::return_typestate(unconsumed)]] auto MakeStatus(Args&&... args) {
  return typename _Et::MakeStatusTrait()(std::forward<Args>(args)...);
}

// It the MakeStatusTrait is not derived from the AlwaysNotOk, we should check
// the result of it before using it.
template <typename _Et, typename... Args>
  requires(ErrorType<_Et> &&
           !CanMakeUnconsumedStatus<typename _Et::MakeStatusTrait, Args...>)
auto MakeStatus(Args&&... args) {
  return typename _Et::MakeStatusTrait()(std::forward<Args>(args)...);
}

}  // namespace status
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_STATUS_STATUS_CHAIN_H_
