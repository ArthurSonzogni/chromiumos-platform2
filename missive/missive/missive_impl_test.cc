// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/missive/missive_service.h"

#include <memory>

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace reporting {

class MissiveImplTest : public ::testing::Test {
 public:
  MissiveImplTest() = default;

 protected:
  std::unique_ptr<MissiveService> missive_;
  base::test::TaskEnvironment task_environment_;
};

TEST_F(MissiveImplTest, DummyTest) {}

}  // namespace reporting
