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

#include "tpm_manager/server/tpm2_nvram_impl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <trunks/mock_hmac_session.h>
#include <trunks/mock_policy_session.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/tpm_constants.h>
#include <trunks/trunks_factory_for_test.h>

#include "tpm_manager/server/mock_local_data_store.h"

namespace {

constexpr uint32_t kSomeNvramIndex = 42;
constexpr size_t kSomeNvramSize = 20;
constexpr char kTestOwnerPassword[] = "owner";
constexpr char kFakePolicyDigest[] = "fake_policy_digest";
constexpr char kFakePCRValue[] = "fake_pcr_value";
constexpr char kFakeAuthorizationValue[] = "fake_authorization";
trunks::AuthorizationDelegate* const kHMACAuth =
    reinterpret_cast<trunks::AuthorizationDelegate*>(1ull);
trunks::AuthorizationDelegate* const kPolicyAuth =
    reinterpret_cast<trunks::AuthorizationDelegate*>(2ull);
constexpr trunks::TPMA_NV kNoExtraAttributes = 0;

}  // namespace

namespace tpm_manager {

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::DoAll;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using trunks::TPM_RC_SUCCESS;
using trunks::TPM_RC_FAILURE;
using trunks::TPM_RC_HANDLE;

class Tpm2NvramTest : public testing::Test {
 public:
  Tpm2NvramTest() = default;
  virtual ~Tpm2NvramTest() = default;

  void SetUp() {
    factory_.set_hmac_session(&mock_hmac_session_);
    factory_.set_policy_session(&mock_policy_session_);
    factory_.set_trial_session(&mock_trial_session_);
    factory_.set_tpm_utility(&mock_tpm_utility_);
    tpm_nvram_.reset(new Tpm2NvramImpl(factory_, &mock_data_store_));
    ON_CALL(mock_hmac_session_, GetDelegate()).WillByDefault(Return(kHMACAuth));
    ON_CALL(mock_policy_session_, GetDelegate())
        .WillByDefault(Return(kPolicyAuth));
    ON_CALL(mock_policy_session_, GetDigest(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(kFakePolicyDigest), Return(TPM_RC_SUCCESS)));
    ON_CALL(mock_trial_session_, GetDigest(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(kFakePolicyDigest), Return(TPM_RC_SUCCESS)));
  }

  void SetupOwnerPassword() {
    LocalData& local_data = mock_data_store_.GetMutableFakeData();
    local_data.set_owner_password(kTestOwnerPassword);
  }

