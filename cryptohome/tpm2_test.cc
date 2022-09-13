// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for Tpm2Impl.

#include "cryptohome/tpm2_impl.h"

#include <stdint.h>

#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include <base/bind.h>
#include <base/callback.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/memory/ptr_util.h>
#include <base/memory/ref_counted.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/single_thread_task_runner.h>
#include <base/threading/thread_task_runner_handle.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <cryptohome/proto_bindings/key.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/status.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <tpm_manager/client/mock_tpm_manager_utility.h>
#include <trunks/mock_authorization_delegate.h>
#include <trunks/mock_blob_parser.h>
#include <trunks/mock_hmac_session.h>
#include <trunks/mock_policy_session.h>
#include <trunks/mock_tpm.h>
#include <trunks/mock_tpm_state.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/tpm_constants.h>
#include <trunks/tpm_generated.h>
#include <trunks/trunks_factory.h>
#include <trunks/trunks_factory_for_test.h>

#include "cryptohome/protobuf_test_utils.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using brillo::SecureBlob;
using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;
using hwsec_foundation::Sha256;
using hwsec_foundation::Sha256ToSecureBlob;
using testing::_;
using testing::DoAll;
using testing::ElementsAreArray;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using testing::Values;
using testing::WithArg;
using tpm_manager::LocalData;
using tpm_manager::MockTpmManagerUtility;
using tpm_manager::NVRAM_RESULT_IPC_ERROR;
using trunks::TPM_ALG_ID;
using trunks::TPM_RC;
using trunks::TPM_RC_FAILURE;
using trunks::TPM_RC_SUCCESS;
using trunks::TrunksFactory;

namespace {

// Reset the |pcr_select| and set the bit corresponding to |index|.
void SetPcrSelectData(uint8_t* pcr_select, uint32_t index) {
  for (uint8_t i = 0; i < PCR_SELECT_MIN; ++i) {
    pcr_select[i] = 0;
  }
  pcr_select[index / 8] = 1 << (index % 8);
}

}  // namespace

namespace cryptohome {

class Tpm2Test : public testing::Test {
 public:
  Tpm2Test() {
    factory_.set_blob_parser(&mock_blob_parser_);
    factory_.set_tpm(&mock_tpm_);
    factory_.set_tpm_state(&mock_tpm_state_);
    factory_.set_tpm_utility(&mock_tpm_utility_);
    factory_.set_hmac_session(&mock_hmac_session_);
    factory_.set_policy_session(&mock_policy_session_);
    factory_.set_trial_session(&mock_trial_session_);
    auto hwsec = std::make_unique<hwsec::MockCryptohomeFrontend>();
    hwsec_ = hwsec.get();
    tpm_ = std::make_unique<Tpm2Impl>(std::move(hwsec), &factory_,
                                      &mock_tpm_manager_utility_);
  }

 protected:
  std::unique_ptr<Tpm2Impl> tpm_;
  hwsec::MockCryptohomeFrontend* hwsec_;
  NiceMock<trunks::MockAuthorizationDelegate> mock_authorization_delegate_;
  NiceMock<trunks::MockBlobParser> mock_blob_parser_;
  NiceMock<trunks::MockTpm> mock_tpm_;
  NiceMock<trunks::MockTpmState> mock_tpm_state_;
  NiceMock<trunks::MockTpmUtility> mock_tpm_utility_;
  NiceMock<trunks::MockHmacSession> mock_hmac_session_;
  NiceMock<trunks::MockPolicySession> mock_policy_session_;
  NiceMock<trunks::MockPolicySession> mock_trial_session_;
  NiceMock<tpm_manager::MockTpmManagerUtility> mock_tpm_manager_utility_;

 private:
  trunks::TrunksFactoryForTest factory_;
};

TEST_F(Tpm2Test, GetPcrMapNotExtended) {
  std::string obfuscated_username = "OBFUSCATED_USER";
  std::map<uint32_t, brillo::Blob> result =
      tpm_->GetPcrMap(obfuscated_username, /*use_extended_pcr=*/false);

  EXPECT_EQ(1, result.size());
  const brillo::Blob& result_blob = result[kTpmSingleUserPCR];

  brillo::Blob expected_result(SHA256_DIGEST_LENGTH, 0);
  EXPECT_EQ(expected_result, result_blob);
}

TEST_F(Tpm2Test, GetPcrMapExtended) {
  std::string obfuscated_username = "OBFUSCATED_USER";
  std::map<uint32_t, brillo::Blob> result =
      tpm_->GetPcrMap(obfuscated_username, /*use_extended_pcr=*/true);

  EXPECT_EQ(1, result.size());
  const brillo::Blob& result_blob = result[kTpmSingleUserPCR];

  // Pre-calculated expected result.
  brillo::Blob expected_result{0x2D, 0x5B, 0x86, 0xF2, 0xBE, 0xEE, 0xD1, 0xB7,
                               0x40, 0xC7, 0xCD, 0xE3, 0x88, 0x25, 0xA6, 0xEE,
                               0xE3, 0x98, 0x69, 0xA4, 0x99, 0x4D, 0x88, 0x09,
                               0x85, 0x6E, 0x0E, 0x11, 0x7A, 0x4E, 0xFD, 0x91};
  EXPECT_EQ(expected_result, result_blob);
}

TEST_F(Tpm2Test, Enabled) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(0);
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(tpm_->IsEnabled());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(false), Return(true)));
  EXPECT_FALSE(tpm_->IsEnabled());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(true), Return(true)));
  EXPECT_TRUE(tpm_->IsEnabled());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(0);
  EXPECT_TRUE(tpm_->IsEnabled());
}

TEST_F(Tpm2Test, OwnedWithoutSignal) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(tpm_->IsOwned());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(false), Return(true)));
  EXPECT_FALSE(tpm_->IsOwned());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(true), Return(true)));
  EXPECT_TRUE(tpm_->IsOwned());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(0);
  EXPECT_TRUE(tpm_->IsOwned());
}

