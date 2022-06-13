// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <openssl/sha.h>

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

class BackendConfigTpm2Test : public BackendTpm2TestBase {};

TEST_F(BackendConfigTpm2Test, ToOperationPolicy) {
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

  auto result =
      middleware_->CallSync<&Backend::Config::ToOperationPolicy>(kFakeSetting);

  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->permission.auth_value.has_value());
  EXPECT_EQ(result->permission.auth_value.value(), kFakeAuthValue);
  EXPECT_EQ(result->device_configs, (DeviceConfigs{
                                        DeviceConfig::kBootMode,
                                        DeviceConfig::kDeviceModel,
                                        DeviceConfig::kCurrentUser,
                                    }));
}

TEST_F(BackendConfigTpm2Test, SetCurrentUser) {
  const std::string kFakeUser = "fake_user";

  EXPECT_CALL(proxy_->GetMock().tpm_utility, ExtendPCR(_, kFakeUser, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().tpm_utility, ExtendPCRForCSME(_, kFakeUser))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result =
      middleware_->CallSync<&Backend::Config::SetCurrentUser>(kFakeUser);

  EXPECT_TRUE(result.ok());
}

TEST_F(BackendConfigTpm2Test, IsCurrentUserSet) {
  const std::string kNonZeroPcr(SHA256_DIGEST_LENGTH, 'X');

  EXPECT_CALL(proxy_->GetMock().tpm_utility, ReadPCR(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kNonZeroPcr), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::Config::IsCurrentUserSet>();

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value());
}

TEST_F(BackendConfigTpm2Test, IsCurrentUserSetZero) {
  const std::string kZeroPcr(SHA256_DIGEST_LENGTH, 0);

  EXPECT_CALL(proxy_->GetMock().tpm_utility, ReadPCR(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kZeroPcr), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::Config::IsCurrentUserSet>();

  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value());
}

}  // namespace hwsec
