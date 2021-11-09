// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for TpmImpl.

#include "cryptohome/tpm_impl.h"

#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>
#include <libhwsec/test_utils/tpm1/test_fixture.h>
#include <tpm_manager/client/mock_tpm_manager_utility.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>

#include <base/macros.h>

namespace {

using ::testing::_;
using ::testing::ByRef;
using ::testing::DoAll;
using ::testing::ElementsAreArray;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

using brillo::Blob;
using brillo::BlobToString;
using brillo::SecureBlob;
using tpm_manager::LocalData;
using tpm_manager::MockTpmManagerUtility;

}  // namespace

namespace cryptohome {

class TpmImplTest : public ::hwsec::Tpm1HwsecTest {
 public:
  TpmImplTest() = default;
  ~TpmImplTest() override = default;

  void SetUp() override {
    tpm_.SetTpmManagerUtilityForTesting(&mock_tpm_manager_utility_);
  }

 protected:
  NiceMock<MockTpmManagerUtility> mock_tpm_manager_utility_;
  TpmImpl tpm_;
  Tpm* GetTpm() { return &tpm_; }
};

TEST_F(TpmImplTest, GetPcrMapNotExtended) {
  std::string obfuscated_username = "OBFUSCATED_USER";
  std::map<uint32_t, std::string> result =
      GetTpm()->GetPcrMap(obfuscated_username, /*use_extended_pcr=*/false);

  EXPECT_EQ(1, result.size());
  const std::string& result_str = result[kTpmSingleUserPCR];

  std::string expected_result(SHA_DIGEST_LENGTH, 0);
  EXPECT_EQ(expected_result, result_str);
}

TEST_F(TpmImplTest, GetPcrMapExtended) {
  std::string obfuscated_username = "OBFUSCATED_USER";
  std::map<uint32_t, std::string> result =
      GetTpm()->GetPcrMap(obfuscated_username, /*use_extended_pcr=*/true);

  EXPECT_EQ(1, result.size());
  const std::string& result_str = result[kTpmSingleUserPCR];

  // Pre-calculated expected result.
  unsigned char expected_result_bytes[] = {
      0x94, 0xce, 0x1b, 0x97, 0x40, 0xfd, 0x5b, 0x1e, 0x8c, 0x64,
      0xb0, 0xd5, 0x38, 0xac, 0x88, 0xb5, 0xb4, 0x52, 0x4f, 0x67};
  std::string expected_result(reinterpret_cast<char*>(expected_result_bytes),
                              std::size(expected_result_bytes));
  EXPECT_EQ(expected_result, result_str);
}

TEST_F(TpmImplTest, TakeOwnership) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_tpm_manager_utility_, TakeOwnership())
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->TakeOwnership(0, SecureBlob{}));
  EXPECT_CALL(mock_tpm_manager_utility_, TakeOwnership())
      .WillOnce(Return(true));
  EXPECT_TRUE(GetTpm()->TakeOwnership(0, SecureBlob{}));

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(true), Return(true)));
  EXPECT_CALL(mock_tpm_manager_utility_, TakeOwnership()).Times(0);
  EXPECT_TRUE(GetTpm()->TakeOwnership(0, SecureBlob{}));
}

TEST_F(TpmImplTest, Enabled) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(0);
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->IsEnabled());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(false), Return(true)));
  EXPECT_FALSE(GetTpm()->IsEnabled());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(true), Return(true)));
  EXPECT_TRUE(GetTpm()->IsEnabled());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(0);
  EXPECT_TRUE(GetTpm()->IsEnabled());
}

TEST_F(TpmImplTest, OwnedWithoutSignal) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->IsOwned());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(false), Return(true)));
  EXPECT_FALSE(GetTpm()->IsOwned());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(true), Return(true)));
  EXPECT_TRUE(GetTpm()->IsOwned());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(0);
  EXPECT_TRUE(GetTpm()->IsOwned());
}

