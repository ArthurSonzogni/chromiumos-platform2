// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/tpm2_status_impl.h"

#include <memory>

#include <base/bind.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <trunks/mock_tpm_state.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/tpm_constants.h>
#include <trunks/trunks_factory_for_test.h>

#include "tpm_manager/common/typedefs.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using trunks::TPM_RC_FAILURE;
using trunks::TPM_RC_SUCCESS;

namespace tpm_manager {

class Tpm2StatusTest : public testing::Test {
 public:
  Tpm2StatusTest() = default;
  ~Tpm2StatusTest() override = default;

  void SetUp() override {
    factory_.set_tpm_state(&mock_tpm_state_);
    factory_.set_tpm_utility(&mock_tpm_utility_);
    tpm_status_.reset(new Tpm2StatusImpl(factory_));
  }

 protected:
  NiceMock<trunks::MockTpmState> mock_tpm_state_;
  NiceMock<trunks::MockTpmUtility> mock_tpm_utility_;
  trunks::TrunksFactoryForTest factory_;
  std::unique_ptr<TpmStatus> tpm_status_;
};

TEST_F(Tpm2StatusTest, IsEnabledAlwaysSuccess) {
  EXPECT_CALL(mock_tpm_state_, Initialize()).Times(0);
  EXPECT_TRUE(tpm_status_->IsTpmEnabled());
}

TEST_F(Tpm2StatusTest, IsOwnedSuccess) {
  EXPECT_CALL(mock_tpm_state_, Initialize())
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_state_, IsOwned()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_tpm_state_, IsOwnerPasswordSet())
      .WillRepeatedly(Return(true));

  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(trunks::kStorageRootKey, _))
      .WillRepeatedly(
          Invoke([](trunks::TPM_HANDLE, trunks::TPMT_PUBLIC* public_area) {
            memset(public_area, 0, sizeof(trunks::TPMT_PUBLIC));
            public_area->object_attributes =
                trunks::kSensitiveDataOrigin | trunks::kUserWithAuth |
                trunks::kNoDA | trunks::kRestricted | trunks::kDecrypt;
            return TPM_RC_SUCCESS;
          }));

  TpmStatus::TpmOwnershipStatus status;
  EXPECT_TRUE(tpm_status_->GetTpmOwned(&status));
  EXPECT_EQ(TpmStatus::kTpmOwned, status);
}

TEST_F(Tpm2StatusTest, IsOwnedWrongAttributes) {
  EXPECT_CALL(mock_tpm_state_, Initialize())
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_state_, IsOwned()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_tpm_state_, IsOwnerPasswordSet())
      .WillRepeatedly(Return(true));

  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(trunks::kStorageRootKey, _))
      .WillRepeatedly(
          Invoke([](trunks::TPM_HANDLE, trunks::TPMT_PUBLIC* public_area) {
            memset(public_area, 0, sizeof(trunks::TPMT_PUBLIC));
            return TPM_RC_SUCCESS;
          }));
  TpmStatus::TpmOwnershipStatus status;
  EXPECT_TRUE(tpm_status_->GetTpmOwned(&status));
  EXPECT_EQ(TpmStatus::kTpmPreOwned, status);
}

TEST_F(Tpm2StatusTest, IsOwnedNoSrk) {
  EXPECT_CALL(mock_tpm_state_, Initialize())
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_state_, IsOwned()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_tpm_state_, IsOwnerPasswordSet())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(trunks::kStorageRootKey, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));

  TpmStatus::TpmOwnershipStatus status;
  EXPECT_TRUE(tpm_status_->GetTpmOwned(&status));
  EXPECT_EQ(TpmStatus::kTpmPreOwned, status);
}

TEST_F(Tpm2StatusTest, IsOwnedFailure) {
  EXPECT_CALL(mock_tpm_state_, IsOwned()).WillRepeatedly(Return(false));
  EXPECT_CALL(mock_tpm_state_, IsOwnerPasswordSet())
      .WillRepeatedly(Return(false));

  TpmStatus::TpmOwnershipStatus status;
  EXPECT_TRUE(tpm_status_->GetTpmOwned(&status));
  EXPECT_EQ(TpmStatus::kTpmUnowned, status);
}

