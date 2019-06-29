//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "tpm_manager/server/tpm2_initializer_impl.h"

#include <memory>

#include <base/strings/string_number_conversions.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/trunks_factory_for_test.h>

#include "tpm_manager/server/dbus_service.h"
#include "tpm_manager/server/mock_local_data_store.h"
#include "tpm_manager/server/mock_openssl_crypto_util.h"
#include "tpm_manager/server/mock_tpm_status.h"
#include "tpm_manager/server/tpm_status.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;

namespace tpm_manager {

class Tpm2InitializerTest : public testing::Test {
 public:
  Tpm2InitializerTest() = default;
  ~Tpm2InitializerTest() override = default;

  void SetUp() override {
    EXPECT_CALL(mock_data_store_, Read(_))
        .WillRepeatedly(Invoke([this](LocalData* arg) {
          *arg = fake_local_data_;
          return true;
        }));
    EXPECT_CALL(mock_data_store_, Write(_))
        .WillRepeatedly(Invoke([this](const LocalData& arg) {
          fake_local_data_ = arg;
          return true;
        }));
    factory_.set_tpm_utility(&mock_tpm_utility_);

    ownership_callback_ = base::Bind(
        &Tpm2InitializerTest::OwnershipCallback, base::Unretained(this));
    tpm_initializer_.reset(new Tpm2InitializerImpl(
        factory_, &mock_openssl_util_, &mock_data_store_, &mock_tpm_status_,
        ownership_callback_));
  }

 protected:
  // Mock ownership taken callback.
  void OwnershipCallback() { ++ownership_call_count_; }

  LocalData fake_local_data_;
  NiceMock<MockOpensslCryptoUtil> mock_openssl_util_;
  NiceMock<MockLocalDataStore> mock_data_store_;
  NiceMock<MockTpmStatus> mock_tpm_status_;
  NiceMock<trunks::MockTpmUtility> mock_tpm_utility_;
  trunks::TrunksFactoryForTest factory_;
  std::unique_ptr<TpmInitializer> tpm_initializer_;