TEST_F(TpmImplTest, GetDelegateWithoutSignal) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillRepeatedly(Return(false));
  Blob result_blob;
  Blob result_secret;
  bool result_has_reset_lock_permissions = false;
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                     &result_has_reset_lock_permissions));
  LocalData expected_local_data;

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<0>(true), SetArgPointee<1>(true),
                            SetArgPointee<2>(ByRef(expected_local_data)),
                            Return(true)));
  EXPECT_FALSE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                     &result_has_reset_lock_permissions));

  expected_local_data.mutable_owner_delegate()->set_blob("blob");
  expected_local_data.mutable_owner_delegate()->set_secret("secret");
  expected_local_data.mutable_owner_delegate()->set_has_reset_lock_permissions(
      true);
  EXPECT_TRUE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                    &result_has_reset_lock_permissions));
  EXPECT_THAT(result_blob,
              ElementsAreArray(expected_local_data.owner_delegate().blob()));
  EXPECT_THAT(result_secret,
              ElementsAreArray(expected_local_data.owner_delegate().secret()));
  EXPECT_TRUE(result_has_reset_lock_permissions);
}

TEST_F(TpmImplTest, GetDictionaryAttackInfo) {
  int result_counter = 0;
  int result_threshold = 0;
  bool result_lockout = false;
  int result_seconds_remaining = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, GetDictionaryAttackInfo(_, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->GetDictionaryAttackInfo(
      &result_counter, &result_threshold, &result_lockout,
      &result_seconds_remaining));

  EXPECT_CALL(mock_tpm_manager_utility_, GetDictionaryAttackInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(123), SetArgPointee<1>(456),
                      SetArgPointee<2>(true), SetArgPointee<3>(789),
                      Return(true)));
  EXPECT_TRUE(GetTpm()->GetDictionaryAttackInfo(
      &result_counter, &result_threshold, &result_lockout,
      &result_seconds_remaining));
  EXPECT_EQ(result_counter, 123);
  EXPECT_EQ(result_threshold, 456);
  EXPECT_TRUE(result_lockout);
  EXPECT_EQ(result_seconds_remaining, 789);
}

TEST_F(TpmImplTest, ResetDictionaryAttackMitigation) {
  EXPECT_CALL(mock_tpm_manager_utility_, ResetDictionaryAttackLock())
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->ResetDictionaryAttackMitigation(Blob{}, Blob{}));
  EXPECT_CALL(mock_tpm_manager_utility_, ResetDictionaryAttackLock())
      .WillOnce(Return(true));
  EXPECT_TRUE(GetTpm()->ResetDictionaryAttackMitigation(Blob{}, Blob{}));
}

TEST_F(TpmImplTest, SignalCache) {
  brillo::Blob result_blob, result_secret;
  bool result_has_reset_lock_permissions;
  ON_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillByDefault(Return(false));

  ON_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillByDefault(Return(false));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(1);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(1);
  EXPECT_FALSE(GetTpm()->IsOwned());

  // |GetDelegate| doesn't fully rely on the signal. Thus, expects to call
  // |GetTpmStatus| but not |GetOwnershipTakenSignalStatus| when the auth
  // delegate is not found.
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(1);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(0);
  EXPECT_FALSE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                     &result_has_reset_lock_permissions));

  ON_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillByDefault(DoAll(SetArgPointee<0>(false), Return(true)));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(2);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(1);
  EXPECT_FALSE(GetTpm()->IsOwned());
  EXPECT_FALSE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                     &result_has_reset_lock_permissions));

  ON_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillByDefault(
          DoAll(SetArgPointee<0>(true), SetArgPointee<1>(false), Return(true)));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(1);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(1);
  EXPECT_FALSE(GetTpm()->IsOwned());
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(1);
  EXPECT_FALSE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                     &result_has_reset_lock_permissions));

  tpm_manager::LocalData expected_local_data;
  expected_local_data.set_owner_password("owner password");
  expected_local_data.mutable_owner_delegate()->set_blob("blob");
  expected_local_data.mutable_owner_delegate()->set_secret("secret");
  expected_local_data.mutable_owner_delegate()->set_has_reset_lock_permissions(
      true);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(true), SetArgPointee<1>(true),
                      SetArgPointee<2>(expected_local_data), Return(true)));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(0);
  EXPECT_TRUE(GetTpm()->IsOwned());
  EXPECT_TRUE(GetTpm()->IsEnabled());
  EXPECT_TRUE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                    &result_has_reset_lock_permissions));
  EXPECT_THAT(result_blob,
              ElementsAreArray(expected_local_data.owner_delegate().blob()));
  EXPECT_THAT(result_secret,
              ElementsAreArray(expected_local_data.owner_delegate().secret()));
  EXPECT_EQ(result_has_reset_lock_permissions,
            expected_local_data.owner_delegate().has_reset_lock_permissions());
}

