// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_STATUS_IMPL_ERROR_H_
#define LIBHWSEC_FOUNDATION_STATUS_IMPL_ERROR_H_

#include <string>

#include "libhwsec-foundation/status/impl/stackable_error_forward_declarations.h"

#include "libhwsec-foundation/hwsec-foundation_export.h"

#include "libhwsec-foundation/status/impl/stackable_error.h"
#include "libhwsec-foundation/status/impl/stackable_error_range.h"

namespace hwsec_foundation {
namespace status {
namespace __impl {

// Base class for error in the hwsec code.
// Note, the export qualifier will be ignored if the class is built as a static
// library. Thus, it has to be directly linked into the target shared library.
class HWSEC_FOUNDATION_EXPORT Error {
 public:
  // Make the base error instantiable with standard StatusChain helpers. Since
  // we have not defined those helpers yet, do the necessary instantiations
  // manually. Note, that derived custom errors should never use |new|,
  // they should use |NewStatus| in this context. See `../status_chain.h` and
  // `../README.md` for more info.
  struct MakeStatusTrait {
    auto operator()() { return StackableError<Error>(new Error()); }
    auto operator()(std::string message) {
      return StackableError<Error>(new Error(message));
    }
  };

  // |BaseErrorType| suggests the required type for the underlying pointer in
  // the stack. Only errors with the same |BaseErrorType| can be in one chain.
  // This type will be returned in ranges and iterators.
  using BaseErrorType = Error;

  Error() = default;
  // The reason this is not inlined is because we have to force symbol
  // resolution when attaching a shared library with the object, for otherwise
  // shared library and its user will generate two different RTTI for inlined
  // instances of the class, which will prevent use of dynamic_cast for error
  // polymorphism. Thus, we need to have an incomplete definition of the class
  // in the header, have a separate compile unit to generate obj artifact, and
  // mark the class for being put in the exported section.
  explicit Error(std::string message);
  Error(const Error&) = default;
  Error& operator=(const Error&) = default;
  Error(Error&&) = default;
  Error& operator=(Error&&) = default;
  virtual ~Error() = default;

  // Allows to transform the current error object during wrapping. It is
  // supplied with a const-iterrable range over the wrapped stack.
  // The base class uses |StackableErrorConstRange|, but it is not exposed to
  // the clients. Clients shall declare the overrides as
  // |void WrapTransform(StatusChain<BaseErrorType>::const_iterator_range)|
  // Note, that the function is a non virtual and template to allow argument
  // type overload. The correct overload choice is enforced by the call site in
  // |StackableError|.
  // |WrapTransform| is added as transitional functionality and might be removed
  // later.
  template <typename _Bt>
  void WrapTransform(StackableErrorConstRange<_Bt> range) {}

  // To string converts the Error to a printable string.
  virtual std::string ToString() const { return message_; }

  // RTTI methods are added as transitional functionality and might be removed
  // later.

  // Check if an error pointer is of a certain type. Uses RTTI and relies on
  // 'one-definition' rule to work correctly.
  template <typename _Dt>
  static bool Is(Error* error) {
    return RTTI::Is<_Dt>(error);
  }

  // Const overload for |Is<_Dt>|
  template <typename _Dt>
  static bool Is(const Error* error) {
    return RTTI::Is<_Dt>(error);
  }

  // Converts the pointer a certain type. Uses RTTI and relies on
  // 'one-definition' rule to work correctly. Returns error if casting fails.
  template <typename _Dt>
  static _Dt* Cast(Error* error) {
    return RTTI::Cast<_Dt>(error);
  }

  // Const overload for |Cast<_Dt>|
  template <typename _Dt>
  static const _Dt* Cast(const Error* error) {
    return RTTI::Cast<_Dt>(error);
  }

 private:
  std::string message_;
};

}  // namespace __impl
}  // namespace status
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_STATUS_IMPL_ERROR_H_