TEST_F(Tpm2Test, GetDictionaryAttackInfo) {
  int result_counter = 0;
  int result_threshold = 0;
  bool result_lockout = false;
  int result_seconds_remaining = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, GetDictionaryAttackInfo(_, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(tpm_->GetDictionaryAttackInfo(&result_counter, &result_threshold,
                                             &result_lockout,
                                             &result_seconds_remaining));

  EXPECT_CALL(mock_tpm_manager_utility_, GetDictionaryAttackInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(123), SetArgPointee<1>(456),
                      SetArgPointee<2>(true), SetArgPointee<3>(789),
                      Return(true)));
  EXPECT_TRUE(tpm_->GetDictionaryAttackInfo(&result_counter, &result_threshold,
                                            &result_lockout,
                                            &result_seconds_remaining));
  EXPECT_EQ(result_counter, 123);
  EXPECT_EQ(result_threshold, 456);
  EXPECT_TRUE(result_lockout);
  EXPECT_EQ(result_seconds_remaining, 789);
}

TEST_F(Tpm2Test, ResetDictionaryAttackMitigation) {
  EXPECT_CALL(mock_tpm_manager_utility_, ResetDictionaryAttackLock())
      .WillOnce(Return(false));
  EXPECT_FALSE(tpm_->ResetDictionaryAttackMitigation());
  EXPECT_CALL(mock_tpm_manager_utility_, ResetDictionaryAttackLock())
      .WillOnce(Return(true));
  EXPECT_TRUE(tpm_->ResetDictionaryAttackMitigation());
}

TEST_F(Tpm2Test, SignalCache) {
  ON_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillByDefault(Return(false));

  ON_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillByDefault(Return(false));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(1);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(1);
  EXPECT_FALSE(tpm_->IsOwned());

  ON_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillByDefault(DoAll(SetArgPointee<0>(false), Return(true)));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(1);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(1);
  EXPECT_FALSE(tpm_->IsOwned());

  ON_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillByDefault(
          DoAll(SetArgPointee<0>(true), SetArgPointee<1>(false), Return(true)));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(1);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(2);
  EXPECT_FALSE(tpm_->IsOwned());
  EXPECT_FALSE(tpm_->IsOwned());

  LocalData expected_local_data;
  expected_local_data.set_owner_password("owner password");
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(true), SetArgPointee<1>(true),
                      SetArgPointee<2>(expected_local_data), Return(true)));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(0);
  EXPECT_TRUE(tpm_->IsOwned());
  EXPECT_TRUE(tpm_->IsEnabled());
}

TEST_F(Tpm2Test, RemoveTpmOwnerDependency) {
  EXPECT_CALL(mock_tpm_manager_utility_,
              RemoveOwnerDependency(tpm_manager::kTpmOwnerDependency_Nvram))
      .WillOnce(Return(true));
  EXPECT_TRUE(
      tpm_->RemoveOwnerDependency(Tpm::TpmOwnerDependency::kInstallAttributes));
  EXPECT_CALL(
      mock_tpm_manager_utility_,
      RemoveOwnerDependency(tpm_manager::kTpmOwnerDependency_Attestation))
      .WillOnce(Return(false));
  EXPECT_FALSE(
      tpm_->RemoveOwnerDependency(Tpm::TpmOwnerDependency::kAttestation));
}

TEST_F(Tpm2Test, RemoveTpmOwnerDependencyInvalidEnum) {
  EXPECT_DEBUG_DEATH(
      tpm_->RemoveOwnerDependency(static_cast<Tpm::TpmOwnerDependency>(999)),
      ".*Unexpected enum class value: 999");
}

TEST_F(Tpm2Test, GetVersionInfoCache) {
  Tpm::TpmVersionInfo expected_version_info;
  expected_version_info.family = 1;
  expected_version_info.spec_level = 2;
  expected_version_info.manufacturer = 3;
  expected_version_info.tpm_model = 4;
  expected_version_info.firmware_version = 5;
  expected_version_info.vendor_specific = "aa";

  EXPECT_CALL(mock_tpm_manager_utility_, GetVersionInfo(_, _, _, _, _, _))
      .WillOnce(Return(false))
      .WillOnce(DoAll(SetArgPointee<0>(expected_version_info.family),
                      SetArgPointee<1>(expected_version_info.spec_level),
                      SetArgPointee<2>(expected_version_info.manufacturer),
                      SetArgPointee<3>(expected_version_info.tpm_model),
                      SetArgPointee<4>(expected_version_info.firmware_version),
                      SetArgPointee<5>(expected_version_info.vendor_specific),
                      Return(true)));

  Tpm::TpmVersionInfo actual_version_info;
  // Requests from tpm_manager, failed, not cached
  EXPECT_FALSE(tpm_->GetVersionInfo(&actual_version_info));

  // Requests from tpm_manager, succeeded, cached
  EXPECT_TRUE(tpm_->GetVersionInfo(&actual_version_info));
  EXPECT_EQ(expected_version_info.GetFingerprint(),
            actual_version_info.GetFingerprint());

  // Returns from cache
  EXPECT_TRUE(tpm_->GetVersionInfo(&actual_version_info));
  EXPECT_EQ(expected_version_info.GetFingerprint(),
            actual_version_info.GetFingerprint());
}

TEST_F(Tpm2Test, GetVersionInfoBadInput) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetVersionInfo(_, _, _, _, _, _))
      .Times(0);
  EXPECT_FALSE(tpm_->GetVersionInfo(nullptr));
}

TEST_F(Tpm2Test, BadTpmManagerUtility) {
  EXPECT_CALL(mock_tpm_manager_utility_, Initialize())
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(tpm_->IsEnabled());
  EXPECT_FALSE(tpm_->IsOwned());
  EXPECT_FALSE(tpm_->ResetDictionaryAttackMitigation());
  int result_counter;
  int result_threshold;
  bool result_lockout;
  int result_seconds_remaining;
  EXPECT_FALSE(tpm_->GetDictionaryAttackInfo(&result_counter, &result_threshold,
                                             &result_lockout,
                                             &result_seconds_remaining));
}

TEST_F(Tpm2Test, GetRandomDataSuccess) {
  std::string random_data("random_data");
  size_t num_bytes = random_data.size();
  brillo::Blob data;
  EXPECT_CALL(mock_tpm_utility_, GenerateRandom(num_bytes, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(random_data), Return(TPM_RC_SUCCESS)));
  EXPECT_EQ(nullptr, tpm_->GetRandomDataBlob(num_bytes, &data));
  EXPECT_EQ(data.size(), num_bytes);
  std::string tpm_data(data.begin(), data.end());
  EXPECT_EQ(tpm_data, random_data);
}