TEST_F(Tpm2StatusTest, IsOwnedRepeatedInitializationOnFalse) {
  EXPECT_CALL(mock_tpm_state_, Initialize())
      .Times(2)
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_state_, IsOwned()).WillOnce(Return(false));
  EXPECT_CALL(mock_tpm_state_, IsOwnerPasswordSet())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(trunks::kStorageRootKey, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));

  TpmStatus::TpmOwnershipStatus status;
  EXPECT_TRUE(tpm_status_->GetTpmOwned(&status));
  EXPECT_EQ(TpmStatus::kTpmUnowned, status);

  EXPECT_CALL(mock_tpm_state_, IsOwned()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(trunks::kStorageRootKey, _))
      .WillRepeatedly(
          Invoke([](trunks::TPM_HANDLE, trunks::TPMT_PUBLIC* public_area) {
            memset(public_area, 0, sizeof(trunks::TPMT_PUBLIC));
            public_area->object_attributes =
                trunks::kSensitiveDataOrigin | trunks::kUserWithAuth |
                trunks::kNoDA | trunks::kRestricted | trunks::kDecrypt;
            return TPM_RC_SUCCESS;
          }));

  EXPECT_TRUE(tpm_status_->GetTpmOwned(&status));
  EXPECT_EQ(TpmStatus::kTpmOwned, status);
}

TEST_F(Tpm2StatusTest, IsOwnedNoRepeatedInitializationOnTrue) {
  EXPECT_CALL(mock_tpm_state_, Initialize()).WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_state_, IsOwned()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_tpm_state_, IsOwnerPasswordSet())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(trunks::kStorageRootKey, _))
      .WillRepeatedly(
          Invoke([](trunks::TPM_HANDLE, trunks::TPMT_PUBLIC* public_area) {
            memset(public_area, 0, sizeof(trunks::TPMT_PUBLIC));
            public_area->object_attributes =
                trunks::kSensitiveDataOrigin | trunks::kUserWithAuth |
                trunks::kNoDA | trunks::kRestricted | trunks::kDecrypt;
            return TPM_RC_SUCCESS;
          }));

  TpmStatus::TpmOwnershipStatus status;
  EXPECT_TRUE(tpm_status_->GetTpmOwned(&status));
  EXPECT_EQ(TpmStatus::kTpmOwned, status);
  EXPECT_TRUE(tpm_status_->GetTpmOwned(&status));
  EXPECT_EQ(TpmStatus::kTpmOwned, status);
}

TEST_F(Tpm2StatusTest, IsOwnedInitializeFailure) {
  EXPECT_CALL(mock_tpm_state_, Initialize())
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  EXPECT_CALL(mock_tpm_state_, IsOwned()).Times(0);
  EXPECT_CALL(mock_tpm_state_, IsOwnerPasswordSet()).Times(0);

  TpmStatus::TpmOwnershipStatus status;
  EXPECT_FALSE(tpm_status_->GetTpmOwned(&status));
}

TEST_F(Tpm2StatusTest, IsPreOwned) {
  EXPECT_CALL(mock_tpm_state_, Initialize())
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_state_, IsOwned()).WillRepeatedly(Return(false));
  EXPECT_CALL(mock_tpm_state_, IsOwnerPasswordSet())
      .WillRepeatedly(Return(true));

  TpmStatus::TpmOwnershipStatus status;
  EXPECT_TRUE(tpm_status_->GetTpmOwned(&status));
  EXPECT_EQ(TpmStatus::kTpmPreOwned, status);
}

TEST_F(Tpm2StatusTest, GetDictionaryAttackInfoInitializeFailure) {
  EXPECT_CALL(mock_tpm_state_, Initialize())
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  uint32_t count;
  uint32_t threshold;
  bool lockout;
  uint32_t seconds_remaining;
  EXPECT_FALSE(tpm_status_->GetDictionaryAttackInfo(
      &count, &threshold, &lockout, &seconds_remaining));
}

TEST_F(Tpm2StatusTest, GetDictionaryAttackInfoForwarding) {
  uint32_t lockout_count = 3;
  uint32_t lockout_threshold = 16;
  bool is_locked = true;
  uint32_t lockout_interval = 3600;
  EXPECT_CALL(mock_tpm_state_, GetLockoutCounter())
      .WillRepeatedly(Return(lockout_count));
  EXPECT_CALL(mock_tpm_state_, GetLockoutThreshold())
      .WillRepeatedly(Return(lockout_threshold));
  EXPECT_CALL(mock_tpm_state_, IsInLockout()).WillRepeatedly(Return(is_locked));
  EXPECT_CALL(mock_tpm_state_, GetLockoutInterval())
      .WillRepeatedly(Return(lockout_interval));
  uint32_t count;
  uint32_t threshold;
  bool lockout;
  uint32_t seconds_remaining;
  EXPECT_TRUE(tpm_status_->GetDictionaryAttackInfo(&count, &threshold, &lockout,
                                                   &seconds_remaining));
  EXPECT_EQ(count, lockout_count);
  EXPECT_EQ(threshold, lockout_threshold);
  EXPECT_EQ(lockout, is_locked);
  EXPECT_EQ(seconds_remaining, lockout_count * lockout_interval);
}

