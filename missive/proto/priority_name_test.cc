// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/proto/record_constants.pb.h"

// Temporary replacement for `Priority_Name` that does
// not work in certain CQ.
// TODO(b/294756107): Remove this function once fixed.
#include "missive/proto/priority_name.h"

namespace reporting {
namespace {

// `Priority_Name` is temporarily replaced.
// TODO(b/294756107): Remove this function once fixed and make sure the test
// passes.
TEST(PriorityNameTest, PriorityNameTest) {
  for (int priority = Priority_MIN; priority <= Priority_MAX; ++priority) {
    LOG(ERROR) << "priority=" << priority;
    ASSERT_TRUE(Priority_IsValid(priority));
    const auto name = Priority_Name_Substitute(priority);
    LOG(ERROR) << "priority=" << priority << " name='" << name << "'";
    EXPECT_THAT(name, ::testing::Not(::testing::IsEmpty()));
  }
}
}  // namespace
}  // namespace reporting
