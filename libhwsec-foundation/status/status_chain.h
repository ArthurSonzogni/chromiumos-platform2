// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_STATUS_STATUS_CHAIN_H_
#define LIBHWSEC_FOUNDATION_STATUS_STATUS_CHAIN_H_

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
namespace __impl {

template <typename T>
std::false_type __is_status_chain(const T&);

// |StackableError| is aliased as |StatusChain| bellow.
template <typename _Et>
std::true_type __is_status_chain(const StackableError<_Et>&);

}  // namespace __impl

// Alias the traits to be publicly visible.
template <typename T>
using is_status_chain = decltype(__impl::__is_status_chain(std::declval<T>()));
template <typename T>
constexpr inline bool is_status_chain_v = is_status_chain<T>::value;
template <typename _Et>
using has_make_status_trait = __impl::has_make_status_trait<_Et>;
template <typename _Et>
inline constexpr bool has_make_status_trait_v =
    __impl::has_make_status_trait_v<_Et>;
template <typename _Et>
using has_base_error_type = __impl::has_base_error_type<_Et>;
template <typename _Et>
inline constexpr bool has_base_error_type_v =
    __impl::has_base_error_type_v<_Et>;

// Declare base |Error| type
using Error = __impl::Error;

// |StackableError| is the canonical Status holder for use in hwsec. Alias it
// to a Status resambling name.
template <typename _Et, typename _Bt = Error>
using StatusChain = __impl::StackableError<_Et>;

// Make a usable discard tag.
constexpr __impl::WrapTransformOnly WrapTransformOnly;

// Creates a new error object, wrapped in |StatusChain|. Custom overloads for
// error types may return a different object type, that might need to be dealt
// with in a certain way to get an object convertible to status type.
template <typename _Et, typename... Args>
auto MakeStatus(Args&&... args) {
  static_assert(std::is_base_of_v<Error, _Et> || std::is_same_v<Error, _Et>,
                "Supplied type is not derived from |Error|.");
  using MakeStatusTrait = typename _Et::MakeStatusTrait;
  return MakeStatusTrait()(std::forward<Args>(args)...);
}

// Factory function for |StatusChain| which by-passes the trait overload for
// creating a status. While it is not enforceable, this function should ONLY be
// used from inside |MakeStatusTrait| customization.
template <typename _Et, typename... Args>
StatusChain<_Et> NewStatus(Args&&... args) {
  static_assert(std::is_base_of_v<Error, _Et> || std::is_same_v<Error, _Et>,
                "Supplied type is not derived from |Error|.");
  return StatusChain<_Et>(new _Et(std::forward<Args>(args)...));
}

// Return |nullptr| error object in a typed |StatusChain| container.
template <typename _Et>
StatusChain<_Et> OkStatus() {
  static_assert(std::is_base_of_v<Error, _Et> || std::is_same_v<Error, _Et>,
                "Supplied type is not derived from |Error|.");
  return StatusChain<_Et>(nullptr);
}

// Specifies default behaviour of the MakeStatus on the object. Default is to
// pass the arguments to the constructor of |_Et|.
template <typename _Et>
struct DefaultMakeStatus {
  template <typename... Args>
  auto operator()(Args&&... args) {
    return StatusChain<_Et>(new _Et(std::forward<Args>(args)...));
  }
};

// Forbids MakeStatus on the object.
struct ForbidMakeStatus {};

}  // namespace status
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_STATUS_STATUS_CHAIN_H_