TEST_F(Tpm2Test, GetRandomDataFailure) {
  brillo::Blob data;
  size_t num_bytes = 5;
  EXPECT_CALL(mock_tpm_utility_, GenerateRandom(num_bytes, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  hwsec::Status err = tpm_->GetRandomDataBlob(num_bytes, &data);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, GetRandomDataBadLength) {
  std::string random_data("random_data");
  brillo::Blob data;
  size_t num_bytes = random_data.size() + 1;
  EXPECT_CALL(mock_tpm_utility_, GenerateRandom(num_bytes, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(random_data), Return(TPM_RC_SUCCESS)));
  hwsec::Status err = tpm_->GetRandomDataBlob(num_bytes, &data);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, DefineNvramSuccess) {
  constexpr uint32_t kIndex = 2;
  constexpr size_t kLength = 5;
  uint32_t index = 0;
  size_t length = 0;
  bool write_define = false;
  bool bind_to_pcr0 = false;
  bool firmware_readable = false;
  EXPECT_CALL(mock_tpm_manager_utility_, DefineSpace(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&index), SaveArg<1>(&length),
                      SaveArg<2>(&write_define), SaveArg<3>(&bind_to_pcr0),
                      SaveArg<4>(&firmware_readable), Return(true)));
  EXPECT_TRUE(tpm_->DefineNvram(kIndex, kLength, Tpm::kTpmNvramWriteDefine));
  EXPECT_EQ(kIndex, index);
  EXPECT_EQ(kLength, length);
  ASSERT_TRUE(write_define);
  ASSERT_FALSE(bind_to_pcr0);
  ASSERT_FALSE(firmware_readable);
}

TEST_F(Tpm2Test, DefineNvramSuccessWithPolicy) {
  constexpr uint32_t kIndex = 2;
  constexpr size_t kLength = 5;
  uint32_t index = 0;
  size_t length = 0;
  bool write_define = false;
  bool bind_to_pcr0 = false;
  bool firmware_readable = false;
  EXPECT_CALL(mock_tpm_manager_utility_, DefineSpace(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&index), SaveArg<1>(&length),
                      SaveArg<2>(&write_define), SaveArg<3>(&bind_to_pcr0),
                      SaveArg<4>(&firmware_readable), Return(true)));
  EXPECT_TRUE(tpm_->DefineNvram(
      kIndex, kLength, Tpm::kTpmNvramWriteDefine | Tpm::kTpmNvramBindToPCR0));
  EXPECT_EQ(kIndex, index);
  EXPECT_EQ(kLength, length);
  ASSERT_TRUE(write_define);
  ASSERT_TRUE(bind_to_pcr0);
  ASSERT_FALSE(firmware_readable);
}

TEST_F(Tpm2Test, DefineNvramSuccessFirmwareReadable) {
  constexpr uint32_t kIndex = 2;
  constexpr size_t kLength = 5;
  uint32_t index = 0;
  size_t length = 0;
  bool write_define = false;
  bool bind_to_pcr0 = false;
  bool firmware_readable = false;
  EXPECT_CALL(mock_tpm_manager_utility_, DefineSpace(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&index), SaveArg<1>(&length),
                      SaveArg<2>(&write_define), SaveArg<3>(&bind_to_pcr0),
                      SaveArg<4>(&firmware_readable), Return(true)));
  EXPECT_TRUE(tpm_->DefineNvram(
      kIndex, kLength,
      Tpm::kTpmNvramWriteDefine | Tpm::kTpmNvramFirmwareReadable));
  EXPECT_EQ(kIndex, index);
  EXPECT_EQ(kLength, length);
  ASSERT_TRUE(write_define);
  ASSERT_FALSE(bind_to_pcr0);
  ASSERT_TRUE(firmware_readable);
}

TEST_F(Tpm2Test, DefineNvramFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, DefineSpace(_, _, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(tpm_->DefineNvram(0, 0, 0));
}

TEST_F(Tpm2Test, DestroyNvramSuccess) {
  constexpr uint32_t kIndex = 2;
  uint32_t index = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, DestroySpace(_))
      .WillOnce(DoAll(SaveArg<0>(&index), Return(true)));
  EXPECT_TRUE(tpm_->DestroyNvram(kIndex));
  EXPECT_EQ(kIndex, index);
}

TEST_F(Tpm2Test, DestroyNvramFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, DestroySpace(_))
      .WillOnce(Return(false));
  EXPECT_FALSE(tpm_->DestroyNvram(0));
}

TEST_F(Tpm2Test, WriteNvram) {
  constexpr uint32_t kIndex = 2;
  const std::string kData("nvram_data");

  EXPECT_CALL(mock_tpm_manager_utility_,
              WriteSpace(kIndex, kData, /*use_owner_auth=*/false))
      .WillOnce(Return(true));
  EXPECT_TRUE(tpm_->WriteNvram(kIndex, SecureBlob(kData)));

  EXPECT_CALL(mock_tpm_manager_utility_,
              WriteSpace(kIndex, kData, /*use_owner_auth=*/false))
      .WillOnce(Return(false));
  EXPECT_FALSE(tpm_->WriteNvram(kIndex, SecureBlob(kData)));
}

TEST_F(Tpm2Test, OwnerWriteNvram) {
  constexpr uint32_t kIndex = 2;
  const std::string kData("nvram_data");

  EXPECT_CALL(mock_tpm_manager_utility_,
              WriteSpace(kIndex, kData, /*use_owner_auth=*/true))
      .WillOnce(Return(true));
  EXPECT_TRUE(tpm_->OwnerWriteNvram(kIndex, SecureBlob(kData)));

  EXPECT_CALL(mock_tpm_manager_utility_,
              WriteSpace(kIndex, kData, /*use_owner_auth=*/true))
      .WillOnce(Return(false));
  EXPECT_FALSE(tpm_->OwnerWriteNvram(kIndex, SecureBlob(kData)));
}

TEST_F(Tpm2Test, WriteLockNvramSuccess) {
  constexpr uint32_t kIndex = 2;
  uint32_t index = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, LockSpace(_))
      .WillOnce(DoAll(SaveArg<0>(&index), Return(true)));
  EXPECT_TRUE(tpm_->WriteLockNvram(kIndex));
  EXPECT_EQ(kIndex, index);
}

TEST_F(Tpm2Test, WriteLockNvramFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, LockSpace(_)).WillOnce(Return(false));
  EXPECT_FALSE(tpm_->WriteLockNvram(0));
}

