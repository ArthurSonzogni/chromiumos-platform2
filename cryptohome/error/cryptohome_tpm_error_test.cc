// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <set>
#include <utility>

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gtest/gtest.h>
#include <libhwsec/error/tpm_error.h>
#include <libhwsec/error/tpm_retry_handler.h>
#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/error/action.h"
#include "cryptohome/error/cryptohome_tpm_error.h"

namespace cryptohome {

namespace error {

class CryptohomeTPMErrorTest : public ::testing::Test {};

namespace {

using hwsec::TPMError;
using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::StatusChain;

constexpr int kTestLocation1 = 1001;

TEST_F(CryptohomeTPMErrorTest, BasicConstruction) {
  auto err1 = MakeStatus<CryptohomeTPMError>(
      kTestLocation1, ErrorActionSet({ErrorAction::kResumePreviousMigration}),
      hwsec::TPMRetryAction::kNoRetry);

  ASSERT_FALSE(err1.ok());
  EXPECT_EQ(err1->local_location(), kTestLocation1);
  EXPECT_EQ(err1->local_actions(),
            ErrorActionSet({ErrorAction::kResumePreviousMigration,
                            ErrorAction::kDevCheckUnexpectedState}));
  EXPECT_EQ(err1->ToTPMRetryAction(), hwsec::TPMRetryAction::kNoRetry);
}

TEST_F(CryptohomeTPMErrorTest, Success) {
  StatusChain<CryptohomeTPMError> err;
  EXPECT_TRUE(err.ok());
}

TEST_F(CryptohomeTPMErrorTest, FromTPMError) {
  StatusChain<TPMErrorBase> tpm_err =
      MakeStatus<TPMError>("QAQ", hwsec::TPMRetryAction::kReboot);

  auto err1 = MakeStatus<CryptohomeTPMError>(std::move(tpm_err));
  ASSERT_FALSE(err1.ok());
  // 0x0132 is precalculated test vector for "QAQ".
  EXPECT_EQ(
      err1->local_location(),
      (0x0132 + hwsec::unified_tpm_error::kUnifiedErrorHashedTpmErrorBase) |
          hwsec::unified_tpm_error::kUnifiedErrorBit);
  EXPECT_EQ(err1->local_actions(), ErrorActionSet({ErrorAction::kReboot}));
  EXPECT_EQ(err1->ToTPMRetryAction(), hwsec::TPMRetryAction::kReboot);
}

TEST_F(CryptohomeTPMErrorTest, FromTPMErrorStacked) {
  StatusChain<TPMErrorBase> status1 =
      MakeStatus<TPMError>("QAQ", hwsec::TPMRetryAction::kReboot);
  StatusChain<TPMErrorBase> status2 =
      MakeStatus<TPMError>("QwQ", hwsec::TPMRetryAction::kLater)
          .Wrap(std::move(status1));

  auto err1 = MakeStatus<CryptohomeTPMError>(std::move(status2));
  ASSERT_FALSE(err1.ok());
  // The location should still be the one for "QAQ", because it's the last in
  // the chain.
  EXPECT_EQ(
      err1->local_location(),
      (0x0132 + hwsec::unified_tpm_error::kUnifiedErrorHashedTpmErrorBase) |
          hwsec::unified_tpm_error::kUnifiedErrorBit);
  // Retry actions should be from the last in the chain.
  EXPECT_EQ(err1->local_actions(), ErrorActionSet({ErrorAction::kReboot}));
  EXPECT_EQ(err1->ToTPMRetryAction(), hwsec::TPMRetryAction::kReboot);
}

}  // namespace

}  // namespace error

}  // namespace cryptohome
