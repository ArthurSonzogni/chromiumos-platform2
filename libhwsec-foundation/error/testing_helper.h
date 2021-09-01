// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_ERROR_TESTING_HELPER_H_
#define LIBHWSEC_FOUNDATION_ERROR_TESTING_HELPER_H_

#include <type_traits>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libhwsec-foundation/error/error.h"

namespace hwsec_foundation {
namespace error {
namespace testing {

/* A helper function to return generic error object in unittest.
 *
 * Example Usage:
 *
 * using ::hwsec_foundation::error::testing::ReturnError;
 *
 * ON_CALL(tpm, EncryptBlob(_, _, aes_skey, _))
 *     .WillByDefault(ReturnError<TPMErrorBase>());  // Always success.
 *
 * ON_CALL(tpm, EncryptBlob(_, _, _, _))
 *     .WillByDefault(
 *         ReturnError<TPMError>("fake", TPMRetryAction::kFatal));
 */

template <typename T>
using remove_const_ref =
    typename std::remove_cv<typename std::remove_reference<T>::type>::type;

ACTION_P(ReturnErrorType, error_ptr) {
  return remove_const_ref<decltype(*error_ptr)>(nullptr);
}

ACTION_P2(ReturnErrorType, error_ptr, p1) {
  return CreateError<remove_const_ref<decltype(*error_ptr)>>(p1);
}

ACTION_P3(ReturnErrorType, error_ptr, p1, p2) {
  return CreateError<remove_const_ref<decltype(*error_ptr)>>(p1, p2);
}

ACTION_P4(ReturnErrorType, error_ptr, p1, p2, p3) {
  return CreateError<remove_const_ref<decltype(*error_ptr)>>(p1, p2, p3);
}

ACTION_P5(ReturnErrorType, error_ptr, p1, p2, p3, p4) {
  return CreateError<remove_const_ref<decltype(*error_ptr)>>(p1, p2, p3, p4);
}

ACTION_P6(ReturnErrorType, error_ptr, p1, p2, p3, p4, p5) {
  return CreateError<remove_const_ref<decltype(*error_ptr)>>(p1, p2, p3, p4,
                                                             p5);
}

ACTION_P7(ReturnErrorType, error_ptr, p1, p2, p3, p4, p5, p6) {
  return CreateError<remove_const_ref<decltype(*error_ptr)>>(p1, p2, p3, p4, p5,
                                                             p6);
}

ACTION_P8(ReturnErrorType, error_ptr, p1, p2, p3, p4, p5, p6, p7) {
  return CreateError<remove_const_ref<decltype(*error_ptr)>>(p1, p2, p3, p4, p5,
                                                             p6, p7);
}

ACTION_P9(ReturnErrorType, error_ptr, p1, p2, p3, p4, p5, p6, p7, p8) {
  return CreateError<remove_const_ref<decltype(*error_ptr)>>(p1, p2, p3, p4, p5,
                                                             p6, p7, p8);
}

template <typename ErrType, typename... Args>
auto ReturnError(Args&&... args) {
  return ReturnErrorType(static_cast<ErrType*>(nullptr),
                         std::forward<Args>(args)...);
}

// Test helpers for CreateError.
template <typename ErrorType, typename... Args>
struct TestForCreateError {
  // primary template handles types that can't "CreateError".
  template <typename InnerErrorType, typename = void, typename... InnerArgs>
  struct CheckImpl : std::false_type {};

  // specialization recognizes types that can "CreateError".
  template <typename InnerErrorType, typename... InnerArgs>
  struct CheckImpl<InnerErrorType,
                   std::void_t<decltype(CreateError<InnerErrorType>(
                       std::declval<InnerArgs>()...))>,
                   InnerArgs...> : std::true_type {};

  using Check = CheckImpl<ErrorType, void, Args...>;
};

// Test helpers for WrapError.
template <typename ErrorType, typename ErrorType2, typename... Args>
struct TestForWrapError {
  // primary template handles types that can't "WrapError".
  template <typename InnerErrorType,
            typename InnerErrorType2,
            typename = void,
            typename... InnerArgs>
  struct CheckImpl : std::false_type {};

  // specialization recognizes types that can "WrapError".
  template <typename InnerErrorType,
            typename InnerErrorType2,
            typename... InnerArgs>
  struct CheckImpl<
      InnerErrorType,
      InnerErrorType2,
      std::void_t<decltype(WrapError<InnerErrorType>(
          std::declval<InnerErrorType2>(), std::declval<InnerArgs>()...))>,
      InnerArgs...> : std::true_type {};

  using Check = CheckImpl<ErrorType, ErrorType2, void, Args...>;
};

}  // namespace testing
}  // namespace error
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_ERROR_TESTING_HELPER_H_