TEST_F(Tpm2Test, ReadNvramSuccess) {
  constexpr uint32_t kIndex = 2;
  constexpr bool kUserOwnerAuth = false;
  const std::string nvram_data("nvram_data");
  uint32_t index = 0;
  bool user_owner_auth = false;
  SecureBlob read_data;
  EXPECT_CALL(mock_tpm_manager_utility_, ReadSpace(_, _, _))
      .WillOnce(DoAll(SaveArg<0>(&index), SaveArg<1>(&user_owner_auth),
                      SetArgPointee<2>(nvram_data), Return(true)));
  EXPECT_TRUE(tpm_->ReadNvram(kIndex, &read_data));
  EXPECT_EQ(index, kIndex);
  EXPECT_EQ(user_owner_auth, kUserOwnerAuth);
  EXPECT_EQ(nvram_data, read_data.to_string());
}

TEST_F(Tpm2Test, ReadNvramFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, ReadSpace(_, _, _))
      .WillOnce(Return(false));
  SecureBlob read_data;
  EXPECT_FALSE(tpm_->ReadNvram(0, &read_data));
}

TEST_F(Tpm2Test, IsNvramDefinedSuccess) {
  constexpr uint32_t kIndex = 2;
  std::vector<uint32_t> spaces;
  spaces.push_back(kIndex);
  EXPECT_CALL(mock_tpm_manager_utility_, ListSpaces(_))
      .WillOnce(DoAll(SetArgPointee<0>(spaces), Return(true)));
  EXPECT_TRUE(tpm_->IsNvramDefined(kIndex));
}

TEST_F(Tpm2Test, IsNvramDefinedFailure) {
  constexpr uint32_t kIndex = 2;
  EXPECT_CALL(mock_tpm_manager_utility_, ListSpaces(_)).WillOnce(Return(false));
  EXPECT_FALSE(tpm_->IsNvramDefined(kIndex));
}

TEST_F(Tpm2Test, IsNvramDefinedUnknownHandle) {
  constexpr uint32_t kIndex = 2;
  std::vector<uint32_t> spaces;
  spaces.push_back(kIndex);
  EXPECT_CALL(mock_tpm_manager_utility_, ListSpaces(_))
      .WillOnce(DoAll(SetArgPointee<0>(spaces), Return(true)));
  EXPECT_FALSE(tpm_->IsNvramDefined(kIndex + 1));
}

TEST_F(Tpm2Test, IsNvramLockedSuccess) {
  constexpr uint32_t kIndex = 2;
  constexpr uint32_t kSize = 5;
  constexpr uint32_t kIsReadLocked = false;
  constexpr uint32_t kIsWriteLocked = true;
  uint32_t index = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&index), SetArgPointee<1>(kSize),
                      SetArgPointee<2>(kIsReadLocked),
                      SetArgPointee<3>(kIsWriteLocked), Return(true)));
  EXPECT_TRUE(tpm_->IsNvramLocked(kIndex));
  EXPECT_EQ(kIndex, index);
}

TEST_F(Tpm2Test, IsNvramLockedNotLocked) {
  constexpr uint32_t kIndex = 2;
  constexpr uint32_t kSize = 5;
  constexpr uint32_t kIsReadLocked = false;
  constexpr uint32_t kIsWriteLocked = false;
  uint32_t index = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&index), SetArgPointee<1>(kSize),
                      SetArgPointee<2>(kIsReadLocked),
                      SetArgPointee<3>(kIsWriteLocked), Return(true)));
  EXPECT_FALSE(tpm_->IsNvramLocked(kIndex));
  EXPECT_EQ(kIndex, index);
}

TEST_F(Tpm2Test, IsNvramLockedFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(_, _, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(tpm_->IsNvramLocked(0));
}

TEST_F(Tpm2Test, GetNvramSizeSuccess) {
  constexpr uint32_t kIndex = 2;
  constexpr uint32_t kSize = 5;
  constexpr uint32_t kIsReadLocked = false;
  constexpr uint32_t kIsWriteLocked = true;
  uint32_t index = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&index), SetArgPointee<1>(kSize),
                      SetArgPointee<2>(kIsReadLocked),
                      SetArgPointee<3>(kIsWriteLocked), Return(true)));
  EXPECT_EQ(tpm_->GetNvramSize(kIndex), kSize);
  EXPECT_EQ(kIndex, index);
}

TEST_F(Tpm2Test, GetNvramSizeFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(_, _, _, _, _))
      .WillOnce(Return(false));
  EXPECT_EQ(tpm_->GetNvramSize(0), 0);
}

TEST_F(Tpm2Test, SignPolicySuccess) {
  uint32_t pcr_index = 5;
  EXPECT_CALL(mock_policy_session_, PolicyPCR(_))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_policy_session_, GetDelegate())
      .WillOnce(Return(&mock_authorization_delegate_));
  std::string tpm_signature(32, 'b');
  EXPECT_CALL(mock_tpm_utility_,
              Sign(_, _, _, _, _, &mock_authorization_delegate_, _))
      .WillOnce(DoAll(SetArgPointee<6>(tpm_signature), Return(TPM_RC_SUCCESS)));
  SecureBlob signature;
  EXPECT_TRUE(tpm_->Sign(SecureBlob("key_blob"), SecureBlob("input"), pcr_index,
                         &signature));
  EXPECT_EQ(signature.to_string(), tpm_signature);
}

TEST_F(Tpm2Test, SignHmacSuccess) {
  EXPECT_CALL(mock_hmac_session_, GetDelegate())
      .WillOnce(Return(&mock_authorization_delegate_));
  std::string tpm_signature(32, 'b');
  EXPECT_CALL(mock_tpm_utility_,
              Sign(_, _, _, _, _, &mock_authorization_delegate_, _))
      .WillOnce(DoAll(SetArgPointee<6>(tpm_signature), Return(TPM_RC_SUCCESS)));

  SecureBlob signature;
  EXPECT_TRUE(tpm_->Sign(SecureBlob("key_blob"), SecureBlob("input"),
                         kNotBoundToPCR, &signature));
  EXPECT_EQ(signature.to_string(), tpm_signature);
}

TEST_F(Tpm2Test, SignLoadFailure) {
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));

  SecureBlob signature;
  EXPECT_FALSE(tpm_->Sign(SecureBlob("key_blob"), SecureBlob("input"),
                          kNotBoundToPCR, &signature));
}

TEST_F(Tpm2Test, SignFailure) {
  uint32_t handle = 42;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(handle), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_, Sign(handle, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));

  SecureBlob signature;
  EXPECT_FALSE(tpm_->Sign(SecureBlob("key_blob"), SecureBlob("input"),
                          kNotBoundToPCR, &signature));
}

