// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_ERROR_ERROR_H_
#define LIBHWSEC_FOUNDATION_ERROR_ERROR_H_

#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>

namespace hwsec_foundation {
namespace error {

/* A generic error object for error handling.
 *
 * Example Usage:
 *
 * TPMErrorBase Foo() {
 *   if (auto err = CreateError<TPM1Error>(0x87)) {
 *     return CreateErrorWrap<TPMError>(std::move(err), "failed to bla");
 *   }
 *   LOG(INFO) << "Good job";
 *   return nullptr;
 * }
 *
 * bool Bar() {
 *   if (auto err = Foo()) {
 *     LOG(ERROR) << "Failed to foo: " << *err;
 *     return false;
 *   }
 *   return true;
 * }
 */

// When ErrorBase == nullptr, means success.
class ErrorBaseObj;
using ErrorBase = std::unique_ptr<ErrorBaseObj>;

template <typename ErrorType>
struct UnwarpErrorType {
  using type = typename std::remove_reference<decltype(*ErrorType())>::type;
};

template <typename T>
std::false_type is_error_type_impl(const T&);

template <
    typename ErrorTypeObj,
    typename std::enable_if<
        std::is_base_of<ErrorBaseObj, ErrorTypeObj>::value>::type* = nullptr>
std::true_type is_error_type_impl(const std::unique_ptr<ErrorTypeObj>&);

template <typename T>
using is_error_type = decltype(is_error_type_impl(std::declval<T>()));

class ErrorBaseObj {
 public:
  ErrorBaseObj(const ErrorBaseObj&) = delete;
  ErrorBaseObj& operator=(const ErrorBaseObj&) = delete;
  virtual ~ErrorBaseObj() = default;

  // Converts the error object to a readable string.
  virtual std::string ToReadableString() const = 0;

  // Creates a copy ot this error object without inner error.
  virtual ErrorBase SelfCopy() const = 0;

  // Creates a copy of this error object including inner error.
  ErrorBase FullCopy() const {
    auto copy = SelfCopy();
    if (copy && inner_error_) {
      copy->Wrap(inner_error_->FullCopy());
    }
    return copy;
  }

  // Returns the readable string for the full chain.
  std::string ToFullReadableString() const {
    if (inner_error_)
      return ToReadableString() + ": " + inner_error_->ToFullReadableString();
    return ToReadableString();
  }

  // Wraps an error into the error chain.
  void Wrap(ErrorBase err) {
    ErrorBaseObj* obj_ptr = this;
    while (obj_ptr->inner_error_) {
      obj_ptr = obj_ptr->inner_error_.get();
    }
    obj_ptr->inner_error_ = std::move(err);
    return;
  }

  // Unwraps the error from the error chain.
  ErrorBase UnWrap() {
    ErrorBase result = std::move(inner_error_);
    return result;
  }

  // Checks the specific type of error.
  template <typename ErrorType>
  bool Is() const {
    static_assert(is_error_type<ErrorType>::value,
                  "ErrorType isn't a valid error type.");
    using ObjType = typename UnwarpErrorType<ErrorType>::type;
    const ObjType* cast = dynamic_cast<const ObjType*>(this);
    return cast;
  }

  // Casts the error to a specific type of error.
  // Returns nullptr when the casting failed.
  template <typename ErrorType>
  auto Cast() {
    static_assert(is_error_type<ErrorType>::value,
                  "ErrorType isn't a valid error type.");
    using ObjType = typename UnwarpErrorType<ErrorType>::type;
    ObjType* cast = dynamic_cast<ObjType*>(this);
    return cast;
  }

  // Finds the specific type of error in the error chain.
  // Returns nullptr when the specific type of error not found.
  // This function would do multiple times of dynamic_cast and fully copy the
  // result. Think twice before using this function.
  template <typename ErrorType>
  ErrorType As() const {
    static_assert(is_error_type<ErrorType>::value,
                  "ErrorType isn't a valid error type.");
    using ObjType = typename UnwarpErrorType<ErrorType>::type;
    const ErrorBaseObj* obj_ptr = this;
    while (obj_ptr) {
      const ObjType* cast = dynamic_cast<const ObjType*>(obj_ptr);
      if (cast) {
        ErrorBase copy = cast->FullCopy();
        ErrorType result(dynamic_cast<ObjType*>(copy.get()));
        if (result) {
          copy.release();
        }
        return result;
      }
      obj_ptr = obj_ptr->inner_error_.get();
    }
    return nullptr;
  }

 protected:
  ErrorBaseObj() = default;
  ErrorBaseObj(ErrorBaseObj&&) = default;

 private:
  ErrorBase inner_error_;
};

// A helper to create specific type of error.
template <typename ErrorType,
          typename... Args,
          decltype(typename UnwarpErrorType<ErrorType>::type(
              std::forward<Args>(std::declval<Args&&>())...))* = nullptr>
ErrorType CreateError(Args&&... args) {
  static_assert(is_error_type<ErrorType>::value,
                "ErrorType isn't a valid error type.");
  using ObjType = typename UnwarpErrorType<ErrorType>::type;
  return std::make_unique<ObjType>(std::forward<Args>(args)...);
}

// A helper to create specific type of error and wrap an inner error into it.
template <typename ErrorType,
          typename InnerErrorType,
          typename... Args,
          decltype(typename UnwarpErrorType<ErrorType>::type(
              std::forward<Args>(std::declval<Args&&>())...))* = nullptr>
ErrorType CreateErrorWrap(InnerErrorType inner_err, Args&&... args) {
  static_assert(is_error_type<ErrorType>::value,
                "ErrorType isn't a valid error type.");
  static_assert(is_error_type<InnerErrorType>::value,
                "InnerErrorType isn't a valid error type.");
  auto err = CreateError<ErrorType>(std::forward<Args>(args)...);
  if (err) {
    err->Wrap(std::move(inner_err));
  }
  return err;
}

// Let the ErrorBaseObj be printable.
template <
    typename ErrorObjType,
    typename std::enable_if<
        std::is_base_of<ErrorBaseObj, ErrorObjType>::value>::type* = nullptr>
std::ostream& operator<<(std::ostream& os, const ErrorObjType& err) {
  os << err.ToFullReadableString();
  return os;
}

}  // namespace error
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_ERROR_ERROR_H_
