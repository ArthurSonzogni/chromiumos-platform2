// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "libhwsec/backend/tpm1/backend_test_base.h"

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

class BackendStateTpm1Test : public BackendTpm1TestBase {};

TEST_F(BackendStateTpm1Test, IsEnabled) {
  tpm_manager::GetTpmNonsensitiveStatusReply reply;
  reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
  reply.set_is_enabled(true);
  EXPECT_CALL(proxy_->GetMock().tpm_manager,
              GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto result = middleware_->CallSync<&Backend::State::IsEnabled>();
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(*result);
}

TEST_F(BackendStateTpm1Test, IsReady) {
  tpm_manager::GetTpmNonsensitiveStatusReply reply;
  reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
  reply.set_is_owned(true);
  EXPECT_CALL(proxy_->GetMock().tpm_manager,
              GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto result = middleware_->CallSync<&Backend::State::IsReady>();
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(*result);
}

TEST_F(BackendStateTpm1Test, Prepare) {
  tpm_manager::TakeOwnershipReply reply;
  reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
  EXPECT_CALL(proxy_->GetMock().tpm_manager, TakeOwnership(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto result = middleware_->CallSync<&Backend::State::Prepare>();
  ASSERT_TRUE(result.ok());
}

}  // namespace hwsec