TEST_F(Tpm2Test, CreatePCRBoundKeySuccess) {
  uint32_t index = 2;
  brillo::Blob pcr_value = brillo::BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;
  uint32_t modulus = 2048;
  uint32_t exponent = 0x10001;
  EXPECT_CALL(mock_tpm_utility_,
              CreateRSAKeyPair(_, modulus, exponent, _, _, true, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(tpm_->CreatePCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}),
      AsymmetricKeyUsage::kDecryptKey, &key_blob, nullptr, &creation_blob));
}

TEST_F(Tpm2Test, CreatePCRBoundKeyPolicyFailure) {
  uint32_t index = 2;
  brillo::Blob pcr_value = brillo::BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;
  EXPECT_CALL(mock_tpm_utility_, GetPolicyDigestForPcrValues(_, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->CreatePCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}),
      AsymmetricKeyUsage::kDecryptKey, &key_blob, nullptr, &creation_blob));
}

TEST_F(Tpm2Test, CreatePCRBoundKeyFailure) {
  uint32_t index = 2;
  brillo::Blob pcr_value = brillo::BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;
  EXPECT_CALL(mock_tpm_utility_, CreateRSAKeyPair(_, _, _, _, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->CreatePCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}),
      AsymmetricKeyUsage::kDecryptKey, &key_blob, nullptr, &creation_blob));
}

TEST_F(Tpm2Test, CreateMultiplePCRBoundKeySuccess) {
  std::map<uint32_t, brillo::Blob> pcr_map(
      {{2, brillo::Blob()}, {5, brillo::Blob()}});
  SecureBlob key_blob;
  SecureBlob creation_blob;
  uint32_t modulus = 2048;
  uint32_t exponent = 0x10001;
  EXPECT_CALL(mock_tpm_utility_,
              CreateRSAKeyPair(_, modulus, exponent, _, _, true, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(tpm_->CreatePCRBoundKey(pcr_map, AsymmetricKeyUsage::kDecryptKey,
                                      &key_blob, nullptr, &creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeySuccess) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));
  std::string pcr_policy_value;
  std::map<uint32_t, std::string> pcr_map;
  EXPECT_CALL(mock_trial_session_, PolicyPCR(_))
      .WillOnce(DoAll(SaveArg<0>(&pcr_map), Return(TPM_RC_SUCCESS)));
  std::string policy_digest(32, 'a');
  EXPECT_CALL(mock_trial_session_, GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_digest), Return(TPM_RC_SUCCESS)));
  trunks::TPMT_PUBLIC public_area;
  public_area.auth_policy.size = policy_digest.size();
  memcpy(public_area.auth_policy.buffer, policy_digest.data(),
         policy_digest.size());
  public_area.object_attributes &= (~trunks::kUserWithAuth);
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_area), Return(TPM_RC_SUCCESS)));
  ASSERT_TRUE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}), key_blob,
      creation_blob));
  EXPECT_EQ(BlobFromString(pcr_map[index]), pcr_value);
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadCreationBlob) {
  uint32_t index = 2;
  brillo::Blob pcr_value = brillo::BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}), key_blob,
      creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadCreationDataCount) {
  uint32_t index = 2;
  brillo::Blob pcr_value = brillo::BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  creation_data.creation_data.pcr_select.count = 0;
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}), key_blob,
      creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadCreationPCRBank) {
  uint32_t index = 2;
  brillo::Blob pcr_value = brillo::BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA1;
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}), key_blob,
      creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadCreationPCR) {
  uint32_t index = 2;
  brillo::Blob pcr_value = brillo::BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  pcr_select.pcr_selections[0].pcr_select[index / 8] = 0xFF;
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}), key_blob,
      creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadCreationPCRDigest) {
  uint32_t index = 2;
  brillo::Blob pcr_value = brillo::BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(Sha256(SecureBlob("")).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}), key_blob,
      creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyImportedKey) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));

  EXPECT_CALL(mock_tpm_utility_, CertifyCreation(_, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}), key_blob,
      creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadSession) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  for (size_t i = 0; i < PCR_SELECT_MIN; ++i) {
    pcr_select.pcr_selections[0].pcr_select[i] = 0;
  }
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));

  EXPECT_CALL(mock_trial_session_, StartUnboundSession(true, true))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}), key_blob,
      creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadPolicy) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  for (size_t i = 0; i < PCR_SELECT_MIN; ++i) {
    pcr_select.pcr_selections[0].pcr_select[i] = 0;
  }
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));

  EXPECT_CALL(mock_trial_session_, PolicyPCR(_))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}), key_blob,
      creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadDigest) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));

  EXPECT_CALL(mock_trial_session_, GetDigest(_))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}), key_blob,
      creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadPolicyDigest) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));

  std::string policy_digest(32, 'a');
  EXPECT_CALL(mock_trial_session_, GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_digest), Return(TPM_RC_SUCCESS)));

  trunks::TPMT_PUBLIC public_area;
  public_area.auth_policy.size = 2;
  public_area.object_attributes &= (~trunks::kUserWithAuth);
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_area), Return(TPM_RC_SUCCESS)));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}), key_blob,
      creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadAttributes) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));

  std::string policy_digest(32, 'a');
  EXPECT_CALL(mock_trial_session_, GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_digest), Return(TPM_RC_SUCCESS)));

  trunks::TPMT_PUBLIC public_area;
  public_area.auth_policy.size = policy_digest.size();
  memcpy(public_area.auth_policy.buffer, policy_digest.data(),
         policy_digest.size());
  public_area.object_attributes = trunks::kUserWithAuth;
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_area), Return(TPM_RC_SUCCESS)));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, brillo::Blob>({{index, pcr_value}}), key_blob,
      creation_blob));
}

