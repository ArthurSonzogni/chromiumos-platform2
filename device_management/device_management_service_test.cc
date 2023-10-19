// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_management/device_management_service.h"

#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "device_management/fwmp/mock_firmware_management_parameters.h"
#include "device_management/install_attributes/mock_install_attributes.h"
#include "device_management/install_attributes/mock_platform.h"

using ::hwsec::TPMError;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::hwsec_foundation::status::MakeStatus;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

using TPMRetryAction = ::hwsec::TPMRetryAction;

namespace device_management {

// Make sure the Init of install attributes always be called at least once.
class DeviceManagementServiceEnsureInstallAttributeInitTest
    : public ::testing::Test {
 public:
  void SetUp() override {
    auto fwmp =
        std::make_unique<StrictMock<MockFirmwareManagementParameters>>();
    auto install_attrs = std::make_unique<StrictMock<MockInstallAttributes>>();
    fwmp_ = fwmp.get();
    install_attrs_ = install_attrs.get();
    device_management_service_.SetParamsForTesting(std::move(fwmp),
                                                   std::move(install_attrs));

    EXPECT_CALL(*install_attrs_, Init()).Times(AtLeast(1));

    EXPECT_CALL(*install_attrs_, status())
        .WillRepeatedly(Return(InstallAttributes::Status::kUnknown));
    EXPECT_CALL(*install_attrs_, Get(_, _)).WillRepeatedly(Return(false));

    // Called by Initialize().
    EXPECT_CALL(hwsec_, RegisterOnReadyCallback)
        .WillOnce([this](base::OnceCallback<void(hwsec::Status)> cb) {
          hwsec_callback_ = std::move(cb);
        });
  }

 protected:
  StrictMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<MockPlatform> platform_;
  DeviceManagementService device_management_service_;
  StrictMock<MockFirmwareManagementParameters>* fwmp_;
  StrictMock<MockInstallAttributes>* install_attrs_;
  base::OnceCallback<void(hwsec::Status)> hwsec_callback_;
};

TEST_F(DeviceManagementServiceEnsureInstallAttributeInitTest,
       InitializeWithHwsecReady) {
  device_management_service_.Initialize(hwsec_, platform_);
  ASSERT_FALSE(hwsec_callback_.is_null());

  // Test the cast that libhwsec is ready.
  std::move(hwsec_callback_).Run(hwsec::OkStatus());
}

// These tests are only useful when the insecure fallback is on.
#if USE_TPM_INSECURE_FALLBACK
TEST_F(DeviceManagementServiceEnsureInstallAttributeInitTest,
       InitializeWithHwsecNoBackend) {
  device_management_service_.Initialize(hwsec_, platform_);
  ASSERT_FALSE(hwsec_callback_.is_null());

  // Test the case that there is no backend in libhwsec.
  std::move(hwsec_callback_)
      .Run(MakeStatus<TPMError>("No backend", TPMRetryAction::kNoRetry));
}

TEST_F(DeviceManagementServiceEnsureInstallAttributeInitTest,
       InitializeWithHwsecNeverReady) {
  device_management_service_.Initialize(hwsec_, platform_);
  ASSERT_FALSE(hwsec_callback_.is_null());

  // Test the cast that libhwsec never ready.
  std::move(hwsec_callback_).Reset();
}
#endif  // USE_TPM_INSECURE_FALLBACK

// Make sure the first install cases works correctly.
class DeviceManagementServicerInstallAttibuteFirstInstallTest
    : public ::testing::Test {
 public:
  void SetUp() override {
    auto fwmp =
        std::make_unique<StrictMock<MockFirmwareManagementParameters>>();
    auto install_attrs =
        std::make_unique<InstallAttributes>(&platform_, &hwsec_);
    fwmp_ = fwmp.get();
    device_management_service_.SetParamsForTesting(std::move(fwmp),
                                                   std::move(install_attrs));

    // Called by Initialize().
    EXPECT_CALL(hwsec_, RegisterOnReadyCallback)
        .WillOnce([this](base::OnceCallback<void(hwsec::Status)> cb) {
          hwsec_callback_ = std::move(cb);
        });
  }

 protected:
  StrictMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<MockPlatform> platform_;
  DeviceManagementService device_management_service_;
  StrictMock<MockFirmwareManagementParameters>* fwmp_;
  base::OnceCallback<void(hwsec::Status)> hwsec_callback_;
};

TEST_F(DeviceManagementServicerInstallAttibuteFirstInstallTest,
       InitializeWithHwsecReady) {
  // Assume the libhwsec is ready.
  EXPECT_CALL(hwsec_, IsEnabled()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_, PrepareSpace(_, _)).WillOnce(ReturnOk<TPMError>());

  device_management_service_.Initialize(hwsec_, platform_);

  // The install attributes may not be initialized yet here.

  ASSERT_FALSE(hwsec_callback_.is_null());
  std::move(hwsec_callback_).Run(hwsec::OkStatus());

  EXPECT_EQ(InstallAttributes::Status::kFirstInstall,
            device_management_service_.InstallAttributesGetStatus());
}

// These tests are only useful when the insecure fallback is on.
#if USE_TPM_INSECURE_FALLBACK
TEST_F(DeviceManagementServicerInstallAttibuteFirstInstallTest,
       InitializeWithHwsecNoBackend) {
  // Test the case that there is no backend in libhwsec.
  EXPECT_CALL(hwsec_, IsEnabled())
      .WillRepeatedly(
          ReturnError<TPMError>("No backend", TPMRetryAction::kNoRetry));
  EXPECT_CALL(hwsec_, IsReady())
      .WillRepeatedly(
          ReturnError<TPMError>("No backend", TPMRetryAction::kNoRetry));

  device_management_service_.Initialize(hwsec_, platform_);

  ASSERT_FALSE(hwsec_callback_.is_null());
  std::move(hwsec_callback_)
      .Run(MakeStatus<TPMError>("No backend", TPMRetryAction::kNoRetry));

  EXPECT_EQ(InstallAttributes::Status::kFirstInstall,
            device_management_service_.InstallAttributesGetStatus());
}

TEST_F(DeviceManagementServicerInstallAttibuteFirstInstallTest,
       InitializeWithHwsecNeverReady) {
  // Test the cast that libhwsec never ready.
  EXPECT_CALL(hwsec_, IsEnabled()).WillRepeatedly(ReturnValue(false));
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(false));

  device_management_service_.Initialize(hwsec_, platform_);

  ASSERT_FALSE(hwsec_callback_.is_null());
  std::move(hwsec_callback_).Reset();

  EXPECT_EQ(InstallAttributes::Status::kFirstInstall,
            device_management_service_.InstallAttributesGetStatus());
}
#endif  // USE_TPM_INSECURE_FALLBACK

// TODO(b/306379379): Add more unittests for the service.

}  // namespace device_management
