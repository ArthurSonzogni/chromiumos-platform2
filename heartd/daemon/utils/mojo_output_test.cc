// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/utils/mojo_output.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace heartd {

namespace {

namespace mojom = ::ash::heartd::mojom;

class MojoOutputTest : public testing::Test {
 public:
  MojoOutputTest() {}
  ~MojoOutputTest() override = default;
};

TEST_F(MojoOutputTest, ServiceNameToStr) {
  auto name = mojom::ServiceName::kKiosk;
  switch (name) {
    case mojom::ServiceName::kKiosk:
      EXPECT_EQ(ToStr(mojom::ServiceName::kKiosk), "kKiosk");
      [[fallthrough]];
    case mojom::ServiceName::kUnmappedEnumField:
      EXPECT_EQ(ToStr(mojom::ServiceName::kUnmappedEnumField),
                "kUnmappedEnumField");
      break;
  }
}

TEST_F(MojoOutputTest, ActionTypeToStr) {
  auto action = mojom::ActionType::kNoOperation;
  switch (action) {
    case mojom::ActionType::kNoOperation:
      EXPECT_EQ(ToStr(mojom::ActionType::kNoOperation), "kNoOperation");
      [[fallthrough]];
    case mojom::ActionType::kNormalReboot:
      EXPECT_EQ(ToStr(mojom::ActionType::kNormalReboot), "kNormalReboot");
      [[fallthrough]];
    case mojom::ActionType::kForceReboot:
      EXPECT_EQ(ToStr(mojom::ActionType::kForceReboot), "kForceReboot");
      [[fallthrough]];
    case mojom::ActionType::kUnmappedEnumField:
      EXPECT_EQ(ToStr(mojom::ActionType::kUnmappedEnumField),
                "kUnmappedEnumField");
      return;
  }
}

}  // namespace

}  // namespace heartd