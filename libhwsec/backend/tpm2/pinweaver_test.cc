// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "libhwsec/backend/tpm2/backend_test_base.h"

using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;
using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using tpm_manager::TpmManagerStatus;
namespace hwsec {

class BackendPinweaverTpm2Test : public BackendTpm2TestBase {};

TEST_F(BackendPinweaverTpm2Test, IsEnabled) {
  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::IsEnabled>();

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(*result);
}

TEST_F(BackendPinweaverTpm2Test, IsEnabledMismatch) {
  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(Return(trunks::SAPI_RC_ABI_MISMATCH))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::IsEnabled>();

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(*result);
}

TEST_F(BackendPinweaverTpm2Test, IsDisabled) {
  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(Return(trunks::TPM_RC_FAILURE));

  auto result = middleware_->CallSync<&Backend::PinWeaver::IsEnabled>();

  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(*result);
}

TEST_F(BackendPinweaverTpm2Test, IsDisabledMismatch) {
  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(Return(trunks::SAPI_RC_ABI_MISMATCH))
      .WillOnce(Return(trunks::SAPI_RC_ABI_MISMATCH));

  auto result = middleware_->CallSync<&Backend::PinWeaver::IsEnabled>();

  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(*result);
}

}  // namespace hwsec
