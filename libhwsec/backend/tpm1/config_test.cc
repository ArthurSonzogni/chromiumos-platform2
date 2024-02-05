// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/hash/sha1.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <openssl/sha.h>

#include "libhwsec/backend/tpm1/backend_test_base.h"
#include "libhwsec/backend/tpm1/static_utils.h"
#include "libhwsec/overalls/mock_overalls.h"

using brillo::BlobFromString;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::IsOkAndHolds;
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

using BackendConfigTpm1Test = BackendTpm1TestBase;

TEST_F(BackendConfigTpm1Test, ToOperationPolicy) {
  const brillo::SecureBlob kFakeAuthValue("auth_value");
  const OperationPolicySetting kFakeSetting = {
      .device_config_settings =
          DeviceConfigSettings{
              .boot_mode =
                  DeviceConfigSettings::BootModeSetting{
                      .mode =
                          DeviceConfigSettings::BootModeSetting::Mode{
                              .developer_mode = true,
                              .recovery_mode = true,
                              .verified_firmware = true,
                          },
                  },
              .device_model =
                  DeviceConfigSettings::DeviceModelSetting{
                      .hardware_id = "ZZCR",
                  },
              .current_user =
                  DeviceConfigSettings::CurrentUserSetting{
                      .username = "username",
                  },
          },
      .permission =
          Permission{
              .auth_value = kFakeAuthValue,
          },
  };

  auto result = backend_->GetConfigTpm1().ToOperationPolicy(kFakeSetting);

  ASSERT_OK(result);
  ASSERT_TRUE(result->permission.auth_value.has_value());
  EXPECT_EQ(result->permission.auth_value.value(), kFakeAuthValue);
  EXPECT_EQ(result->device_configs, (DeviceConfigs{
                                        DeviceConfig::kBootMode,
                                        DeviceConfig::kDeviceModel,
                                        DeviceConfig::kCurrentUser,
                                    }));
}

TEST_F(BackendConfigTpm1Test, SetCurrentUser) {
  const std::string kFakeUser = "fake_user";

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_TPM_PcrExtend(kDefaultTpm, _, _, _, _, _, _))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_THAT(backend_->GetConfigTpm1().SetCurrentUser(kFakeUser), IsOk());
}

TEST_F(BackendConfigTpm1Test, IsCurrentUserSet) {
  const brillo::Blob kNonZeroPcr(SHA_DIGEST_LENGTH, 'X');

  brillo::Blob non_zero_pcr = kNonZeroPcr;
  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_TPM_PcrRead(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(non_zero_pcr.size()),
                      SetArgPointee<3>(non_zero_pcr.data()),
                      Return(TPM_SUCCESS)));

  EXPECT_THAT(backend_->GetConfigTpm1().IsCurrentUserSet(), IsOkAndHolds(true));
}

TEST_F(BackendConfigTpm1Test, IsCurrentUserSetZero) {
  const brillo::Blob kZeroPcr(SHA_DIGEST_LENGTH, 0);

  brillo::Blob zero_pcr = kZeroPcr;
  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_TPM_PcrRead(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(zero_pcr.size()),
                      SetArgPointee<3>(zero_pcr.data()), Return(TPM_SUCCESS)));

  EXPECT_THAT(backend_->GetConfigTpm1().IsCurrentUserSet(),
              IsOkAndHolds(false));
}

TEST_F(BackendConfigTpm1Test, GetCurrentBootMode) {
  DeviceConfigSettings::BootModeSetting::Mode fake_mode = {
      .developer_mode = false,
      .recovery_mode = true,
      .verified_firmware = false,
  };
  brillo::Blob valid_pcr_blob = GetTpm1PCRValueForMode(fake_mode);

  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_TPM_PcrRead(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(valid_pcr_blob.size()),
                      SetArgPointee<3>(valid_pcr_blob.data()),
                      Return(TPM_SUCCESS)));

  auto result = backend_->GetConfigTpm1().GetCurrentBootMode();
  ASSERT_OK(result);
  EXPECT_TRUE(result.value().developer_mode == fake_mode.developer_mode);
  EXPECT_TRUE(result.value().recovery_mode == fake_mode.recovery_mode);
  EXPECT_TRUE(result.value().verified_firmware == fake_mode.verified_firmware);
}

TEST_F(BackendConfigTpm1Test, GetCurrentBootModeInvalid) {
  const brillo::Blob invalid_pcr(SHA_DIGEST_LENGTH, 0);

  brillo::Blob invalid_pcr_blob = invalid_pcr;
  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_TPM_PcrRead(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(invalid_pcr_blob.size()),
                      SetArgPointee<3>(invalid_pcr_blob.data()),
                      Return(TPM_SUCCESS)));

  auto result = backend_->GetConfigTpm1().GetCurrentBootMode();
  ASSERT_NOT_OK(result);
}

}  // namespace hwsec
