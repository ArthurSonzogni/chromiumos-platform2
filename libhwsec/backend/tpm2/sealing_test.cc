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

class BackendSealingTpm2Test : public BackendTpm2TestBase {};

TEST_F(BackendSealingTpm2Test, IsSupported) {
  auto result = middleware_->CallSync<&Backend::Sealing::IsSupported>();

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(*result);
}

TEST_F(BackendSealingTpm2Test, Seal) {
  const std::string kFakeAuthValue = "fake_auth_value";
  const OperationPolicySetting kFakePolicy{
      .device_config_settings =
          DeviceConfigSettings{
              .current_user =
                  DeviceConfigSettings::CurrentUserSetting{
                      .username = std::nullopt,
                  },
          },
      .permission =
          Permission{
              .auth_value = brillo::SecureBlob(kFakeAuthValue),
          },
  };
  const std::string kFakePolicyDigest = "fake_policy_digest";
  const std::string kFakeData = "fake_data";
  const std::string kFakeSealedData = "fake_sealed_data";

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetPolicyDigestForPcrValues(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakePolicyDigest),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(
      proxy_->GetMock().tpm_utility,
      SealData(kFakeData, kFakePolicyDigest, kFakeAuthValue, true, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(kFakeSealedData),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::Sealing::Seal>(
      kFakePolicy, brillo::SecureBlob(kFakeData));

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, brillo::BlobFromString(kFakeSealedData));
}

TEST_F(BackendSealingTpm2Test, PreloadSealedData) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeSealedData = "fake_sealed_data";
  const uint32_t kFakeKeyHandle = 0x1337;

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeSealedData, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result = middleware_->CallSync<&Backend::Sealing::PreloadSealedData>(
      kFakePolicy, brillo::BlobFromString(kFakeSealedData));

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result->has_value());

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

TEST_F(BackendSealingTpm2Test, Unseal) {
  const std::string kFakeAuthValue = "fake_auth_value";
  const OperationPolicy kFakePolicy{
      .device_configs = DeviceConfigs{DeviceConfig::kCurrentUser},
      .permission =
          Permission{
              .auth_value = brillo::SecureBlob(kFakeAuthValue),
          },
  };
  const std::string kFakeData = "fake_data";
  const std::string kFakeSealedData = "fake_sealed_data";

  EXPECT_CALL(proxy_->GetMock().tpm_utility, UnsealData(kFakeSealedData, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(kFakeData), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::Sealing::Unseal>(
      kFakePolicy, brillo::BlobFromString(kFakeSealedData),
      Backend::Sealing::UnsealOptions{});

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, brillo::SecureBlob(kFakeData));
}

TEST_F(BackendSealingTpm2Test, UnsealWithPreload) {
  const std::string kFakeAuthValue = "fake_auth_value";
  const OperationPolicy kFakePolicy{
      .device_configs = DeviceConfigs{DeviceConfig::kCurrentUser},
      .permission =
          Permission{
              .auth_value = brillo::SecureBlob(kFakeAuthValue),
          },
  };
  const std::string kFakeData = "fake_data";
  const std::string kFakeSealedData = "fake_sealed_data";
  const uint32_t kFakeKeyHandle = 0x1337;

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeSealedData, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result = middleware_->CallSync<&Backend::Sealing::PreloadSealedData>(
      kFakePolicy, brillo::BlobFromString(kFakeSealedData));

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result->has_value());

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              UnsealDataWithHandle(kFakeKeyHandle, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(kFakeData), Return(trunks::TPM_RC_SUCCESS)));

  auto result2 = middleware_->CallSync<&Backend::Sealing::Unseal>(
      kFakePolicy, brillo::BlobFromString(kFakeSealedData),
      Backend::Sealing::UnsealOptions{
          .preload_data = result->value().GetKey(),
      });

  ASSERT_TRUE(result2.ok());
  EXPECT_EQ(*result2, brillo::SecureBlob(kFakeData));

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

}  // namespace hwsec