TEST_F(Tpm2Test, ExtendPCRSuccess) {
  const uint32_t index = 5;
  const std::string extension = "extension";
  EXPECT_CALL(mock_tpm_utility_, ExtendPCR(index, extension, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, ExtendPCRForCSME(index, extension))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(tpm_->ExtendPCR(index, BlobFromString(extension)));
}

TEST_F(Tpm2Test, ExtendPCRFailureTPM) {
  uint32_t index = 5;
  const std::string extension = "extension";
  EXPECT_CALL(mock_tpm_utility_, ExtendPCR(index, extension, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_CALL(mock_tpm_utility_, ExtendPCRForCSME(index, extension))
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  EXPECT_FALSE(tpm_->ExtendPCR(index, BlobFromString(extension)));
}

TEST_F(Tpm2Test, ExtendPCRFailureCSME) {
  uint32_t index = 5;
  const std::string extension = "extension";
  EXPECT_CALL(mock_tpm_utility_, ExtendPCR(index, extension, _))
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, ExtendPCRForCSME(index, extension))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->ExtendPCR(index, BlobFromString(extension)));
}

TEST_F(Tpm2Test, ReadPCRSuccess) {
  uint32_t index = 5;
  Blob pcr_value;
  std::string pcr_digest("digest");
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(index, _))
      .WillOnce(DoAll(SetArgPointee<1>(pcr_digest), Return(TPM_RC_SUCCESS)));
  EXPECT_TRUE(tpm_->ReadPCR(index, &pcr_value));
  EXPECT_EQ(BlobFromString(pcr_digest), pcr_value);
}

TEST_F(Tpm2Test, ReadPCRFailure) {
  uint32_t index = 5;
  Blob pcr_value;
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(index, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->ReadPCR(index, &pcr_value));
}

TEST_F(Tpm2Test, WrapRsaKeySuccess) {
  std::string key_blob("key_blob");
  SecureBlob modulus;
  SecureBlob prime_factor;
  EXPECT_CALL(mock_tpm_utility_, ImportRSAKey(_, _, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<6>(key_blob), Return(TPM_RC_SUCCESS)));
  SecureBlob wrapped_key;
  EXPECT_TRUE(tpm_->WrapRsaKey(modulus, prime_factor, &wrapped_key));
  EXPECT_EQ(key_blob, wrapped_key.to_string());
}

