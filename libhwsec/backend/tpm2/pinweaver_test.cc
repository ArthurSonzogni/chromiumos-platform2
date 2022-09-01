// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#define __packed __attribute((packed))
#define __aligned(x) __attribute((aligned(x)))
#include <pinweaver/pinweaver_types.h>

#include "libhwsec/backend/tpm2/backend_test_base.h"

using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;
using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using tpm_manager::TpmManagerStatus;
using ErrorCode = hwsec::Backend::PinWeaver::CredentialTreeResult::ErrorCode;

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

TEST_F(BackendPinweaverTpm2Test, Reset) {
  constexpr uint32_t kLengthLabels = 14;
  constexpr uint32_t kBitsPerLevel = 2;
  constexpr uint32_t kVersion = 1;
  const std::string kFakeRoot = "fake_root";
  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverResetTree(kVersion, 2, 7, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(0), SetArgPointee<4>(kFakeRoot),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::Reset>(
      kBitsPerLevel, kLengthLabels);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->error, ErrorCode::kSuccess);
  EXPECT_EQ(result->new_root, brillo::BlobFromString(kFakeRoot));
}

TEST_F(BackendPinweaverTpm2Test, ResetFailure) {
  constexpr uint32_t kLengthLabels = 128;
  constexpr uint32_t kBitsPerLevel = 128;
  constexpr uint32_t kVersion = 1;
  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverResetTree(kVersion, 128, 1, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(PW_ERR_BITS_PER_LEVEL_INVALID),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::Reset>(
      kBitsPerLevel, kLengthLabels);

  ASSERT_FALSE(result.ok());
}

TEST_F(BackendPinweaverTpm2Test, InsertCredential) {
  constexpr uint32_t kVersion = 2;
  constexpr uint32_t kLabel = 42;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeCred = "fake_cred";
  const std::string kFakeMac = "fake_mac";
  const brillo::SecureBlob kFakeLeSecret("fake_le_secret");
  const brillo::SecureBlob kFakeHeSecret("fake_he_secret");
  const brillo::SecureBlob kFakeResetSecret("fake_reset_secret");
  const hwsec::Backend::PinWeaver::DelaySchedule kDelaySched = {
      {5, UINT32_MAX},
  };
  const uint32_t kExpirationDelay = 100;
  const std::vector<OperationPolicySetting> kPolicies = {
      OperationPolicySetting{
          .device_config_settings =
              DeviceConfigSettings{
                  .current_user =
                      DeviceConfigSettings::CurrentUserSetting{
                          .username = std::nullopt,
                      },
              },
      },
      OperationPolicySetting{
          .device_config_settings =
              DeviceConfigSettings{
                  .current_user =
                      DeviceConfigSettings::CurrentUserSetting{
                          .username = "fake_username",
                      },
              },
      },
  };
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverInsertLeaf(kVersion, kLabel, _, kFakeLeSecret,
                                  kFakeHeSecret, kFakeResetSecret, kDelaySched,
                                  _, Eq(kExpirationDelay), _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<9>(0), SetArgPointee<10>(kFakeRoot),
                      SetArgPointee<11>(kFakeCred), SetArgPointee<12>(kFakeMac),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::InsertCredential>(
      kPolicies, kLabel, kHAux, kFakeLeSecret, kFakeHeSecret, kFakeResetSecret,
      kDelaySched, kExpirationDelay);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->error, ErrorCode::kSuccess);
  EXPECT_EQ(result->new_root, brillo::BlobFromString(kFakeRoot));
  ASSERT_TRUE(result->new_cred_metadata.has_value());
  EXPECT_EQ(result->new_cred_metadata.value(),
            brillo::BlobFromString(kFakeCred));
  ASSERT_TRUE(result->new_mac.has_value());
  EXPECT_EQ(result->new_mac.value(), brillo::BlobFromString(kFakeMac));
}

TEST_F(BackendPinweaverTpm2Test, InsertCredentialUnsupportedPolicy) {
  constexpr uint32_t kVersion = 2;
  constexpr uint32_t kLabel = 42;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeCred = "fake_cred";
  const std::string kFakeMac = "fake_mac";
  const brillo::SecureBlob kFakeLeSecret("fake_le_secret");
  const brillo::SecureBlob kFakeHeSecret("fake_he_secret");
  const brillo::SecureBlob kFakeResetSecret("fake_reset_secret");
  const hwsec::Backend::PinWeaver::DelaySchedule kDelaySched = {
      {5, UINT32_MAX},
  };
  const uint32_t kExpirationDelay = 100;
  const std::vector<OperationPolicySetting> kPolicies = {
      OperationPolicySetting{.permission =
                                 Permission{
                                     .auth_value = brillo::SecureBlob("auth"),
                                 }},
  };
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::InsertCredential>(
      kPolicies, kLabel, kHAux, kFakeLeSecret, kFakeHeSecret, kFakeResetSecret,
      kDelaySched, kExpirationDelay);

  EXPECT_FALSE(result.ok());
}

TEST_F(BackendPinweaverTpm2Test, InsertCredentialV0PolicyUnsupported) {
  constexpr uint32_t kVersion = 0;
  constexpr uint32_t kLabel = 42;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeCred = "fake_cred";
  const std::string kFakeMac = "fake_mac";
  const brillo::SecureBlob kFakeLeSecret("fake_le_secret");
  const brillo::SecureBlob kFakeHeSecret("fake_he_secret");
  const brillo::SecureBlob kFakeResetSecret("fake_reset_secret");
  const hwsec::Backend::PinWeaver::DelaySchedule kDelaySched = {
      {5, UINT32_MAX},
  };
  const std::vector<OperationPolicySetting> kPolicies = {
      OperationPolicySetting{
          .device_config_settings =
              DeviceConfigSettings{
                  .current_user =
                      DeviceConfigSettings::CurrentUserSetting{
                          .username = std::nullopt,
                      },
              },
      },
      OperationPolicySetting{
          .device_config_settings =
              DeviceConfigSettings{
                  .current_user =
                      DeviceConfigSettings::CurrentUserSetting{
                          .username = "fake_username",
                      },
              },
      },
  };
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::InsertCredential>(
      kPolicies, kLabel, kHAux, kFakeLeSecret, kFakeHeSecret, kFakeResetSecret,
      kDelaySched, /*expiration_delay=*/std::nullopt);

  EXPECT_FALSE(result.ok());
}

TEST_F(BackendPinweaverTpm2Test, InsertCredentialV1ExpirationUnsupported) {
  constexpr uint32_t kVersion = 1;
  constexpr uint32_t kLabel = 42;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeCred = "fake_cred";
  const std::string kFakeMac = "fake_mac";
  const brillo::SecureBlob kFakeLeSecret("fake_le_secret");
  const brillo::SecureBlob kFakeHeSecret("fake_he_secret");
  const brillo::SecureBlob kFakeResetSecret("fake_reset_secret");
  const hwsec::Backend::PinWeaver::DelaySchedule kDelaySched = {
      {5, UINT32_MAX},
  };
  const uint32_t kExpirationDelay = 100;
  const std::vector<OperationPolicySetting> kPolicies = {
      OperationPolicySetting{
          .device_config_settings =
              DeviceConfigSettings{
                  .current_user =
                      DeviceConfigSettings::CurrentUserSetting{
                          .username = std::nullopt,
                      },
              },
      },
      OperationPolicySetting{
          .device_config_settings =
              DeviceConfigSettings{
                  .current_user =
                      DeviceConfigSettings::CurrentUserSetting{
                          .username = "fake_username",
                      },
              },
      },
  };
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::InsertCredential>(
      kPolicies, kLabel, kHAux, kFakeLeSecret, kFakeHeSecret, kFakeResetSecret,
      kDelaySched, kExpirationDelay);

  EXPECT_FALSE(result.ok());
}

TEST_F(BackendPinweaverTpm2Test, InsertCredentialNoDelay) {
  constexpr uint32_t kVersion = 2;
  constexpr uint32_t kLabel = 42;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeCred = "fake_cred";
  const std::string kFakeMac = "fake_mac";
  const brillo::SecureBlob kFakeLeSecret("fake_le_secret");
  const brillo::SecureBlob kFakeHeSecret("fake_he_secret");
  const brillo::SecureBlob kFakeResetSecret("fake_reset_secret");
  const hwsec::Backend::PinWeaver::DelaySchedule kDelaySched = {
      {5, UINT32_MAX},
  };
  const uint32_t kExpirationDelay = 100;
  const std::vector<OperationPolicySetting> kPolicies = {
      OperationPolicySetting{
          .device_config_settings =
              DeviceConfigSettings{
                  .current_user =
                      DeviceConfigSettings::CurrentUserSetting{
                          .username = std::nullopt,
                      },
              },
      },
      OperationPolicySetting{
          .device_config_settings =
              DeviceConfigSettings{
                  .current_user =
                      DeviceConfigSettings::CurrentUserSetting{
                          .username = "fake_username",
                      },
              },
      },
  };
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverInsertLeaf(kVersion, kLabel, _, kFakeLeSecret,
                                  kFakeHeSecret, kFakeResetSecret, kDelaySched,
                                  _, Eq(kExpirationDelay), _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<9>(PW_ERR_DELAY_SCHEDULE_INVALID),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::InsertCredential>(
      kPolicies, kLabel, kHAux, kFakeLeSecret, kFakeHeSecret, kFakeResetSecret,
      kDelaySched, kExpirationDelay);

  EXPECT_FALSE(result.ok());
}

TEST_F(BackendPinweaverTpm2Test, CheckCredential) {
  constexpr uint32_t kVersion = 1;
  constexpr uint32_t kLabel = 42;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeCred = "fake_cred";
  const std::string kNewCred = "new_cred";
  const std::string kFakeMac = "fake_mac";
  const brillo::SecureBlob kFakeLeSecret("fake_le_secret");
  const brillo::SecureBlob kFakeHeSecret("fake_he_secret");
  const brillo::SecureBlob kFakeResetSecret("fake_reset_secret");
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverTryAuth(kVersion, kFakeLeSecret, _, kFakeCred, _, _, _,
                               _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(0), SetArgPointee<5>(kFakeRoot),
                      SetArgPointee<7>(kFakeHeSecret),
                      SetArgPointee<8>(kFakeResetSecret),
                      SetArgPointee<9>(kNewCred), SetArgPointee<10>(kFakeMac),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::CheckCredential>(
      kLabel, kHAux, brillo::BlobFromString(kFakeCred), kFakeLeSecret);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->error, ErrorCode::kSuccess);
  EXPECT_EQ(result->new_root, brillo::BlobFromString(kFakeRoot));
  ASSERT_TRUE(result->new_cred_metadata.has_value());
  EXPECT_EQ(result->new_cred_metadata.value(),
            brillo::BlobFromString(kNewCred));
  ASSERT_TRUE(result->new_mac.has_value());
  EXPECT_EQ(result->new_mac.value(), brillo::BlobFromString(kFakeMac));
  ASSERT_TRUE(result->he_secret.has_value());
  EXPECT_EQ(result->he_secret.value(), kFakeHeSecret);
  ASSERT_TRUE(result->reset_secret.has_value());
  EXPECT_EQ(result->reset_secret.value(), kFakeResetSecret);
}

TEST_F(BackendPinweaverTpm2Test, CheckCredentialAuthFail) {
  constexpr uint32_t kVersion = 1;
  constexpr uint32_t kLabel = 42;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeCred = "fake_cred";
  const std::string kNewCred = "new_cred";
  const std::string kFakeMac = "fake_mac";
  const brillo::SecureBlob kFakeLeSecret("fake_le_secret");
  const brillo::SecureBlob kFakeHeSecret("fake_he_secret");
  const brillo::SecureBlob kFakeResetSecret("fake_reset_secret");
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverTryAuth(kVersion, kFakeLeSecret, _, kFakeCred, _, _, _,
                               _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<4>(PW_ERR_LOWENT_AUTH_FAILED),
                SetArgPointee<5>(kFakeRoot), SetArgPointee<7>(kFakeHeSecret),
                SetArgPointee<8>(kFakeResetSecret), SetArgPointee<9>(kNewCred),
                SetArgPointee<10>(kFakeMac), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::CheckCredential>(
      kLabel, kHAux, brillo::BlobFromString(kFakeCred), kFakeLeSecret);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->error, ErrorCode::kInvalidLeSecret);
  EXPECT_EQ(result->new_root, brillo::BlobFromString(kFakeRoot));
  ASSERT_TRUE(result->new_cred_metadata.has_value());
  EXPECT_EQ(result->new_cred_metadata.value(),
            brillo::BlobFromString(kNewCred));
  ASSERT_TRUE(result->new_mac.has_value());
  EXPECT_EQ(result->new_mac.value(), brillo::BlobFromString(kFakeMac));
  ASSERT_TRUE(result->he_secret.has_value());
  EXPECT_EQ(result->he_secret.value(), kFakeHeSecret);
  ASSERT_TRUE(result->reset_secret.has_value());
  EXPECT_EQ(result->reset_secret.value(), kFakeResetSecret);
}

TEST_F(BackendPinweaverTpm2Test, CheckCredentialTpmFail) {
  constexpr uint32_t kVersion = 1;
  constexpr uint32_t kLabel = 42;
  const std::string kFakeCred = "fake_cred";
  const brillo::SecureBlob kFakeLeSecret("fake_le_secret");
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverTryAuth(kVersion, kFakeLeSecret, _, kFakeCred, _, _, _,
                               _, _, _, _))
      .WillOnce(Return(trunks::TPM_RC_FAILURE));

  auto result = middleware_->CallSync<&Backend::PinWeaver::CheckCredential>(
      kLabel, kHAux, brillo::BlobFromString(kFakeCred), kFakeLeSecret);

  EXPECT_FALSE(result.ok());
}

TEST_F(BackendPinweaverTpm2Test, RemoveCredential) {
  constexpr uint32_t kVersion = 1;
  constexpr uint32_t kLabel = 42;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeMac = "fake_mac";
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverRemoveLeaf(kVersion, kLabel, _, kFakeMac, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(0), SetArgPointee<5>(kFakeRoot),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::RemoveCredential>(
      kLabel, kHAux, brillo::BlobFromString(kFakeMac));

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->error, ErrorCode::kSuccess);
  EXPECT_EQ(result->new_root, brillo::BlobFromString(kFakeRoot));
}

TEST_F(BackendPinweaverTpm2Test, RemoveCredentialFail) {
  constexpr uint32_t kVersion = 1;
  constexpr uint32_t kLabel = 42;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeMac = "fake_mac";
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverRemoveLeaf(kVersion, kLabel, _, kFakeMac, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(PW_ERR_HMAC_AUTH_FAILED),
                      SetArgPointee<5>(kFakeRoot),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::RemoveCredential>(
      kLabel, kHAux, brillo::BlobFromString(kFakeMac));

  EXPECT_FALSE(result.ok());
}

TEST_F(BackendPinweaverTpm2Test, ResetCredential) {
  constexpr uint32_t kVersion = 2;
  constexpr uint32_t kLabel = 42;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeCred = "fake_cred";
  const std::string kNewCred = "new_cred";
  const std::string kFakeMac = "fake_mac";
  const brillo::SecureBlob kFakeResetSecret("fake_reset_secret");
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(
      proxy_->GetMock().tpm_utility,
      PinWeaverResetAuth(kVersion, kFakeResetSecret, /*strong_reset=*/true, _,
                         kFakeCred, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(0), SetArgPointee<6>(kFakeRoot),
                      SetArgPointee<7>(kNewCred), SetArgPointee<8>(kFakeMac),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::ResetCredential>(
      kLabel, kHAux, brillo::BlobFromString(kFakeCred), kFakeResetSecret,
      /*strong_reset=*/true);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->error, ErrorCode::kSuccess);
  EXPECT_EQ(result->new_root, brillo::BlobFromString(kFakeRoot));
  ASSERT_TRUE(result->new_cred_metadata.has_value());
  EXPECT_EQ(result->new_cred_metadata.value(),
            brillo::BlobFromString(kNewCred));
  ASSERT_TRUE(result->new_mac.has_value());
  EXPECT_EQ(result->new_mac.value(), brillo::BlobFromString(kFakeMac));
}

TEST_F(BackendPinweaverTpm2Test, ResetCredentialV1ExpirationUnsupported) {
  constexpr uint32_t kVersion = 1;
  constexpr uint32_t kLabel = 42;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeCred = "fake_cred";
  const std::string kNewCred = "new_cred";
  const std::string kFakeMac = "fake_mac";
  const brillo::SecureBlob kFakeResetSecret("fake_reset_secret");
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::ResetCredential>(
      kLabel, kHAux, brillo::BlobFromString(kFakeCred), kFakeResetSecret,
      /*strong_reset=*/true);

  ASSERT_FALSE(result.ok());
}

TEST_F(BackendPinweaverTpm2Test, GetLog) {
  constexpr uint32_t kVersion = 1;
  const std::string kFakeRoot = "fake_root";
  const std::string kNewRoot = "new_root";

  trunks::PinWeaverLogEntry entry1;
  entry1.set_label(42);
  entry1.set_root(kNewRoot);
  entry1.mutable_insert_leaf()->set_hmac("fake_mac");

  trunks::PinWeaverLogEntry entry2;
  entry2.set_label(42);
  entry2.set_root(kFakeRoot);

  trunks::PinWeaverLogEntry entry3;
  entry3.set_label(43);
  entry3.set_root(kFakeRoot);
  entry3.mutable_remove_leaf();

  trunks::PinWeaverLogEntry entry4;
  entry4.set_label(44);
  entry4.set_root(kNewRoot);
  entry4.mutable_reset_tree();

  const std::vector<trunks::PinWeaverLogEntry> kFakeLog = {entry1, entry2,
                                                           entry3, entry4};

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverGetLog(kVersion, kFakeRoot, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(0), SetArgPointee<3>(kNewRoot),
                      SetArgPointee<4>(kFakeLog),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::GetLog>(
      brillo::BlobFromString(kFakeRoot));

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->root_hash, brillo::BlobFromString(kNewRoot));
  EXPECT_EQ(result->log_entries.size(), kFakeLog.size());
}

TEST_F(BackendPinweaverTpm2Test, GetLogFail) {
  constexpr uint32_t kVersion = 1;
  const std::string kFakeRoot = "fake_root";
  const std::string kNewRoot = "new_root";

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverGetLog(kVersion, kFakeRoot, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(PW_ERR_TREE_INVALID),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::GetLog>(
      brillo::BlobFromString(kFakeRoot));

  EXPECT_FALSE(result.ok());
}

TEST_F(BackendPinweaverTpm2Test, ReplayLogOperation) {
  constexpr uint32_t kVersion = 1;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeCred = "fake_cred";
  const std::string kNewCred = "new_cred";
  const std::string kFakeMac = "fake_mac";
  const brillo::SecureBlob kFakeHeSecret("fake_he_secret");
  const brillo::SecureBlob kFakeResetSecret("fake_reset_secret");
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverLogReplay(kVersion, kFakeRoot, _, kFakeCred, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(0), SetArgPointee<5>(kFakeRoot),
                      SetArgPointee<6>(kNewCred), SetArgPointee<7>(kFakeMac),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::ReplayLogOperation>(
      brillo::BlobFromString(kFakeRoot), kHAux,
      brillo::BlobFromString(kFakeCred));

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->new_cred_metadata, brillo::BlobFromString(kNewCred));
  EXPECT_EQ(result->new_mac, brillo::BlobFromString(kFakeMac));
}

TEST_F(BackendPinweaverTpm2Test, ReplayLogOperationFail) {
  constexpr uint32_t kVersion = 1;
  const std::string kFakeRoot = "fake_root";
  const std::string kFakeCred = "fake_cred";
  const std::string kNewCred = "new_cred";
  const std::string kFakeMac = "fake_mac";
  const brillo::SecureBlob kFakeHeSecret("fake_he_secret");
  const brillo::SecureBlob kFakeResetSecret("fake_reset_secret");
  const std::vector<brillo::Blob>& kHAux = {
      brillo::Blob(32, 'X'),
      brillo::Blob(32, 'Y'),
      brillo::Blob(32, 'Z'),
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverLogReplay(kVersion, kFakeRoot, _, kFakeCred, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(PW_ERR_ROOT_NOT_FOUND),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::ReplayLogOperation>(
      brillo::BlobFromString(kFakeRoot), kHAux,
      brillo::BlobFromString(kFakeCred));

  EXPECT_FALSE(result.ok());
}

TEST_F(BackendPinweaverTpm2Test, GetWrongAuthAttempts) {
  brillo::Blob header(sizeof(unimported_leaf_data_t));
  brillo::Blob leaf(sizeof(leaf_public_data_t));

  struct leaf_public_data_t* leaf_data =
      reinterpret_cast<struct leaf_public_data_t*>(leaf.data());
  leaf_data->attempt_count.v = 123;

  auto result =
      middleware_->CallSync<&Backend::PinWeaver::GetWrongAuthAttempts>(
          brillo::CombineBlobs({header, leaf}));

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 123);
}

TEST_F(BackendPinweaverTpm2Test, GetWrongAuthAttemptsEmpty) {
  auto result =
      middleware_->CallSync<&Backend::PinWeaver::GetWrongAuthAttempts>(
          brillo::Blob());

  EXPECT_FALSE(result.ok());
}

TEST_F(BackendPinweaverTpm2Test, GetDelaySchedule) {
  brillo::Blob header(sizeof(unimported_leaf_data_t));
  brillo::Blob leaf(sizeof(leaf_public_data_t));

  struct leaf_public_data_t* leaf_data =
      reinterpret_cast<struct leaf_public_data_t*>(leaf.data());
  leaf_data->delay_schedule[0].attempt_count.v = 5;
  leaf_data->delay_schedule[0].time_diff.v = UINT32_MAX;

  auto result = middleware_->CallSync<&Backend::PinWeaver::GetDelaySchedule>(
      brillo::CombineBlobs({header, leaf}));

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value().size(), 1);
  EXPECT_EQ(result.value().begin()->first, 5);
  EXPECT_EQ(result.value().begin()->second, UINT32_MAX);
}

TEST_F(BackendPinweaverTpm2Test, GetDelayScheduleEmpty) {
  auto result = middleware_->CallSync<&Backend::PinWeaver::GetDelaySchedule>(
      brillo::Blob());

  EXPECT_FALSE(result.ok());
}

TEST_F(BackendPinweaverTpm2Test, GetDelayInSecondsV1) {
  brillo::Blob header(sizeof(unimported_leaf_data_t));
  brillo::Blob leaf(sizeof(leaf_public_data_t));

  struct leaf_public_data_t* leaf_data =
      reinterpret_cast<struct leaf_public_data_t*>(leaf.data());
  leaf_data->delay_schedule[0].attempt_count.v = 5;
  leaf_data->delay_schedule[0].time_diff.v = UINT32_MAX;
  leaf_data->attempt_count.v = 4;

  // In version 1, GetDelayInSeconds only parses the cred metadata, without
  // initiating any requests to the PinWeaver server.
  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::GetDelayInSeconds>(
      brillo::CombineBlobs({header, leaf}));

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 0);

  leaf_data->attempt_count.v = 5;

  auto result2 = middleware_->CallSync<&Backend::PinWeaver::GetDelayInSeconds>(
      brillo::CombineBlobs({header, leaf}));

  ASSERT_TRUE(result2.ok());
  EXPECT_EQ(result2.value(), UINT32_MAX);
}

TEST_F(BackendPinweaverTpm2Test, GetDelayInSecondsV2) {
  const std::string kFakeRoot = "fake_root";

  brillo::Blob header(sizeof(unimported_leaf_data_t));
  brillo::Blob leaf(sizeof(leaf_public_data_t));

  struct leaf_public_data_t* leaf_data =
      reinterpret_cast<struct leaf_public_data_t*>(leaf.data());
  leaf_data->delay_schedule[0].attempt_count.v = 5;
  leaf_data->delay_schedule[0].time_diff.v = 60;
  leaf_data->delay_schedule[1].attempt_count.v = 6;
  leaf_data->delay_schedule[1].time_diff.v = 70;
  leaf_data->delay_schedule[2].attempt_count.v = 7;
  leaf_data->delay_schedule[2].time_diff.v = UINT32_MAX;
  leaf_data->last_access_ts.boot_count = 0;
  leaf_data->last_access_ts.timer_value = 100;
  leaf_data->attempt_count.v = 4;

  // In version 2, GetDelayInSeconds requests the current timestamp from the
  // PinWeaver server, so that it can return a more accurate remaining seconds.
  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(2), Return(trunks::TPM_RC_SUCCESS)));

  // This is only called twice because when the delay is infinite, we don't have
  // to query the current timestamp.
  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverSysInfo(2, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(kFakeRoot),
                      SetArgPointee<3>(0), SetArgPointee<4>(120),
                      Return(trunks::TPM_RC_SUCCESS)))
      .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(kFakeRoot),
                      SetArgPointee<3>(1), SetArgPointee<4>(10),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::PinWeaver::GetDelayInSeconds>(
      brillo::CombineBlobs({header, leaf}));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 0);

  // Ready timestamp is 100+60=160, and the current timestamp is 120.
  leaf_data->attempt_count.v = 5;
  auto result2 = middleware_->CallSync<&Backend::PinWeaver::GetDelayInSeconds>(
      brillo::CombineBlobs({header, leaf}));
  ASSERT_TRUE(result2.ok());
  EXPECT_EQ(result2.value(), 40);

  // Ready timestamp is 70 because the boot count has changed, and the current
  // timestamp is 10.
  leaf_data->attempt_count.v = 6;
  auto result3 = middleware_->CallSync<&Backend::PinWeaver::GetDelayInSeconds>(
      brillo::CombineBlobs({header, leaf}));
  ASSERT_TRUE(result3.ok());
  EXPECT_EQ(result3.value(), 60);

  // Ready timestamp isn't important because the leaf is infinitely locked out.
  leaf_data->attempt_count.v = 7;
  auto result4 = middleware_->CallSync<&Backend::PinWeaver::GetDelayInSeconds>(
      brillo::CombineBlobs({header, leaf}));
  ASSERT_TRUE(result4.ok());
  EXPECT_EQ(result4.value(), UINT32_MAX);
}

