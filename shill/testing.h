// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_TESTING_H_
#define SHILL_TESTING_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/test/test_future.h>

#include "shill/device.h"
#include "shill/error.h"

namespace shill {

MATCHER(IsSuccess, "") {
  return arg.IsSuccess();
}

MATCHER(IsFailure, "") {
  return arg.IsFailure();
}

MATCHER_P(ErrorTypeIs, error_type, "") {
  return error_type == arg.type();
}

// Use this matcher instead of passing RefPtrs directly into the arguments
// of EXPECT_CALL() because otherwise we may create un-cleaned-up references at
// system teardown.
MATCHER_P(IsRefPtrTo, ref_address, "") {
  return arg.get() == ref_address;
}

base::OnceCallback<void(const Error&)> GetResultCallback(
    base::test::TestFuture<Error>* e);

// Helper function to set the enabled state of devices synchronously.
void SetEnabledSync(Device* device, bool enable, bool persist, Error* error);

template <typename CallbackType>
class CallbackValue {};

template <typename F>
class CallbackValue<base::OnceCallback<F>> {
 public:
  using Type = base::OnceCallback<F>;
};

template <typename F>
class CallbackValue<base::RepeatingCallback<F>> {
 public:
  using Type = const base::RepeatingCallback<F>&;
};

template <typename CallbackType>
void ReturnOperationFailed(typename CallbackValue<CallbackType>::Type callback);

}  // namespace shill

#endif  // SHILL_TESTING_H_
