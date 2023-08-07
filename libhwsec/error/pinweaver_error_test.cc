// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/error/pinweaver_error.h"

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "libhwsec/error/tpm_error.h"
#include "libhwsec/status.h"

namespace hwsec {

using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::StatusChain;

class TestingPinWeaverErrorTest : public ::testing::Test {
 public:
  using PinWeaverErrorCode = PinWeaver::CredentialTreeResult::ErrorCode;

  TestingPinWeaverErrorTest() = default;
  ~TestingPinWeaverErrorTest() override = default;
};

TEST_F(TestingPinWeaverErrorTest, MakeStatus) {
  Status status =
      MakeStatus<PinWeaverError>(PinWeaverErrorCode::kHashTreeOutOfSync);
  EXPECT_THAT(status, NotOk());
}

TEST_F(TestingPinWeaverErrorTest, TPMRetryAction) {
  constexpr PinWeaverErrorCode kTestPWError1 =
      PinWeaverErrorCode::kHashTreeOutOfSync;
  Status status = MakeStatus<PinWeaverError>(kTestPWError1);
  EXPECT_EQ(status.HintNotOk()->ToTPMRetryAction(),
            TPMRetryAction::kPinWeaverOutOfSync);
  EXPECT_EQ(status->UnifiedErrorCode(),
            static_cast<unified_tpm_error::UnifiedError>(kTestPWError1) +
                unified_tpm_error::kUnifiedErrorPinWeaverBase);

  Status status2 = MakeStatus<TPMError>("OuO+").Wrap(std::move(status));
  EXPECT_EQ("OuO+: Pinweaver Manager Error Code 4 (kHashTreeOutOfSync)",
            status2.ToFullString());
  EXPECT_EQ(status2->ToTPMRetryAction(), TPMRetryAction::kPinWeaverOutOfSync);
}

}  // namespace hwsec
