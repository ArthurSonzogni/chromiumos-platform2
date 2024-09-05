// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/routines/urandom_delegate.h"

#include <string>

#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/delegate/constants.h"

namespace diagnostics {
namespace {

class UrandomDelegateTest : public BaseFileTest {};

TEST_F(UrandomDelegateTest, CreationFailed) {
  UnsetPath(path::kUrandomPath);
  EXPECT_FALSE(UrandomDelegate::Create());
}

// Task failed due to read byte less than expected.
TEST_F(UrandomDelegateTest, RunFailed) {
  SetFile(path::kUrandomPath, "");

  std::unique_ptr<UrandomDelegate> delegate = UrandomDelegate::Create();
  ASSERT_TRUE(delegate);
  EXPECT_FALSE(delegate->Run());
}

TEST_F(UrandomDelegateTest, RunSucceeded) {
  std::string urandom_data(UrandomDelegate::kNumBytesRead, ' ');
  SetFile(path::kUrandomPath, urandom_data);

  std::unique_ptr<UrandomDelegate> delegate = UrandomDelegate::Create();
  ASSERT_TRUE(delegate);
  EXPECT_TRUE(delegate->Run());
}

}  // namespace
}  // namespace diagnostics