TEST_F(TpmImplTest, RemoveTpmOwnerDependency) {
  EXPECT_CALL(mock_tpm_manager_utility_,
              RemoveOwnerDependency(tpm_manager::kTpmOwnerDependency_Nvram))
      .WillOnce(Return(true));
  EXPECT_TRUE(GetTpm()->RemoveOwnerDependency(
      Tpm::TpmOwnerDependency::kInstallAttributes));
  EXPECT_CALL(
      mock_tpm_manager_utility_,
      RemoveOwnerDependency(tpm_manager::kTpmOwnerDependency_Attestation))
      .WillOnce(Return(false));
  EXPECT_FALSE(
      GetTpm()->RemoveOwnerDependency(Tpm::TpmOwnerDependency::kAttestation));
}

TEST_F(TpmImplTest, RemoveTpmOwnerDependencyInvalidEnum) {
  EXPECT_DEBUG_DEATH(GetTpm()->RemoveOwnerDependency(
                         static_cast<Tpm::TpmOwnerDependency>(999)),
                     ".*Unexpected enum class value: 999");
}

TEST_F(TpmImplTest, ClearStoredPassword) {
  EXPECT_CALL(mock_tpm_manager_utility_, ClearStoredOwnerPassword())
      .WillOnce(Return(true));
  EXPECT_TRUE(GetTpm()->ClearStoredPassword());
  EXPECT_CALL(mock_tpm_manager_utility_, ClearStoredOwnerPassword())
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->ClearStoredPassword());
}

TEST_F(TpmImplTest, GetVersionInfoCache) {
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
  EXPECT_FALSE(GetTpm()->GetVersionInfo(&actual_version_info));

  // Requests from tpm_manager, succeeded, cached
  EXPECT_TRUE(GetTpm()->GetVersionInfo(&actual_version_info));
  EXPECT_EQ(expected_version_info.GetFingerprint(),
            actual_version_info.GetFingerprint());

  // Returns from cache
  EXPECT_TRUE(GetTpm()->GetVersionInfo(&actual_version_info));
  EXPECT_EQ(expected_version_info.GetFingerprint(),
            actual_version_info.GetFingerprint());
}

TEST_F(TpmImplTest, GetVersionInfoBadInput) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetVersionInfo(_, _, _, _, _, _))
      .Times(0);
  EXPECT_FALSE(GetTpm()->GetVersionInfo(nullptr));
}

TEST_F(TpmImplTest, BadTpmManagerUtility) {
  EXPECT_CALL(mock_tpm_manager_utility_, Initialize())
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(GetTpm()->TakeOwnership(0, SecureBlob{}));
  EXPECT_FALSE(GetTpm()->IsEnabled());
  EXPECT_FALSE(GetTpm()->IsOwned());
  EXPECT_FALSE(GetTpm()->ResetDictionaryAttackMitigation(Blob{}, Blob{}));
  int result_counter;
  int result_threshold;
  bool result_lockout;
  int result_seconds_remaining;
  EXPECT_FALSE(GetTpm()->GetDictionaryAttackInfo(
      &result_counter, &result_threshold, &result_lockout,
      &result_seconds_remaining));
  Blob result_blob;
  Blob result_secret;
  bool result_has_reset_lock_permissions;
  EXPECT_FALSE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                     &result_has_reset_lock_permissions));
}

TEST_F(TpmImplTest, DefineNvramSuccess) {
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
  EXPECT_TRUE(
      GetTpm()->DefineNvram(kIndex, kLength, Tpm::kTpmNvramWriteDefine));
  EXPECT_EQ(kIndex, index);
  EXPECT_EQ(kLength, length);
  ASSERT_TRUE(write_define);
  ASSERT_FALSE(bind_to_pcr0);
  ASSERT_FALSE(firmware_readable);
}

