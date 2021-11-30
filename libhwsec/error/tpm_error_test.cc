// Copyright 2021 The Chromium OS Authors. All rights reserved.
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

namespace hwsec {

class TestingTPMErrorTest : public ::testing::Test {
 public:
  TestingTPMErrorTest() {}
  ~TestingTPMErrorTest() override = default;
};

TEST_F(TestingTPMErrorTest, TPMRetryAction) {
  StatusChain<TPMError> status =
      MakeStatus<TPMError>("OuOb", TPMRetryAction::kReboot);
  EXPECT_EQ(status->ToTPMRetryAction(), TPMRetryAction::kReboot);
  StatusChain<TPMError> status2 =
      MakeStatus<TPMError>("OuQ", status->ToTPMRetryAction())
          .Wrap(std::move(status));
  EXPECT_EQ("OuQ: OuOb", status2.ToFullString());
  EXPECT_EQ(status2->ToTPMRetryAction(), TPMRetryAction::kReboot);
}

TEST_F(TestingTPMErrorTest, TPMRetryHandler) {
  StatusChain<TPMErrorBase> status = HANDLE_TPM_COMM_ERROR(
      MakeStatus<TPMError>("OuOb", TPMRetryAction::kReboot));
  EXPECT_EQ("OuOb", status.ToFullString());
  EXPECT_EQ(TPMRetryAction::kReboot, status->ToTPMRetryAction());

  int counter = 0;
  auto func = base::BindLambdaForTesting([&counter]() {
    counter++;
    return MakeStatus<TPMError>("OwO", TPMRetryAction::kCommunication);
  });

  StatusChain<TPMErrorBase> status2 = HANDLE_TPM_COMM_ERROR(func.Run());
  EXPECT_EQ("Retry Failed: OwO", status2.ToFullString());
  EXPECT_EQ(TPMRetryAction::kLater, status2->ToTPMRetryAction());
  EXPECT_EQ(counter, 5);
}

}  // namespace hwsec