TEST_F(Tpm2Test, WrapRsaKeyFailure) {
  SecureBlob wrapped_key;
  EXPECT_CALL(mock_tpm_utility_, ImportRSAKey(_, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->WrapRsaKey(SecureBlob(), SecureBlob(), &wrapped_key));
}

TEST_F(Tpm2Test, CreateWrappedEccKeySuccess) {
  std::string key_blob("key_blob");
  EXPECT_CALL(mock_tpm_utility_, CreateECCKeyPair(_, _, _, _, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<7>(key_blob), Return(TPM_RC_SUCCESS)));
  SecureBlob wrapped_key;
  EXPECT_TRUE(tpm_->CreateWrappedEccKey(&wrapped_key));
  EXPECT_EQ(key_blob, wrapped_key.to_string());
}

TEST_F(Tpm2Test, CreateWrappedEccKeyFailure) {
  SecureBlob wrapped_key;
  EXPECT_CALL(mock_tpm_utility_, CreateECCKeyPair(_, _, _, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->CreateWrappedEccKey(&wrapped_key));
}

TEST_F(Tpm2Test, LoadWrappedKeySuccess) {
  SecureBlob wrapped_key("wrapped_key");
  trunks::TPM_HANDLE handle = trunks::TPM_RH_FIRST;
  std::string loaded_key;
  ScopedKeyHandle key_handle;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillOnce(DoAll(SaveArg<0>(&loaded_key), SetArgPointee<2>(handle),
                      Return(TPM_RC_SUCCESS)));
  EXPECT_EQ(tpm_->LoadWrappedKey(wrapped_key, &key_handle), nullptr);
  EXPECT_EQ(handle, key_handle.value());
  EXPECT_EQ(loaded_key, wrapped_key.to_string());
}

TEST_F(Tpm2Test, LoadWrappedKeyFailure) {
  SecureBlob wrapped_key("wrapped_key");
  ScopedKeyHandle key_handle;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  hwsec::Status err = tpm_->LoadWrappedKey(wrapped_key, &key_handle);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, LoadWrappedKeyTransientDevWriteFailure) {
  SecureBlob wrapped_key("wrapped_key");
  ScopedKeyHandle key_handle;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillRepeatedly(Return(trunks::TRUNKS_RC_WRITE_ERROR));
  hwsec::Status err = tpm_->LoadWrappedKey(wrapped_key, &key_handle);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kLater, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, LoadWrappedKeyRetryActions) {
  constexpr TPM_RC error_code_fmt0 = trunks::TPM_RC_REFERENCE_H0;
  constexpr TPM_RC error_code_fmt1 = trunks::TPM_RC_HANDLE | trunks::TPM_RC_2;
  SecureBlob wrapped_key("wrapped_key");
  ScopedKeyHandle key_handle;
  // For hardware TPM and Resource Manager, should use the error number to
  // determine the corresponding retry action.
  for (TPM_RC layer_code : {trunks::kResourceManagerTpmErrorBase, TPM_RC(0)}) {
    EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
        .WillOnce(Return(error_code_fmt0 | layer_code))
        .WillOnce(Return(error_code_fmt1 | layer_code))
        .RetiresOnSaturation();
    hwsec::Status err = tpm_->LoadWrappedKey(wrapped_key, &key_handle);
    EXPECT_NE(nullptr, err);
    EXPECT_EQ(TPMRetryAction::kLater, err->ToTPMRetryAction());
    err = tpm_->LoadWrappedKey(wrapped_key, &key_handle);
    EXPECT_NE(nullptr, err);
    EXPECT_EQ(TPMRetryAction::kLater, err->ToTPMRetryAction());
  }
  // For response codes produced by other layers (e.g. trunks, SAPI), should
  // always return FailNoRetry, even if lower 12 bits match hardware TPM errors.
  for (TPM_RC layer_code : {trunks::kSapiErrorBase, trunks::kTrunksErrorBase}) {
    EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
        .WillOnce(Return(error_code_fmt0 | layer_code))
        .WillOnce(Return(error_code_fmt1 | layer_code))
        .RetiresOnSaturation();
    hwsec::Status err = tpm_->LoadWrappedKey(wrapped_key, &key_handle);
    EXPECT_NE(nullptr, err);
    EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
    err = tpm_->LoadWrappedKey(wrapped_key, &key_handle);
    EXPECT_NE(nullptr, err);
    EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
  }
}

TEST_F(Tpm2Test, CloseHandle) {
  TpmKeyHandle key_handle = 42;
  EXPECT_CALL(mock_tpm_, FlushContext(key_handle, _, _)).Times(1);
  tpm_->CloseHandle(key_handle);
}

TEST_F(Tpm2Test, EncryptBlobSuccess) {
  TpmKeyHandle handle = 42;
  std::string tpm_ciphertext(32, 'a');
  SecureBlob key(32, 'b');
  SecureBlob plaintext("plaintext");
  EXPECT_CALL(mock_tpm_utility_, AsymmetricEncrypt(handle, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<5>(tpm_ciphertext), Return(TPM_RC_SUCCESS)));
  SecureBlob ciphertext;
  EXPECT_EQ(nullptr, tpm_->EncryptBlob(handle, plaintext, key, &ciphertext));
}

TEST_F(Tpm2Test, EncryptBlobBadAesKey) {
  TpmKeyHandle handle = 42;
  std::string tpm_ciphertext(32, 'a');
  SecureBlob key(16, 'b');
  SecureBlob plaintext("plaintext");
  EXPECT_CALL(mock_tpm_utility_, AsymmetricEncrypt(handle, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<5>(tpm_ciphertext), Return(TPM_RC_SUCCESS)));
  SecureBlob ciphertext;
  hwsec::Status err = tpm_->EncryptBlob(handle, plaintext, key, &ciphertext);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, EncryptBlobBadTpmEncrypt) {
  TpmKeyHandle handle = 42;
  std::string tpm_ciphertext(16, 'a');
  SecureBlob key(32, 'b');
  SecureBlob plaintext("plaintext");
  EXPECT_CALL(mock_tpm_utility_, AsymmetricEncrypt(handle, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<5>(tpm_ciphertext), Return(TPM_RC_SUCCESS)));
  SecureBlob ciphertext;
  hwsec::Status err = tpm_->EncryptBlob(handle, plaintext, key, &ciphertext);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, EncryptBlobFailure) {
  TpmKeyHandle handle = 42;
  SecureBlob key(32, 'b');
  SecureBlob plaintext("plaintext");
  EXPECT_CALL(mock_tpm_utility_, AsymmetricEncrypt(handle, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  SecureBlob ciphertext;
  hwsec::Status err = tpm_->EncryptBlob(handle, plaintext, key, &ciphertext);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, DecryptBlobSuccess) {
  TpmKeyHandle handle = 42;
  SecureBlob key(32, 'a');
  SecureBlob ciphertext(32, 'b');
  std::string tpm_plaintext("plaintext");
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(handle, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(tpm_plaintext), Return(TPM_RC_SUCCESS)));
  SecureBlob plaintext;
  EXPECT_EQ(nullptr, tpm_->DecryptBlob(handle, ciphertext, key, &plaintext));
}

TEST_F(Tpm2Test, DecryptBlobBadAesKey) {
  TpmKeyHandle handle = 42;
  SecureBlob key(16, 'a');
  SecureBlob ciphertext(32, 'b');
  SecureBlob plaintext;
  hwsec::Status err = tpm_->DecryptBlob(handle, ciphertext, key, &plaintext);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, DecryptBlobBadCiphertext) {
  TpmKeyHandle handle = 42;
  SecureBlob key(32, 'a');
  SecureBlob ciphertext(16, 'b');
  SecureBlob plaintext;
  hwsec::Status err = tpm_->DecryptBlob(handle, ciphertext, key, &plaintext);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, DecryptBlobFailure) {
  TpmKeyHandle handle = 42;
  SecureBlob key(32, 'a');
  SecureBlob ciphertext(32, 'b');
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(handle, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  SecureBlob plaintext;
  hwsec::Status err = tpm_->DecryptBlob(handle, ciphertext, key, &plaintext);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, GetAuthValueSuccess) {
  TpmKeyHandle handle = 42;
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(handle, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  SecureBlob pass_blob(256, 'a');
  SecureBlob auth_value;
  EXPECT_EQ(nullptr, tpm_->GetAuthValue(std::optional<TpmKeyHandle>(handle),
                                        pass_blob, &auth_value));
}

TEST_F(Tpm2Test, GetAuthValueFailedWithAuthorizationBadAuthSize) {
  TpmKeyHandle handle = 42;
  SecureBlob pass_blob(128, 'a');
  SecureBlob auth_value;
  EXPECT_NE(nullptr, tpm_->GetAuthValue(std::optional<TpmKeyHandle>(handle),
                                        pass_blob, &auth_value));
}

TEST_F(Tpm2Test, GetAuthValueFailed) {
  TpmKeyHandle handle = 42;
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(handle, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  SecureBlob pass_blob(256, 'a');
  SecureBlob auth_value;
  EXPECT_NE(nullptr, tpm_->GetAuthValue(std::optional<TpmKeyHandle>(handle),
                                        pass_blob, &auth_value));
}

TEST_F(Tpm2Test, GetEccAuthValueSuccess) {
  TpmKeyHandle handle = 42;

  trunks::TPMS_ECC_POINT ecc_point;
  ecc_point.x = trunks::Make_TPM2B_ECC_PARAMETER(std::string(32, 0xcc));
  ecc_point.y = trunks::Make_TPM2B_ECC_PARAMETER(std::string(32, 0xbb));
  trunks::TPM2B_ECC_POINT out_point = trunks::Make_TPM2B_ECC_POINT(ecc_point);

  EXPECT_CALL(mock_tpm_utility_, ECDHZGen(handle, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(out_point), Return(TPM_RC_SUCCESS)));
  SecureBlob pass_blob(256, 'a');
  SecureBlob auth_value;
  EXPECT_EQ(nullptr, tpm_->GetEccAuthValue(std::optional<TpmKeyHandle>(handle),
                                           pass_blob, &auth_value));
}

TEST_F(Tpm2Test, GetEccAuthValueFailedWithAuthorizationBadAuthSize) {
  TpmKeyHandle handle = 42;
  SecureBlob pass_blob(16, 'a');
  SecureBlob auth_value;
  hwsec::Status err = tpm_->GetEccAuthValue(std::optional<TpmKeyHandle>(handle),
                                            pass_blob, &auth_value);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, GetEccAuthValueFailed) {
  TpmKeyHandle handle = 42;
  EXPECT_CALL(mock_tpm_utility_, ECDHZGen(handle, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  SecureBlob pass_blob(256, 'a');
  SecureBlob auth_value;
  hwsec::Status err = tpm_->GetEccAuthValue(std::optional<TpmKeyHandle>(handle),
                                            pass_blob, &auth_value);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, GetEccAuthValueScalarOutOfRange) {
  const std::string kOorStr =
      "AD1FE60D4FF828511B829DA029F98A1A164C4C946776AC1A4DEF3D490371BB66";
  TpmKeyHandle handle = 42;
  SecureBlob pass_blob;
  EXPECT_TRUE(brillo::SecureBlob::HexStringToSecureBlob(kOorStr, &pass_blob));
  SecureBlob auth_value;
  hwsec::Status err = tpm_->GetEccAuthValue(std::optional<TpmKeyHandle>(handle),
                                            pass_blob, &auth_value);

  EXPECT_NE(nullptr, err);
  EXPECT_EQ(err->ToTPMRetryAction(),
            TPMRetryAction::kEllipticCurveScalarOutOfRange);
}

TEST_F(Tpm2Test, SealToPcrWithAuthorizationSuccess) {
  SecureBlob auth_value(256, 'a');
  SecureBlob plaintext(32, 'b');
  EXPECT_CALL(mock_tpm_utility_,
              SealData(plaintext.to_string(), _, _,
                       /*require_admin_with_policy=*/true, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  SecureBlob sealed_data;
  EXPECT_EQ(nullptr, tpm_->SealToPcrWithAuthorization(
                         plaintext, auth_value,
                         std::map<uint32_t, brillo::Blob>(), &sealed_data));
}

TEST_F(Tpm2Test, UnsealWithAuthorizationSuccess) {
  SecureBlob auth_value(256, 'a');
  SecureBlob sealed_data(32, 'b');
  EXPECT_CALL(mock_tpm_utility_, UnsealData(sealed_data.to_string(), _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  SecureBlob plaintext;
  EXPECT_EQ(nullptr, tpm_->UnsealWithAuthorization(
                         std::nullopt, sealed_data, auth_value,
                         std::map<uint32_t, brillo::Blob>(), &plaintext));
}

TEST_F(Tpm2Test, UnsealWithAuthorizationWithPreloadSuccess) {
  TpmKeyHandle preload_handle = 87;
  SecureBlob auth_value(256, 'a');
  SecureBlob sealed_data(32, 'b');
  EXPECT_CALL(mock_tpm_utility_, UnsealDataWithHandle(preload_handle, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  SecureBlob plaintext;
  hwsec::Status err = tpm_->UnsealWithAuthorization(
      preload_handle, sealed_data, auth_value,
      std::map<uint32_t, brillo::Blob>(), &plaintext);
  EXPECT_EQ(nullptr, err);
}

TEST_F(Tpm2Test, GetPublicKeyHashSuccess) {
  TpmKeyHandle handle = 42;
  trunks::TPMT_PUBLIC public_data;
  SecureBlob public_key("hello");
  public_data.unique.rsa =
      trunks::Make_TPM2B_PUBLIC_KEY_RSA(public_key.to_string());
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  SecureBlob public_key_hash;
  EXPECT_EQ(nullptr, tpm_->GetPublicKeyHash(handle, &public_key_hash));
  SecureBlob expected_key_hash = Sha256(public_key);
  EXPECT_EQ(expected_key_hash, public_key_hash);
}

TEST_F(Tpm2Test, GetPublicKeyHashFailure) {
  TpmKeyHandle handle = 42;
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(handle, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  SecureBlob public_key_hash;
  hwsec::Status err = tpm_->GetPublicKeyHash(handle, &public_key_hash);
  EXPECT_NE(nullptr, err);
  EXPECT_EQ(TPMRetryAction::kNoRetry, err->ToTPMRetryAction());
}

TEST_F(Tpm2Test, DeclareTpmFirmwareStable) {
  EXPECT_CALL(mock_tpm_utility_, DeclareTpmFirmwareStable())
      .Times(2)
      .WillOnce(Return(TPM_RC_FAILURE))
      .WillOnce(Return(TPM_RC_SUCCESS));
  // First attempt shall call TpmUtility since we haven't called it yet.
  tpm_->DeclareTpmFirmwareStable();
  // Second attempt shall call TpmUtility since the first attempt failed.
  tpm_->DeclareTpmFirmwareStable();
  // Subsequent attempts shall do nothing since we already succeeded on the
  // second attempt.
  tpm_->DeclareTpmFirmwareStable();
  tpm_->DeclareTpmFirmwareStable();
}

TEST_F(Tpm2Test, RemoveOwnerDependencySuccess) {
  std::string dependency;
  EXPECT_CALL(mock_tpm_manager_utility_, RemoveOwnerDependency(_))
      .WillOnce(DoAll(SaveArg<0>(&dependency), Return(true)));
  EXPECT_TRUE(
      tpm_->RemoveOwnerDependency(Tpm::TpmOwnerDependency::kInstallAttributes));
  EXPECT_EQ(tpm_manager::kTpmOwnerDependency_Nvram, dependency);
  EXPECT_CALL(mock_tpm_manager_utility_, RemoveOwnerDependency(_))
      .WillOnce(DoAll(SaveArg<0>(&dependency), Return(true)));
  EXPECT_TRUE(
      tpm_->RemoveOwnerDependency(Tpm::TpmOwnerDependency::kAttestation));
  EXPECT_EQ(tpm_manager::kTpmOwnerDependency_Attestation, dependency);
}

TEST_F(Tpm2Test, RemoveOwnerDependencyFailure) {
  std::string dependency;
  EXPECT_CALL(mock_tpm_manager_utility_, RemoveOwnerDependency(_))
      .WillOnce(DoAll(SaveArg<0>(&dependency), Return(false)));
  EXPECT_FALSE(
      tpm_->RemoveOwnerDependency(Tpm::TpmOwnerDependency::kInstallAttributes));
  EXPECT_EQ(tpm_manager::kTpmOwnerDependency_Nvram, dependency);
}

TEST_F(Tpm2Test, IsOwnerPasswordPresentSuccess) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_TRUE(tpm_->IsOwnerPasswordPresent());
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(false), Return(true)));
  EXPECT_FALSE(tpm_->IsOwnerPasswordPresent());
}

TEST_F(Tpm2Test, IsOwnerPasswordPresentFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(tpm_->IsOwnerPasswordPresent());
}

TEST_F(Tpm2Test, HasResetLockPermissionsSuccess) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(true)));
  EXPECT_TRUE(tpm_->HasResetLockPermissions());
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(false), Return(true)));
  EXPECT_FALSE(tpm_->HasResetLockPermissions());
}

TEST_F(Tpm2Test, HasResetLockPermissionsFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(tpm_->HasResetLockPermissions());
}

}  // namespace cryptohome
