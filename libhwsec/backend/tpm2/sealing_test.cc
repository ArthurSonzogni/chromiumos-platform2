// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <trunks/error_codes.h>
#include <trunks/mock_policy_session.h>
#include <trunks/mock_tpm.h>
#include <trunks/mock_tpm_utility.h>

#include "libhwsec/backend/tpm2/backend_test_base.h"
#include "libhwsec/middleware/middleware.h"

using hwsec_foundation::error::testing::IsOkAndHolds;
using hwsec_foundation::error::testing::NotOk;
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

using BackendSealingTpm2Test = BackendTpm2TestBase;

TEST_F(BackendSealingTpm2Test, IsSupported) {
  EXPECT_THAT(backend_->GetSealingTpm2().IsSupported(), IsOkAndHolds(true));
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

  EXPECT_CALL(proxy_->GetMockTrialSession(), GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakePolicyDigest),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(
      proxy_->GetMockTpmUtility(),
      SealData(kFakeData, kFakePolicyDigest, kFakeAuthValue, true, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(kFakeSealedData),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(backend_->GetSealingTpm2().Seal(kFakePolicy,
                                              brillo::SecureBlob(kFakeData)),
              IsOkAndHolds(brillo::BlobFromString(kFakeSealedData)));
}

TEST_F(BackendSealingTpm2Test, PreloadSealedData) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeSealedData = "fake_sealed_data";
  const uint32_t kFakeKeyHandle = 0x1337;

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeSealedData, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result = backend_->GetSealingTpm2().PreloadSealedData(
      kFakePolicy, brillo::BlobFromString(kFakeSealedData));

  ASSERT_OK(result);
  EXPECT_TRUE(result->has_value());

  EXPECT_CALL(proxy_->GetMockTpm(), FlushContextSync(kFakeKeyHandle, _))
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

  EXPECT_CALL(proxy_->GetMockTpmUtility(), UnsealData(kFakeSealedData, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(kFakeData), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(backend_->GetSealingTpm2().Unseal(
                  kFakePolicy, brillo::BlobFromString(kFakeSealedData),
                  Backend::Sealing::UnsealOptions{}),
              IsOkAndHolds(brillo::SecureBlob(kFakeData)));
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

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeSealedData, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result = backend_->GetSealingTpm2().PreloadSealedData(
      kFakePolicy, brillo::BlobFromString(kFakeSealedData));

  ASSERT_OK(result);
  EXPECT_TRUE(result->has_value());

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              UnsealDataWithHandle(kFakeKeyHandle, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(kFakeData), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(backend_->GetSealingTpm2().Unseal(
                  kFakePolicy, brillo::BlobFromString(kFakeSealedData),
                  Backend::Sealing::UnsealOptions{
                      .preload_data = result->value().GetKey(),
                  }),
              IsOkAndHolds(brillo::SecureBlob(kFakeData)));

  EXPECT_CALL(proxy_->GetMockTpm(), FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

TEST_F(BackendSealingTpm2Test, UnsealWithPreloadAutoReloadOnInvalidHandle) {
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
  const uint32_t kStaleKeyHandle = 0x1337;
  const uint32_t kReloadedKeyHandle = 0x7331;

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeSealedData, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kStaleKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)))
      .WillOnce(DoAll(SetArgPointee<2>(kReloadedKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kStaleKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result = backend_->GetSealingTpm2().PreloadSealedData(
      kFakePolicy, brillo::BlobFromString(kFakeSealedData));

  ASSERT_OK(result);
  ASSERT_TRUE(result->has_value());

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              UnsealDataWithHandle(kStaleKeyHandle, _, _))
      .WillOnce(
          Return(trunks::TPM_RC_HANDLE | trunks::kResourceManagerTpmErrorBase));

  EXPECT_CALL(proxy_->GetMockTpm(), FlushContextSync(kStaleKeyHandle, _))
      .WillOnce(
          Return(trunks::TPM_RC_HANDLE | trunks::kResourceManagerTpmErrorBase));

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              UnsealDataWithHandle(kReloadedKeyHandle, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(kFakeData), Return(trunks::TPM_RC_SUCCESS)));

  Middleware middleware(middleware_owner_->Derive());
  EXPECT_THAT(middleware.CallSync<&Backend::Sealing::Unseal>(
                  kFakePolicy, brillo::BlobFromString(kFakeSealedData),
                  Backend::Sealing::UnsealOptions{
                      .preload_data = result->value().GetKey(),
                  }),
              IsOkAndHolds(brillo::SecureBlob(kFakeData)));

  EXPECT_CALL(proxy_->GetMockTpm(), FlushContextSync(kReloadedKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

// Regression guard ensuring `TPMRetryHandler::ReloadObject(UnsealOptions)`
// returns `false` and does not trigger a retry when `preload_data` is empty
// (using a policy session so `FlushInvalidSessions` is also a no-op).
TEST_F(BackendSealingTpm2Test,
       UnsealWithoutPreloadDoesNotRetryOnInvalidHandle) {
  const OperationPolicy kFakePolicy{
      .device_configs = DeviceConfigs{DeviceConfig::kCurrentUser},
  };
  const std::string kFakeSealedData = "fake_sealed_data";

  EXPECT_CALL(proxy_->GetMockTpmUtility(), UnsealData(kFakeSealedData, _, _))
      .Times(1)
      .WillOnce(
          Return(trunks::TPM_RC_HANDLE | trunks::kResourceManagerTpmErrorBase));

  Middleware middleware(middleware_owner_->Derive());
  EXPECT_THAT(middleware.CallSync<&Backend::Sealing::Unseal>(
                  kFakePolicy, brillo::BlobFromString(kFakeSealedData),
                  Backend::Sealing::UnsealOptions{}),
              NotOk());
}

}  // namespace hwsec