TEST_F(TpmImplTest, DefineNvramSuccessWithPolicy) {
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
  EXPECT_TRUE(GetTpm()->DefineNvram(
      kIndex, kLength, Tpm::kTpmNvramWriteDefine | Tpm::kTpmNvramBindToPCR0));
  EXPECT_EQ(kIndex, index);
  EXPECT_EQ(kLength, length);
  ASSERT_TRUE(write_define);
  ASSERT_TRUE(bind_to_pcr0);
  ASSERT_FALSE(firmware_readable);
}

TEST_F(TpmImplTest, DefineNvramSuccessFirmwareReadable) {
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
  EXPECT_TRUE(GetTpm()->DefineNvram(
      kIndex, kLength,
      Tpm::kTpmNvramWriteDefine | Tpm::kTpmNvramFirmwareReadable));
  EXPECT_EQ(kIndex, index);
  EXPECT_EQ(kLength, length);
  ASSERT_TRUE(write_define);
  ASSERT_FALSE(bind_to_pcr0);
  ASSERT_TRUE(firmware_readable);
}

TEST_F(TpmImplTest, DefineNvramFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, DefineSpace(_, _, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->DefineNvram(0, 0, 0));
}

TEST_F(TpmImplTest, DestroyNvramSuccess) {
  constexpr uint32_t kIndex = 2;
  uint32_t index = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, DestroySpace(_))
      .WillOnce(DoAll(SaveArg<0>(&index), Return(true)));
  EXPECT_TRUE(GetTpm()->DestroyNvram(kIndex));
  EXPECT_EQ(kIndex, index);
}

TEST_F(TpmImplTest, DestroyNvramFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, DestroySpace(_))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->DestroyNvram(0));
}

TEST_F(TpmImplTest, WriteNvram) {
  constexpr uint32_t kIndex = 2;
  const std::string kData("nvram_data");

  EXPECT_CALL(mock_tpm_manager_utility_,
              WriteSpace(kIndex, kData, /*use_owner_auth=*/false))
      .WillOnce(Return(true));
  EXPECT_TRUE(GetTpm()->WriteNvram(kIndex, SecureBlob(kData)));

  EXPECT_CALL(mock_tpm_manager_utility_,
              WriteSpace(kIndex, kData, /*use_owner_auth=*/false))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->WriteNvram(kIndex, SecureBlob(kData)));
}

TEST_F(TpmImplTest, OwnerWriteNvramAlwaysReturnFalse) {
  EXPECT_FALSE(GetTpm()->OwnerWriteNvram(0, SecureBlob()));
}

TEST_F(TpmImplTest, WriteLockNvramSuccess) {
  constexpr uint32_t kIndex = 2;
  uint32_t index = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, LockSpace(_))
      .WillOnce(DoAll(SaveArg<0>(&index), Return(true)));
  EXPECT_TRUE(GetTpm()->WriteLockNvram(kIndex));
  EXPECT_EQ(kIndex, index);
}

TEST_F(TpmImplTest, WriteLockNvramFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, LockSpace(_)).WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->WriteLockNvram(0));
}

TEST_F(TpmImplTest, ReadNvramSuccess) {
  constexpr uint32_t kIndex = 2;
  constexpr bool kUserOwnerAuth = false;
  const std::string nvram_data("nvram_data");
  uint32_t index = 0;
  bool user_owner_auth = false;
  SecureBlob read_data;
  EXPECT_CALL(mock_tpm_manager_utility_, ReadSpace(_, _, _))
      .WillOnce(DoAll(SaveArg<0>(&index), SaveArg<1>(&user_owner_auth),
                      SetArgPointee<2>(nvram_data), Return(true)));
  EXPECT_TRUE(GetTpm()->ReadNvram(kIndex, &read_data));
  EXPECT_EQ(index, kIndex);
  EXPECT_EQ(user_owner_auth, kUserOwnerAuth);
  EXPECT_EQ(nvram_data, read_data.to_string());
}

TEST_F(TpmImplTest, ReadNvramFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, ReadSpace(_, _, _))
      .WillOnce(Return(false));
  SecureBlob read_data;
  EXPECT_FALSE(GetTpm()->ReadNvram(0, &read_data));
}