TEST_F(Tpm2StatusTest, GetDictionaryAttackInfoAlwaysRefresh) {
  EXPECT_CALL(mock_tpm_state_, Initialize())
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  uint32_t count;
  uint32_t threshold;
  bool lockout;
  uint32_t seconds_remaining;
  EXPECT_TRUE(tpm_status_->GetDictionaryAttackInfo(&count, &threshold, &lockout,
                                                   &seconds_remaining));
}

TEST_F(Tpm2StatusTest, IsDictionaryAttackMitigationEnabledInitializeFailure) {
  EXPECT_CALL(mock_tpm_state_, Initialize())
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  bool is_enabled;
  EXPECT_FALSE(tpm_status_->IsDictionaryAttackMitigationEnabled(&is_enabled));
}

TEST_F(Tpm2StatusTest, IsDictionaryAttackMitigationEnabledSuccess) {
  EXPECT_CALL(mock_tpm_state_, Initialize())
      .WillRepeatedly(Return(TPM_RC_SUCCESS));

  // Either lockout interval or lockout recovery indicates a positive test.
  EXPECT_CALL(mock_tpm_state_, GetLockoutInterval())
      .WillRepeatedly(Return(2000));
  EXPECT_CALL(mock_tpm_state_, GetLockoutRecovery()).WillRepeatedly(Return(0));
  bool is_enabled = false;
  EXPECT_TRUE(tpm_status_->IsDictionaryAttackMitigationEnabled(&is_enabled));
  EXPECT_TRUE(is_enabled);

  EXPECT_CALL(mock_tpm_state_, GetLockoutInterval()).WillRepeatedly(Return(0));
  EXPECT_CALL(mock_tpm_state_, GetLockoutRecovery())
      .WillRepeatedly(Return(2000));
  is_enabled = false;
  EXPECT_TRUE(tpm_status_->IsDictionaryAttackMitigationEnabled(&is_enabled));
  EXPECT_TRUE(is_enabled);

  // Otherwise both values being 0 indicates a negative test.
  EXPECT_CALL(mock_tpm_state_, GetLockoutInterval()).WillRepeatedly(Return(0));
  EXPECT_CALL(mock_tpm_state_, GetLockoutRecovery()).WillRepeatedly(Return(0));
  is_enabled = true;
  EXPECT_TRUE(tpm_status_->IsDictionaryAttackMitigationEnabled(&is_enabled));
  EXPECT_FALSE(is_enabled);
}

TEST_F(Tpm2StatusTest, SupportU2f) {
  EXPECT_CALL(mock_tpm_utility_, IsCr50).WillRepeatedly(Return(true));

  EXPECT_TRUE(tpm_status_->SupportU2f());
}

TEST_F(Tpm2StatusTest, NotSupportU2f) {
  EXPECT_CALL(mock_tpm_utility_, IsCr50).WillRepeatedly(Return(false));

  EXPECT_FALSE(tpm_status_->SupportU2f());
}

TEST_F(Tpm2StatusTest, SupportPinweaver) {
  EXPECT_CALL(mock_tpm_utility_, PinWeaverIsSupported(0, _))
      .WillRepeatedly(Return(TPM_RC_SUCCESS));

  EXPECT_TRUE(tpm_status_->SupportPinweaver());
}

TEST_F(Tpm2StatusTest, NotSupportPinweaver) {
  EXPECT_CALL(mock_tpm_utility_, PinWeaverIsSupported(0, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));

  EXPECT_FALSE(tpm_status_->SupportPinweaver());
}

TEST_F(Tpm2StatusTest, GetGscVersionCr50) {
  EXPECT_CALL(mock_tpm_utility_, IsCr50).WillRepeatedly(Return(true));

  EXPECT_EQ(tpm_status_->GetGscVersion(), GscVersion::GSC_VERSION_CR50);
}

TEST_F(Tpm2StatusTest, GetGscVersionNotGsc) {
  EXPECT_CALL(mock_tpm_utility_, IsCr50).WillRepeatedly(Return(false));

  EXPECT_EQ(tpm_status_->GetGscVersion(), GscVersion::GSC_VERSION_NOT_GSC);
}

}  // namespace tpm_manager