TEST_F(BackendPinweaverTpm2Test, GetExpirationInSecondsV1) {
  constexpr uint32_t kVersion = 1;
  const std::string kFakeRoot = "fake_root";

  brillo::Blob header(sizeof(unimported_leaf_data_t));
  brillo::Blob leaf(sizeof(leaf_public_data_t));

  struct leaf_public_data_t* leaf_data =
      reinterpret_cast<struct leaf_public_data_t*>(leaf.data());
  leaf_data->expiration_delay_s.v = 10;
  leaf_data->expiration_ts.boot_count = 1;
  leaf_data->expiration_ts.timer_value = 120;

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  // In version 1, credentials are always treated as having no expiration.
  auto result =
      middleware_->CallSync<&Backend::PinWeaver::GetExpirationInSeconds>(
          brillo::CombineBlobs({header, leaf}));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), std::nullopt);
}

TEST_F(BackendPinweaverTpm2Test, GetExpirationInSecondsV2) {
  constexpr uint32_t kVersion = 2;
  const std::string kFakeRoot = "fake_root";

  brillo::Blob header(sizeof(unimported_leaf_data_t));
  brillo::Blob leaf(sizeof(leaf_public_data_t));
  // Simulate a leaf created at v1.
  brillo::Blob leaf_v1(offsetof(leaf_public_data_t, expiration_ts));

  struct leaf_public_data_t* leaf_data =
      reinterpret_cast<struct leaf_public_data_t*>(leaf.data());
  leaf_data->expiration_delay_s.v = 0;
  leaf_data->expiration_ts.boot_count = 0;
  leaf_data->expiration_ts.timer_value = 0;

  EXPECT_CALL(proxy_->GetMock().tpm_utility, PinWeaverIsSupported(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kVersion), Return(trunks::TPM_RC_SUCCESS)));

  // This is only called 3 times because when the delay is 0, we don't have
  // to query the current timestamp.
  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              PinWeaverSysInfo(kVersion, _, _, _, _))
      .Times(3)
      .WillRepeatedly(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(kFakeRoot),
                            SetArgPointee<3>(1), SetArgPointee<4>(100),
                            Return(trunks::TPM_RC_SUCCESS)));

  auto result =
      middleware_->CallSync<&Backend::PinWeaver::GetExpirationInSeconds>(
          brillo::CombineBlobs({header, leaf}));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), std::nullopt);

  leaf_data->expiration_delay_s.v = 10;
  leaf_data->expiration_ts.timer_value = 120;
  auto result2 =
      middleware_->CallSync<&Backend::PinWeaver::GetExpirationInSeconds>(
          brillo::CombineBlobs({header, leaf}));
  ASSERT_TRUE(result2.ok());
  EXPECT_EQ(result2.value(), 0);

  leaf_data->expiration_ts.boot_count = 1;
  leaf_data->expiration_ts.timer_value = 80;
  auto result3 =
      middleware_->CallSync<&Backend::PinWeaver::GetExpirationInSeconds>(
          brillo::CombineBlobs({header, leaf}));
  ASSERT_TRUE(result3.ok());
  EXPECT_EQ(result3.value(), 0);

  leaf_data->expiration_ts.timer_value = 120;
  auto result4 =
      middleware_->CallSync<&Backend::PinWeaver::GetExpirationInSeconds>(
          brillo::CombineBlobs({header, leaf}));
  ASSERT_TRUE(result4.ok());
  EXPECT_EQ(result4.value(), 20);

  // Leaf created in version before v2 has no expiration.
  auto result5 =
      middleware_->CallSync<&Backend::PinWeaver::GetExpirationInSeconds>(
          brillo::CombineBlobs({header, leaf_v1}));
  ASSERT_TRUE(result5.ok());
  EXPECT_EQ(result5.value(), std::nullopt);
}

}  // namespace hwsec