TEST_F(TpmImplTest, IsNvramDefinedSuccess) {
  constexpr uint32_t kIndex = 2;
  std::vector<uint32_t> spaces;
  spaces.push_back(kIndex);
  EXPECT_CALL(mock_tpm_manager_utility_, ListSpaces(_))
      .WillOnce(DoAll(SetArgPointee<0>(spaces), Return(true)));
  EXPECT_TRUE(GetTpm()->IsNvramDefined(kIndex));
}

TEST_F(TpmImplTest, IsNvramDefinedFailure) {
  constexpr uint32_t kIndex = 2;
  EXPECT_CALL(mock_tpm_manager_utility_, ListSpaces(_)).WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->IsNvramDefined(kIndex));
}

TEST_F(TpmImplTest, IsNvramDefinedUnknownHandle) {
  constexpr uint32_t kIndex = 2;
  std::vector<uint32_t> spaces;
  spaces.push_back(kIndex);
  EXPECT_CALL(mock_tpm_manager_utility_, ListSpaces(_))
      .WillOnce(DoAll(SetArgPointee<0>(spaces), Return(true)));
  EXPECT_FALSE(GetTpm()->IsNvramDefined(kIndex + 1));
}

TEST_F(TpmImplTest, IsNvramLockedSuccess) {
  constexpr uint32_t kIndex = 2;
  constexpr uint32_t kSize = 5;
  constexpr uint32_t kIsReadLocked = false;
  constexpr uint32_t kIsWriteLocked = true;
  uint32_t index = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&index), SetArgPointee<1>(kSize),
                      SetArgPointee<2>(kIsReadLocked),
                      SetArgPointee<3>(kIsWriteLocked), Return(true)));
  EXPECT_TRUE(GetTpm()->IsNvramLocked(kIndex));
  EXPECT_EQ(kIndex, index);
}

TEST_F(TpmImplTest, IsNvramLockedNotLocked) {
  constexpr uint32_t kIndex = 2;
  constexpr uint32_t kSize = 5;
  constexpr uint32_t kIsReadLocked = false;
  constexpr uint32_t kIsWriteLocked = false;
  uint32_t index = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&index), SetArgPointee<1>(kSize),
                      SetArgPointee<2>(kIsReadLocked),
                      SetArgPointee<3>(kIsWriteLocked), Return(true)));
  EXPECT_FALSE(GetTpm()->IsNvramLocked(kIndex));
  EXPECT_EQ(kIndex, index);
}

TEST_F(TpmImplTest, IsNvramLockedFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(_, _, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->IsNvramLocked(0));
}

TEST_F(TpmImplTest, GetNvramSizeSuccess) {
  constexpr uint32_t kIndex = 2;
  constexpr uint32_t kSize = 5;
  constexpr uint32_t kIsReadLocked = false;
  constexpr uint32_t kIsWriteLocked = true;
  uint32_t index = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&index), SetArgPointee<1>(kSize),
                      SetArgPointee<2>(kIsReadLocked),
                      SetArgPointee<3>(kIsWriteLocked), Return(true)));
  EXPECT_EQ(GetTpm()->GetNvramSize(kIndex), kSize);
  EXPECT_EQ(kIndex, index);
}

TEST_F(TpmImplTest, GetNvramSizeFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(_, _, _, _, _))
      .WillOnce(Return(false));
  EXPECT_EQ(GetTpm()->GetNvramSize(0), 0);
}

TEST_F(TpmImplTest, IsOwnerPasswordPresentSuccess) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_TRUE(GetTpm()->IsOwnerPasswordPresent());
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(false), Return(true)));
  EXPECT_FALSE(GetTpm()->IsOwnerPasswordPresent());
}

TEST_F(TpmImplTest, IsOwnerPasswordPresentFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->IsOwnerPasswordPresent());
}

TEST_F(TpmImplTest, HasResetLockPermissionsSuccess) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(true)));
  EXPECT_TRUE(GetTpm()->HasResetLockPermissions());
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(false), Return(true)));
  EXPECT_FALSE(GetTpm()->HasResetLockPermissions());
}

TEST_F(TpmImplTest, HasResetLockPermissionsFailure) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->HasResetLockPermissions());
}
}  // namespace cryptohome