  enum ExpectAuth { NO_EXPECT_AUTH, EXPECT_AUTH };
  enum AuthType { NORMAL_AUTH, POLICY_AUTH, OWNER_AUTH };
  void SetupExistingSpace(uint32_t index,
                          uint32_t size,
                          trunks::TPMA_NV extra_attributes,
                          ExpectAuth expect_auth,
                          AuthType auth_type) {
    trunks::TPMS_NV_PUBLIC public_data;
    public_data.nv_index = index;
    public_data.data_size = size;
    public_data.attributes = trunks::TPMA_NV_READ_STCLEAR |
                             trunks::TPMA_NV_WRITE_STCLEAR | extra_attributes;
    switch (auth_type) {
      case NORMAL_AUTH:
        public_data.attributes |=
            trunks::TPMA_NV_AUTHREAD | trunks::TPMA_NV_AUTHWRITE;
        break;
      case POLICY_AUTH:
        public_data.attributes |=
            trunks::TPMA_NV_POLICYREAD | trunks::TPMA_NV_POLICYWRITE;
        break;
      case OWNER_AUTH:
        public_data.attributes |=
            trunks::TPMA_NV_OWNERREAD | trunks::TPMA_NV_OWNERWRITE;
        break;
    }
    ON_CALL(mock_tpm_utility_, GetNVSpacePublicArea(index, _))
        .WillByDefault(
            DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
    LocalData& local_data = mock_data_store_.GetMutableFakeData();
    NvramPolicyRecord& policy_record = *local_data.add_nvram_policy();
    policy_record.set_index(index);
    if (auth_type == POLICY_AUTH) {
      policy_record.set_policy(NVRAM_POLICY_PCR0);
    }
    if (!expect_auth) {
      EXPECT_CALL(mock_hmac_session_, SetEntityAuthorizationValue(_)).Times(0);
      EXPECT_CALL(mock_policy_session_, SetEntityAuthorizationValue(_))
          .Times(0);
      EXPECT_CALL(mock_policy_session_, PolicyAuthValue()).Times(0);
    } else if (auth_type == NORMAL_AUTH) {
      EXPECT_CALL(mock_hmac_session_,
                  SetEntityAuthorizationValue(kFakeAuthorizationValue))
          .Times(AtLeast(1));
      EXPECT_CALL(mock_hmac_session_, SetEntityAuthorizationValue("")).Times(0);
    } else if (auth_type == OWNER_AUTH) {
      EXPECT_CALL(mock_hmac_session_,
                  SetEntityAuthorizationValue(kTestOwnerPassword))
          .Times(AtLeast(1));
      EXPECT_CALL(mock_hmac_session_, SetEntityAuthorizationValue("")).Times(0);
    } else {
      EXPECT_CALL(mock_policy_session_,
                  SetEntityAuthorizationValue(kFakeAuthorizationValue))
          .Times(AtLeast(1));
      EXPECT_CALL(mock_hmac_session_, SetEntityAuthorizationValue("")).Times(0);
      EXPECT_CALL(mock_tpm_utility_, ReadPCR(0, _))
          .Times(AtLeast(1))
          .WillRepeatedly(
              DoAll(SetArgPointee<1>(kFakePCRValue), Return(TPM_RC_SUCCESS)));
      EXPECT_CALL(mock_policy_session_, PolicyAuthValue()).Times(AtLeast(1));
      EXPECT_CALL(mock_policy_session_, PolicyPCR(0, kFakePCRValue))
          .Times(AtLeast(1));
    }
  }

 protected:
  const std::string kSomeData{"data"};
  trunks::TrunksFactoryForTest factory_;
  NiceMock<trunks::MockHmacSession> mock_hmac_session_;
  NiceMock<trunks::MockPolicySession> mock_policy_session_;
  NiceMock<trunks::MockPolicySession> mock_trial_session_;
  NiceMock<MockLocalDataStore> mock_data_store_;
  NiceMock<trunks::MockTpmUtility> mock_tpm_utility_;
  std::unique_ptr<Tpm2NvramImpl> tpm_nvram_;
};

TEST_F(Tpm2NvramTest, NoOwnerFailure) {
  EXPECT_EQ(NVRAM_RESULT_OPERATION_DISABLED,
            tpm_nvram_->DefineSpace(0, 0, {}, "", NVRAM_POLICY_NONE));
  EXPECT_EQ(NVRAM_RESULT_OPERATION_DISABLED, tpm_nvram_->DestroySpace(0));
}

TEST_F(Tpm2NvramTest, SessionFailure) {
  EXPECT_CALL(mock_hmac_session_, StartUnboundSession(_))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  EXPECT_NE(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->DefineSpace(0, 0, {}, "", NVRAM_POLICY_NONE));
  EXPECT_NE(NVRAM_RESULT_SUCCESS, tpm_nvram_->DestroySpace(0));
  EXPECT_NE(NVRAM_RESULT_SUCCESS, tpm_nvram_->WriteSpace(0, "", ""));
  EXPECT_NE(NVRAM_RESULT_SUCCESS, tpm_nvram_->ReadSpace(0, nullptr, ""));
  EXPECT_NE(NVRAM_RESULT_SUCCESS, tpm_nvram_->LockSpace(0, false, false, ""));
}

TEST_F(Tpm2NvramTest, DefineSpaceSuccess) {
  SetupOwnerPassword();
  EXPECT_CALL(mock_hmac_session_,
              SetEntityAuthorizationValue(kTestOwnerPassword))
      .Times(AtLeast(1));
  EXPECT_CALL(mock_trial_session_, PolicyAuthValue()).Times(0);
  EXPECT_CALL(mock_trial_session_, PolicyPCR(_, _)).Times(0);
  std::vector<NvramSpaceAttribute> attributes{NVRAM_PERSISTENT_WRITE_LOCK};
  EXPECT_CALL(
      mock_tpm_utility_,
      DefineNVSpace(kSomeNvramIndex, kSomeNvramSize,
                    trunks::TPMA_NV_WRITEDEFINE | trunks::TPMA_NV_AUTHWRITE |
                        trunks::TPMA_NV_AUTHREAD,
                    kFakeAuthorizationValue, std::string() /* policy */,
                    kHMACAuth))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_EQ(
      NVRAM_RESULT_SUCCESS,
      tpm_nvram_->DefineSpace(kSomeNvramIndex, kSomeNvramSize,
                              attributes, kFakeAuthorizationValue,
                              NVRAM_POLICY_NONE));
  const LocalData& local_data = mock_data_store_.GetFakeData();
  EXPECT_EQ(1, local_data.nvram_policy_size());
  EXPECT_EQ(kSomeNvramIndex, local_data.nvram_policy(0).index());
  EXPECT_EQ(NVRAM_POLICY_NONE, local_data.nvram_policy(0).policy());
}

TEST_F(Tpm2NvramTest, DefineSpaceSuccessPlatformReadable) {
  SetupOwnerPassword();
  EXPECT_CALL(mock_hmac_session_,
              SetEntityAuthorizationValue(kTestOwnerPassword))
      .Times(AtLeast(1));
  EXPECT_CALL(mock_trial_session_, PolicyAuthValue()).Times(0);
  EXPECT_CALL(mock_trial_session_, PolicyPCR(_, _)).Times(0);
  std::vector<NvramSpaceAttribute> attributes{NVRAM_PERSISTENT_WRITE_LOCK,
                                              NVRAM_PLATFORM_READ};
  EXPECT_CALL(
      mock_tpm_utility_,
      DefineNVSpace(kSomeNvramIndex, kSomeNvramSize,
                    trunks::TPMA_NV_WRITEDEFINE | trunks::TPMA_NV_AUTHWRITE |
                        trunks::TPMA_NV_AUTHREAD | trunks::TPMA_NV_PPREAD,
                    kFakeAuthorizationValue, std::string() /* policy */,
                    kHMACAuth))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_EQ(
      NVRAM_RESULT_SUCCESS,
      tpm_nvram_->DefineSpace(kSomeNvramIndex, kSomeNvramSize,
                              attributes, kFakeAuthorizationValue,
                              NVRAM_POLICY_NONE));
  const LocalData& local_data = mock_data_store_.GetFakeData();
  EXPECT_EQ(1, local_data.nvram_policy_size());
  EXPECT_EQ(kSomeNvramIndex, local_data.nvram_policy(0).index());
  EXPECT_EQ(NVRAM_POLICY_NONE, local_data.nvram_policy(0).policy());
}

TEST_F(Tpm2NvramTest, DefineSpaceFailure) {
  SetupOwnerPassword();
  std::vector<NvramSpaceAttribute> attributes{NVRAM_PERSISTENT_WRITE_LOCK};
  EXPECT_CALL(mock_tpm_utility_, DefineNVSpace(_, _, _, _, _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  EXPECT_NE(
      NVRAM_RESULT_SUCCESS,
      tpm_nvram_->DefineSpace(kSomeNvramIndex, kSomeNvramSize,
                              attributes, "", NVRAM_POLICY_NONE));
}

TEST_F(Tpm2NvramTest, DefineSpaceNoClobberOnError) {
  SetupOwnerPassword();
  EXPECT_CALL(mock_tpm_utility_, DefineNVSpace(_, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->DefineSpace(kSomeNvramIndex, kSomeNvramSize, {}, "",
                                    NVRAM_POLICY_NONE));
  EXPECT_NE(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->DefineSpace(kSomeNvramIndex, kSomeNvramSize, {}, "",
                                    NVRAM_POLICY_PCR0));
  const LocalData& local_data = mock_data_store_.GetFakeData();
  EXPECT_EQ(1, local_data.nvram_policy_size());
  EXPECT_EQ(kSomeNvramIndex, local_data.nvram_policy(0).index());
  EXPECT_EQ(NVRAM_POLICY_NONE, local_data.nvram_policy(0).policy());
}

TEST_F(Tpm2NvramTest, DefineSpaceWithPolicy) {
  SetupOwnerPassword();
  EXPECT_CALL(mock_hmac_session_,
              SetEntityAuthorizationValue(kTestOwnerPassword))
      .Times(AtLeast(1));
  EXPECT_CALL(mock_trial_session_, PolicyAuthValue()).Times(AtLeast(1));
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(0, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(kFakePCRValue), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_trial_session_, PolicyPCR(0, kFakePCRValue))
      .Times(AtLeast(1));
  std::vector<NvramSpaceAttribute> attributes{NVRAM_WRITE_AUTHORIZATION};
  EXPECT_CALL(
      mock_tpm_utility_,
      DefineNVSpace(kSomeNvramIndex, kSomeNvramSize,
                    trunks::TPMA_NV_POLICYWRITE | trunks::TPMA_NV_POLICYREAD,
                    kFakeAuthorizationValue, kFakePolicyDigest, kHMACAuth))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_EQ(
      NVRAM_RESULT_SUCCESS,
      tpm_nvram_->DefineSpace(kSomeNvramIndex, kSomeNvramSize,
                              attributes, kFakeAuthorizationValue,
                              NVRAM_POLICY_PCR0));
}

TEST_F(Tpm2NvramTest, DefineSpaceWithExistingLocalData) {
  SetupOwnerPassword();
  LocalData& local_data = mock_data_store_.GetMutableFakeData();
  local_data.add_nvram_policy()->set_index(0);
  local_data.add_nvram_policy()->set_index(4);
  local_data.add_nvram_policy()->set_index(1);
  uint32_t index = 5;  // not in {0, 4, 1} added above
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->DefineSpace(index, kSomeNvramSize, {}, "",
                                    NVRAM_POLICY_NONE));
  EXPECT_EQ(4, local_data.nvram_policy_size());
  EXPECT_EQ(index, local_data.nvram_policy(3).index());
}

TEST_F(Tpm2NvramTest, DefineSpaceClobberExistingLocalData) {
  SetupOwnerPassword();
  LocalData& local_data = mock_data_store_.GetMutableFakeData();
  local_data.add_nvram_policy()->set_index(0);
  local_data.add_nvram_policy()->set_index(4);
  local_data.add_nvram_policy()->set_index(1);
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->DefineSpace(4, kSomeNvramSize, {}, "",
                                    NVRAM_POLICY_NONE));
  EXPECT_EQ(3, local_data.nvram_policy_size());
  EXPECT_NE(local_data.nvram_policy(0).index(),
            local_data.nvram_policy(1).index());
  EXPECT_NE(local_data.nvram_policy(0).index(),
            local_data.nvram_policy(2).index());
  EXPECT_NE(local_data.nvram_policy(1).index(),
            local_data.nvram_policy(2).index());
}

TEST_F(Tpm2NvramTest, DestroySpaceSuccess) {
  SetupOwnerPassword();
  LocalData& local_data = mock_data_store_.GetMutableFakeData();
  local_data.add_nvram_policy()->set_index(kSomeNvramIndex);
  EXPECT_CALL(mock_hmac_session_,
              SetEntityAuthorizationValue(kTestOwnerPassword))
      .Times(AtLeast(1));
  EXPECT_CALL(mock_tpm_utility_, DestroyNVSpace(kSomeNvramIndex, kHMACAuth))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_EQ(NVRAM_RESULT_SUCCESS, tpm_nvram_->DestroySpace(kSomeNvramIndex));
  EXPECT_EQ(0, local_data.nvram_policy_size());
}

TEST_F(Tpm2NvramTest, DestroySpaceFailure) {
  SetupOwnerPassword();
  LocalData& local_data = mock_data_store_.GetMutableFakeData();
  local_data.add_nvram_policy()->set_index(kSomeNvramIndex);
  EXPECT_CALL(mock_tpm_utility_, DestroyNVSpace(kSomeNvramIndex, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  EXPECT_NE(NVRAM_RESULT_SUCCESS, tpm_nvram_->DestroySpace(kSomeNvramIndex));
  EXPECT_EQ(1, local_data.nvram_policy_size());
}

TEST_F(Tpm2NvramTest, DestroySpaceWithExistingLocalData) {
  SetupOwnerPassword();
  LocalData& local_data = mock_data_store_.GetMutableFakeData();
  local_data.add_nvram_policy()->set_index(0);
  local_data.add_nvram_policy()->set_index(1);
  local_data.add_nvram_policy()->set_index(2);
  uint32_t destroyed_index = 1;  // one of {0, 1, 2} added above
  EXPECT_CALL(mock_tpm_utility_, DestroyNVSpace(destroyed_index, kHMACAuth))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_EQ(NVRAM_RESULT_SUCCESS, tpm_nvram_->DestroySpace(destroyed_index));
  EXPECT_EQ(2, local_data.nvram_policy_size());
  EXPECT_NE(destroyed_index, local_data.nvram_policy(0).index());
  EXPECT_NE(destroyed_index, local_data.nvram_policy(1).index());
}

TEST_F(Tpm2NvramTest, WriteSpaceSuccess) {
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize, kNoExtraAttributes,
                     EXPECT_AUTH, NORMAL_AUTH);
  EXPECT_CALL(mock_tpm_utility_,
              WriteNVSpace(kSomeNvramIndex, 0, kSomeData, false, false,
                           kHMACAuth))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->WriteSpace(kSomeNvramIndex, kSomeData,
                                   kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, WriteSpaceExtend) {
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize, trunks::TPMA_NV_EXTEND,
                     EXPECT_AUTH, NORMAL_AUTH);
  EXPECT_CALL(mock_tpm_utility_,
              WriteNVSpace(kSomeNvramIndex, 0, kSomeData, false, true,
                           kHMACAuth))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->WriteSpace(kSomeNvramIndex, kSomeData,
                                   kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, WriteSpaceNonexistant) {
  EXPECT_CALL(mock_tpm_utility_, GetNVSpacePublicArea(kSomeNvramIndex, _))
      .WillRepeatedly(Return(TPM_RC_HANDLE));
  std::string read_data;
  EXPECT_EQ(NVRAM_RESULT_SPACE_DOES_NOT_EXIST,
            tpm_nvram_->WriteSpace(kSomeNvramIndex, "data",
                                   kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, WriteSpaceFailure) {
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize, kNoExtraAttributes,
                     EXPECT_AUTH, NORMAL_AUTH);
  EXPECT_CALL(mock_tpm_utility_, WriteNVSpace(kSomeNvramIndex, _, _, _, _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  EXPECT_NE(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->WriteSpace(kSomeNvramIndex, "data",
                                   kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, WriteSpacePolicy) {
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize, kNoExtraAttributes,
                     EXPECT_AUTH, POLICY_AUTH);
  EXPECT_CALL(mock_tpm_utility_,
              WriteNVSpace(kSomeNvramIndex, 0, kSomeData, false, false,
                           kPolicyAuth))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->WriteSpace(kSomeNvramIndex, kSomeData,
                                   kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, WriteSpaceOwner) {
  SetupOwnerPassword();
  SetupExistingSpace(kSomeNvramIndex, kSomeData.size(), kNoExtraAttributes,
                     EXPECT_AUTH, OWNER_AUTH);
  EXPECT_CALL(mock_tpm_utility_,
              WriteNVSpace(kSomeNvramIndex, 0, kSomeData, true, false,
                           kHMACAuth))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->WriteSpace(kSomeNvramIndex, kSomeData,
                                   kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, ReadSpaceSuccess) {
  SetupExistingSpace(kSomeNvramIndex, kSomeData.size(), trunks::TPMA_NV_WRITTEN,
                     EXPECT_AUTH, NORMAL_AUTH);
  EXPECT_CALL(mock_tpm_utility_,
              ReadNVSpace(kSomeNvramIndex, 0, kSomeData.size(), false, _,
                          kHMACAuth))
      .WillOnce(DoAll(SetArgPointee<4>(kSomeData), Return(TPM_RC_SUCCESS)));
  std::string read_data;
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->ReadSpace(kSomeNvramIndex, &read_data,
                                  kFakeAuthorizationValue));
  EXPECT_EQ(kSomeData, read_data);
}

TEST_F(Tpm2NvramTest, ReadSpaceNonexistant) {
  EXPECT_CALL(mock_tpm_utility_, GetNVSpacePublicArea(kSomeNvramIndex, _))
      .WillRepeatedly(Return(TPM_RC_HANDLE));
  std::string read_data;
  EXPECT_EQ(NVRAM_RESULT_SPACE_DOES_NOT_EXIST,
            tpm_nvram_->ReadSpace(kSomeNvramIndex, &read_data,
                                  kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, ReadSpaceFailure) {
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize, trunks::TPMA_NV_WRITTEN,
                     EXPECT_AUTH, NORMAL_AUTH);
  EXPECT_CALL(mock_tpm_utility_, ReadNVSpace(kSomeNvramIndex, _, _, _, _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  std::string read_data;
  EXPECT_NE(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->ReadSpace(kSomeNvramIndex, &read_data,
                                  kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, ReadSpacePolicy) {
  SetupExistingSpace(kSomeNvramIndex, kSomeData.size(), trunks::TPMA_NV_WRITTEN,
                     EXPECT_AUTH, POLICY_AUTH);
  EXPECT_CALL(mock_tpm_utility_,
              ReadNVSpace(kSomeNvramIndex, 0, kSomeData.size(), false, _,
                          kPolicyAuth))
      .WillOnce(DoAll(SetArgPointee<4>(kSomeData), Return(TPM_RC_SUCCESS)));
  std::string read_data;
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->ReadSpace(kSomeNvramIndex, &read_data,
                                  kFakeAuthorizationValue));
  EXPECT_EQ(kSomeData, read_data);
}

TEST_F(Tpm2NvramTest, ReadSpaceOwner) {
  SetupOwnerPassword();
  SetupExistingSpace(kSomeNvramIndex, kSomeData.size(), trunks::TPMA_NV_WRITTEN,
                     EXPECT_AUTH, OWNER_AUTH);
  EXPECT_CALL(mock_tpm_utility_,
              ReadNVSpace(kSomeNvramIndex, 0, kSomeData.size(), true, _,
                          kHMACAuth))
      .WillOnce(DoAll(SetArgPointee<4>(kSomeData), Return(TPM_RC_SUCCESS)));
  std::string read_data;
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->ReadSpace(kSomeNvramIndex, &read_data,
                                  kFakeAuthorizationValue));
  EXPECT_EQ(kSomeData, read_data);
}

TEST_F(Tpm2NvramTest, LockSpaceSuccess) {
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize, kNoExtraAttributes,
                     EXPECT_AUTH, NORMAL_AUTH);
  EXPECT_CALL(mock_tpm_utility_,
              LockNVSpace(kSomeNvramIndex, true, _, false, kHMACAuth))
      .Times(AtLeast(1));
  EXPECT_CALL(mock_tpm_utility_,
              LockNVSpace(kSomeNvramIndex, _, true, false, kHMACAuth))
      .Times(AtLeast(1));
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->LockSpace(kSomeNvramIndex, true, true,
                                  kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, LockSpaceNonexistant) {
  EXPECT_CALL(mock_tpm_utility_, GetNVSpacePublicArea(kSomeNvramIndex, _))
      .WillOnce(Return(trunks::TPM_RC_HANDLE));
  EXPECT_EQ(NVRAM_RESULT_SPACE_DOES_NOT_EXIST,
            tpm_nvram_->LockSpace(kSomeNvramIndex, true, true,
                                  kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, LockSpaceFailure) {
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize, kNoExtraAttributes,
                     EXPECT_AUTH, NORMAL_AUTH);
  EXPECT_CALL(mock_tpm_utility_, LockNVSpace(_, _, _, _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  EXPECT_NE(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->LockSpace(kSomeNvramIndex, true, true,
                                  kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, LockSpacePolicy) {
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize, kNoExtraAttributes,
                     EXPECT_AUTH, POLICY_AUTH);
  EXPECT_CALL(mock_tpm_utility_,
              LockNVSpace(kSomeNvramIndex, true, _, false, kPolicyAuth))
      .Times(AtLeast(1));
  EXPECT_CALL(mock_tpm_utility_,
              LockNVSpace(kSomeNvramIndex, _, true, false, kPolicyAuth))
      .Times(AtLeast(1));
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->LockSpace(kSomeNvramIndex, true, true,
                                  kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, LockSpaceOwner) {
  SetupOwnerPassword();
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize, kNoExtraAttributes,
                     EXPECT_AUTH, OWNER_AUTH);
  EXPECT_CALL(mock_tpm_utility_,
              LockNVSpace(kSomeNvramIndex, true, _, true, kHMACAuth))
      .Times(AtLeast(1));
  EXPECT_CALL(mock_tpm_utility_,
              LockNVSpace(kSomeNvramIndex, _, true, true, kHMACAuth))
      .Times(AtLeast(1));
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->LockSpace(kSomeNvramIndex, true, true,
                                  kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, LockSpaceRead) {
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize, kNoExtraAttributes,
                     EXPECT_AUTH, NORMAL_AUTH);
  EXPECT_CALL(mock_tpm_utility_,
              LockNVSpace(kSomeNvramIndex, true, false, false, kHMACAuth))
      .Times(AtLeast(1));
  EXPECT_CALL(mock_tpm_utility_,
              LockNVSpace(kSomeNvramIndex, _, true, false, kHMACAuth))
      .Times(0);
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->LockSpace(kSomeNvramIndex, true, false,
                                  kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, LockSpaceWrite) {
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize, kNoExtraAttributes,
                     EXPECT_AUTH, NORMAL_AUTH);
  EXPECT_CALL(mock_tpm_utility_,
              LockNVSpace(kSomeNvramIndex, false, true, false, kHMACAuth))
      .Times(AtLeast(1));
  EXPECT_CALL(mock_tpm_utility_,
              LockNVSpace(kSomeNvramIndex, true, _, false, kHMACAuth))
      .Times(0);
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->LockSpace(kSomeNvramIndex, false, true,
                                  kFakeAuthorizationValue));
}

TEST_F(Tpm2NvramTest, ListSpacesSuccess) {
  std::vector<uint32_t> expected_spaces{1, 5, 42};
  std::vector<uint32_t> spaces;
  EXPECT_CALL(mock_tpm_utility_, ListNVSpaces(_))
      .Times(AtLeast(1))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(expected_spaces), Return(TPM_RC_SUCCESS)));
  EXPECT_EQ(NVRAM_RESULT_SUCCESS, tpm_nvram_->ListSpaces(&spaces));
  EXPECT_EQ(expected_spaces, spaces);
}

TEST_F(Tpm2NvramTest, ListSpacesFailure) {
  std::vector<uint32_t> spaces;
  EXPECT_CALL(mock_tpm_utility_, ListNVSpaces(_))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  EXPECT_NE(NVRAM_RESULT_SUCCESS, tpm_nvram_->ListSpaces(&spaces));
}

TEST_F(Tpm2NvramTest, GetSpaceInfoSuccess) {
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize,
                     trunks::TPMA_NV_READLOCKED | trunks::TPMA_NV_WRITELOCKED,
                     NO_EXPECT_AUTH, POLICY_AUTH);
  size_t size;
  bool is_read_locked;
  bool is_write_locked;
  std::vector<NvramSpaceAttribute> attributes;
  NvramSpacePolicy policy;
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->GetSpaceInfo(kSomeNvramIndex, &size, &is_write_locked,
                                     &is_read_locked, &attributes, &policy));
  EXPECT_EQ(kSomeNvramSize, size);
  EXPECT_TRUE(is_read_locked);
  EXPECT_TRUE(is_write_locked);
  EXPECT_GE(attributes.size(), 1);
  EXPECT_EQ(1, std::count(attributes.begin(), attributes.end(),
                          NVRAM_WRITE_AUTHORIZATION));
  EXPECT_EQ(NVRAM_POLICY_PCR0, policy);
}

TEST_F(Tpm2NvramTest, GetSpaceInfoSuccessAlt) {
  SetupExistingSpace(kSomeNvramIndex, kSomeNvramSize,
                     trunks::TPMA_NV_AUTHREAD | trunks::TPMA_NV_AUTHWRITE |
                         trunks::TPMA_NV_PPREAD,
                     NO_EXPECT_AUTH, POLICY_AUTH);
  size_t size;
  bool is_read_locked;
  bool is_write_locked;
  std::vector<NvramSpaceAttribute> attributes;
  NvramSpacePolicy policy;
  EXPECT_EQ(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->GetSpaceInfo(kSomeNvramIndex, &size, &is_write_locked,
                                     &is_read_locked, &attributes, &policy));
  EXPECT_EQ(kSomeNvramSize, size);
  EXPECT_FALSE(is_read_locked);
  EXPECT_FALSE(is_write_locked);
  EXPECT_GE(attributes.size(), 3);
  EXPECT_GE(std::count(attributes.begin(), attributes.end(),
                       NVRAM_WRITE_AUTHORIZATION), 1);
  EXPECT_GE(std::count(attributes.begin(), attributes.end(),
                       NVRAM_READ_AUTHORIZATION), 1);
  EXPECT_GE(std::count(attributes.begin(), attributes.end(),
                       NVRAM_PLATFORM_READ), 1);
  EXPECT_EQ(NVRAM_POLICY_PCR0, policy);
}

TEST_F(Tpm2NvramTest, GetSpaceInfoFailure) {
  EXPECT_CALL(mock_tpm_utility_, GetNVSpacePublicArea(kSomeNvramIndex, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  size_t size;
  bool is_read_locked;
  bool is_write_locked;
  std::vector<NvramSpaceAttribute> attributes;
  NvramSpacePolicy policy;
  EXPECT_NE(NVRAM_RESULT_SUCCESS,
            tpm_nvram_->GetSpaceInfo(kSomeNvramIndex, &size, &is_write_locked,
                                     &is_read_locked, &attributes, &policy));
}

}  // namespace tpm_manager
