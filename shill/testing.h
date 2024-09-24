// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_TESTING_H_
#define SHILL_TESTING_H_

#include <string_view>

#include <base/files/file_path.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

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

// Returns the filepath for |filename| in the OUT directory. `getenv("OUT")`
// will be used if it exists (which will be the case if the unit test is invoked
// by `FEATURES="test" emerge`). Otherwise assumes that the current executable
// (which should be `shill_unittest`) is located in the OUT directory and
// constructs the path from it.
base::FilePath GetFilePathForTest(std::string_view filename);

}  // namespace shill

#endif  // SHILL_TESTING_H_
