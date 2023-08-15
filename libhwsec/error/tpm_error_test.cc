// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/error/tpm_error.h"
#include "libhwsec/error/tpm_retry_handler.h"
#include "libhwsec-foundation/error/testing_helper.h"

#include <sstream>
#include <type_traits>

#include <base/test/task_environment.h>
#include <base/test/bind.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::StatusChain;

namespace hwsec {

class TestingTPMErrorTest : public ::testing::Test {
 public:
  TestingTPMErrorTest() {}
  ~TestingTPMErrorTest() override = default;
};

TEST_F(TestingTPMErrorTest, TPMRetryAction) {
  StatusChain<TPMError> status =
      MakeStatus<TPMError>("OuOb", TPMRetryAction::kReboot);
  ASSERT_NOT_OK(status);
  EXPECT_EQ(status->ToTPMRetryAction(), TPMRetryAction::kReboot);
  StatusChain<TPMError> status2 =
      MakeStatus<TPMError>("OuQ", status->ToTPMRetryAction())
          .Wrap(std::move(status));
  EXPECT_EQ("OuQ: OuOb", status2.ToFullString());
  EXPECT_EQ(status2->ToTPMRetryAction(), TPMRetryAction::kReboot);
}

}  // namespace hwsec
