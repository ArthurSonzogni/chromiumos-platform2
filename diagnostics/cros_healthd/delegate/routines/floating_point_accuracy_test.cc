// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/routines/floating_point_accuracy.h"

#include <gtest/gtest.h>

namespace diagnostics {
namespace {

TEST(FloatingPointAccuracyDelegateTest, RunSuccessfully) {
  FloatingPointAccuracyDelegate delegate;
  EXPECT_TRUE(delegate.Run());
}

}  // namespace
}  // namespace diagnostics