  OwnershipTakenCallBack ownership_callback_;
  int ownership_call_count_ = 0;
};

TEST_F(Tpm2InitializerTest, InitializeTpmNoSeedTpm) {
  EXPECT_CALL(mock_tpm_utility_, StirRandom(_, _))
      .WillRepeatedly(Return(trunks::TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_initializer_->InitializeTpm());
}

TEST_F(Tpm2InitializerTest, InitializeTpmAlreadyOwned) {
  EXPECT_CALL(mock_tpm_utility_, TakeOwnership(_, _, _)).Times(0);
  EXPECT_TRUE(tpm_initializer_->InitializeTpm());
}

TEST_F(Tpm2InitializerTest, InitializeTpmOwnershipStatusError) {
  EXPECT_CALL(mock_tpm_status_, CheckAndNotifyIfTpmOwned(_))
      .WillOnce(Return(false));
  EXPECT_CALL(mock_data_store_, Read(_)).Times(0);
  EXPECT_CALL(mock_tpm_utility_, TakeOwnership(_, _, _)).Times(0);
  EXPECT_FALSE(tpm_initializer_->InitializeTpm());
}

TEST_F(Tpm2InitializerTest, InitializeTpmLocalDataReadError) {
  EXPECT_CALL(mock_tpm_status_, CheckAndNotifyIfTpmOwned(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(TpmStatus::kTpmUnowned), Return(true)));
  EXPECT_CALL(mock_data_store_, Read(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(mock_tpm_utility_, TakeOwnership(_, _, _)).Times(0);
  EXPECT_FALSE(tpm_initializer_->InitializeTpm());
}

TEST_F(Tpm2InitializerTest, InitializeTpmLocalDataWriteError) {
  EXPECT_CALL(mock_tpm_status_, CheckAndNotifyIfTpmOwned(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(TpmStatus::kTpmUnowned), Return(true)));
  EXPECT_CALL(mock_data_store_, Write(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(mock_tpm_utility_, TakeOwnership(_, _, _)).Times(0);
  EXPECT_FALSE(tpm_initializer_->InitializeTpm());
  EXPECT_EQ(ownership_call_count_, 0);
}

TEST_F(Tpm2InitializerTest, InitializeTpmOwnershipError) {
  EXPECT_CALL(mock_tpm_status_, CheckAndNotifyIfTpmOwned(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(TpmStatus::kTpmUnowned), Return(true)));
  EXPECT_CALL(mock_tpm_utility_, TakeOwnership(_, _, _))
      .WillRepeatedly(Return(trunks::TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_initializer_->InitializeTpm());
  EXPECT_EQ(ownership_call_count_, 0);
}

TEST_F(Tpm2InitializerTest, InitializeTpmSuccess) {
  EXPECT_CALL(mock_tpm_status_, CheckAndNotifyIfTpmOwned(_))
      .WillOnce(DoAll(SetArgPointee<0>(TpmStatus::kTpmUnowned), Return(true)));
  std::string owner_random_bytes("\xFF\xF7\x00\x01\xD2\xA3", 6);
  std::string owner_password =
      base::HexEncode(owner_random_bytes.data(), owner_random_bytes.size());
  EXPECT_EQ(owner_random_bytes.size() * 2, owner_password.size());
  std::string endorsement_password = "hunter2";
  std::string lockout_password = "sesame";
  EXPECT_CALL(mock_tpm_utility_, GenerateRandom(_, _, _))
      .Times(3)  // Once for owner, endorsement and lockout passwords
      .WillOnce(DoAll(SetArgPointee<2>(owner_random_bytes),
                      Return(trunks::TPM_RC_SUCCESS)))
      .WillOnce(DoAll(SetArgPointee<2>(endorsement_password),
                      Return(trunks::TPM_RC_SUCCESS)))
      .WillOnce(DoAll(SetArgPointee<2>(lockout_password),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_, TakeOwnership(_, _, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_TRUE(tpm_initializer_->InitializeTpm());
  EXPECT_LT(0, fake_local_data_.owner_dependency_size());
  EXPECT_EQ(owner_password, fake_local_data_.owner_password());
  EXPECT_EQ(endorsement_password, fake_local_data_.endorsement_password());
  EXPECT_EQ(lockout_password, fake_local_data_.lockout_password());
  EXPECT_EQ(ownership_call_count_, 1);
}

TEST_F(Tpm2InitializerTest, InitializeTpmSuccessAfterError) {
  EXPECT_CALL(mock_tpm_status_, CheckAndNotifyIfTpmOwned(_))
      .WillOnce(DoAll(SetArgPointee<0>(TpmStatus::kTpmUnowned), Return(true)));
  std::string owner_password("owner");
  std::string endorsement_password("endorsement");
  std::string lockout_password("lockout");
  fake_local_data_.add_owner_dependency("test");
  fake_local_data_.set_owner_password(owner_password);
  fake_local_data_.set_endorsement_password(endorsement_password);
  fake_local_data_.set_lockout_password(lockout_password);
  EXPECT_CALL(
      mock_tpm_utility_,
      TakeOwnership(owner_password, endorsement_password, lockout_password))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_TRUE(tpm_initializer_->InitializeTpm());
  EXPECT_LT(0, fake_local_data_.owner_dependency_size());
  EXPECT_EQ(owner_password, fake_local_data_.owner_password());
  EXPECT_EQ(endorsement_password, fake_local_data_.endorsement_password());
  EXPECT_EQ(lockout_password, fake_local_data_.lockout_password());
  EXPECT_EQ(ownership_call_count_, 1);
}

// TODO(http://crosbug.com/p/59837): restore when TPM_RC_PCR_CHANGED is
// properly handled. Until then, VerifiedBootHelper won't extend PCRs.
#if 0
TEST_F(Tpm2InitializerTest, PCRSpoofGuard) {
  // Setup empty PCRs that need to be extended.
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(std::string(32, 0)),
                            Return(trunks::TPM_RC_SUCCESS)));
  // Expect at least four PCRs to be extended.
  EXPECT_CALL(mock_tpm_utility_, ExtendPCR(_, _, _))
      .Times(AtLeast(4))
      .WillRepeatedly(Return(trunks::TPM_RC_SUCCESS));
  tpm_initializer_->VerifiedBootHelper();
}

TEST_F(Tpm2InitializerTest, PCRSpoofGuardReadFailure) {
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(_, _))
      .WillRepeatedly(Return(trunks::TPM_RC_FAILURE));
  tpm_initializer_->VerifiedBootHelper();
}

TEST_F(Tpm2InitializerTest, PCRSpoofGuardExtendFailure) {
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(std::string(32, 0)),
                            Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_, ExtendPCR(_, _, _))
      .WillRepeatedly(Return(trunks::TPM_RC_FAILURE));
  tpm_initializer_->VerifiedBootHelper();
}
#endif

TEST_F(Tpm2InitializerTest, DAResetSuccess) {
  fake_local_data_.set_lockout_password("lockout");
  EXPECT_CALL(mock_tpm_utility_, ResetDictionaryAttackLock(_))
      .WillRepeatedly(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_TRUE(tpm_initializer_->ResetDictionaryAttackLock());
}

TEST_F(Tpm2InitializerTest, DAResetNoLockoutPassword) {
  fake_local_data_.clear_lockout_password();
  EXPECT_FALSE(tpm_initializer_->ResetDictionaryAttackLock());
}

TEST_F(Tpm2InitializerTest, DAResetFailure) {
  fake_local_data_.set_lockout_password("lockout");
  EXPECT_CALL(mock_tpm_utility_, ResetDictionaryAttackLock(_))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(trunks::TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_initializer_->ResetDictionaryAttackLock());
}

}  // namespace tpm_manager
