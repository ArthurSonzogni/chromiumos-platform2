// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_ERROR_TESTING_HELPER_H_
#define LIBHWSEC_FOUNDATION_ERROR_TESTING_HELPER_H_

#include <type_traits>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libhwsec-foundation/status/status_chain.h"

namespace hwsec_foundation {
namespace error {
namespace testing {

using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

MATCHER(IsOk, "") {
  return arg.ok();
}

MATCHER(NotOk, "") {
  return !arg.ok();
}

// TODO(dlunev): figure out how to add error type matchers.

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
using remove_cvref_t =
    typename std::remove_cv<typename std::remove_reference<T>::type>::type;

ACTION_P(ReturnErrorType, error_ptr) {
  return OkStatus<remove_cvref_t<decltype(*error_ptr)>>();
}

ACTION_P2(ReturnErrorType, error_ptr, p1) {
  return MakeStatus<remove_cvref_t<decltype(*error_ptr)>>(p1);
}

ACTION_P3(ReturnErrorType, error_ptr, p1, p2) {
  return MakeStatus<remove_cvref_t<decltype(*error_ptr)>>(p1, p2);
}

ACTION_P4(ReturnErrorType, error_ptr, p1, p2, p3) {
  return MakeStatus<remove_cvref_t<decltype(*error_ptr)>>(p1, p2, p3);
}

ACTION_P5(ReturnErrorType, error_ptr, p1, p2, p3, p4) {
  return MakeStatus<remove_cvref_t<decltype(*error_ptr)>>(p1, p2, p3, p4);
}

ACTION_P6(ReturnErrorType, error_ptr, p1, p2, p3, p4, p5) {
  return MakeStatus<remove_cvref_t<decltype(*error_ptr)>>(p1, p2, p3, p4, p5);
}

ACTION_P7(ReturnErrorType, error_ptr, p1, p2, p3, p4, p5, p6) {
  return MakeStatus<remove_cvref_t<decltype(*error_ptr)>>(p1, p2, p3, p4, p5,
                                                          p6);
}

ACTION_P8(ReturnErrorType, error_ptr, p1, p2, p3, p4, p5, p6, p7) {
  return MakeStatus<remove_cvref_t<decltype(*error_ptr)>>(p1, p2, p3, p4, p5,
                                                          p6, p7);
}

ACTION_P9(ReturnErrorType, error_ptr, p1, p2, p3, p4, p5, p6, p7, p8) {
  return MakeStatus<remove_cvref_t<decltype(*error_ptr)>>(p1, p2, p3, p4, p5,
                                                          p6, p7, p8);
}

template <typename ErrType, typename... Args>
auto ReturnError(Args&&... args) {
  return ReturnErrorType(static_cast<ErrType*>(nullptr),
                         std::forward<Args>(args)...);
}

}  // namespace testing
}  // namespace error
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_ERROR_TESTING_HELPER_H_
