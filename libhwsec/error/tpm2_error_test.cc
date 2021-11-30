// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/error/tpm2_error.h"
#include "libhwsec-foundation/error/testing_helper.h"

#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace hwsec {

using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;

class TestingTPM2ErrorTest : public ::testing::Test {
 public:
  TestingTPM2ErrorTest() {}
  ~TestingTPM2ErrorTest() override = default;
};

TEST_F(TestingTPM2ErrorTest, MakeStatus) {
  StatusChain<TPM2Error> status = MakeStatus<TPM2Error>(trunks::TPM_RC_SUCCESS);
  EXPECT_THAT(status, IsOk());

  status = MakeStatus<TPM2Error>(trunks::TPM_RC_HANDLE | trunks::TPM_RC_1);
  EXPECT_THAT(status, NotOk());
}

TEST_F(TestingTPM2ErrorTest, TPMRetryAction) {
  StatusChain<TPMErrorBase> status =
      MakeStatus<TPM2Error>(trunks::TPM_RC_HANDLE | trunks::TPM_RC_1);
  EXPECT_EQ(status->ToTPMRetryAction(), TPMRetryAction::kLater);

  StatusChain<TPMError> status2 =
      MakeStatus<TPMError>("OuO|||").Wrap(std::move(status));
  EXPECT_EQ("OuO|||: TPM2 error 0x18b (Handle 1: TPM_RC_HANDLE)",
            status2.ToFullString());
  EXPECT_EQ(status2->ToTPMRetryAction(), TPMRetryAction::kLater);
}

}  // namespace hwsec
