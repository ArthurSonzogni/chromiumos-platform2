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
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "device_management/fwmp/mock_firmware_management_parameters.h"
#include "device_management/install_attributes/mock_install_attributes.h"
#include "device_management/install_attributes/mock_platform.h"
#include "device_management/proto_bindings/device_management_interface.pb.h"

using ::hwsec::TPMError;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::hwsec_foundation::status::MakeStatus;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::ElementsAreArray;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SaveArgPointee;
using ::testing::SetArgPointee;
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
class DeviceManagementServiceInstallAttibuteFirstInstallTest
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

TEST_F(DeviceManagementServiceInstallAttibuteFirstInstallTest,
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
TEST_F(DeviceManagementServiceInstallAttibuteFirstInstallTest,
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

TEST_F(DeviceManagementServiceInstallAttibuteFirstInstallTest,
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

// Provides a test fixture to make sure operations related to FWMP,
// install_attributes etc. are working as expected.
class DeviceManagementServiceAPITest : public ::testing::Test {
 public:
  void SetUp() override {
    auto fwmp =
        std::make_unique<StrictMock<MockFirmwareManagementParameters>>();
    auto install_attrs = std::make_unique<StrictMock<MockInstallAttributes>>();
    fwmp_ = fwmp.get();
    install_attrs_ = install_attrs.get();
    device_management_service_.SetParamsForTesting(std::move(fwmp),
                                                   std::move(install_attrs));
  }

 protected:
  constexpr static char kInstallAttributeName[] = "SomeRandomAttribute";
  constexpr static uint8_t kInstallAttributeData[] = {0x01, 0x02, 0x00,
                                                      0x03, 0xFF, 0xAB};
  DeviceManagementService device_management_service_;
  StrictMock<MockFirmwareManagementParameters>* fwmp_;
  StrictMock<MockInstallAttributes>* install_attrs_;
};

TEST_F(DeviceManagementServiceAPITest, GetFirmwareManagementParametersSuccess) {
  const std::string kHash = "its_a_hash";
  std::vector<uint8_t> hash(kHash.begin(), kHash.end());
  constexpr uint32_t kFlag = 0x1234;

  EXPECT_CALL(*fwmp_, Load()).WillOnce(Return(true));
  EXPECT_CALL(*fwmp_, GetFlags(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(kFlag), Return(true)));
  EXPECT_CALL(*fwmp_, GetDeveloperKeyHash(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(hash), Return(true)));

  device_management::FirmwareManagementParameters fwmp;
  EXPECT_EQ(device_management::DEVICE_MANAGEMENT_ERROR_NOT_SET,
            device_management_service_.GetFirmwareManagementParameters(&fwmp));

  EXPECT_EQ(kFlag, fwmp.flags());
  EXPECT_EQ(kHash, fwmp.developer_key_hash());
}

TEST_F(DeviceManagementServiceAPITest, GetFirmwareManagementParametersFailure) {
  constexpr uint32_t kFlag = 0x1234;

  // Test Load() fail.
  EXPECT_CALL(*fwmp_, Load()).WillRepeatedly(Return(false));

  device_management::FirmwareManagementParameters fwmp;
  EXPECT_EQ(device_management::
                DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID,
            device_management_service_.GetFirmwareManagementParameters(&fwmp));

  // Test GetFlags() fail.
  EXPECT_CALL(*fwmp_, Load()).WillRepeatedly(Return(true));
  EXPECT_CALL(*fwmp_, GetFlags(_)).WillRepeatedly(Return(false));

  EXPECT_EQ(device_management::
                DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID,
            device_management_service_.GetFirmwareManagementParameters(&fwmp));

  // Test GetDeveloperKeyHash fail.
  EXPECT_CALL(*fwmp_, Load()).WillRepeatedly(Return(true));
  EXPECT_CALL(*fwmp_, GetFlags(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(kFlag), Return(true)));
  EXPECT_CALL(*fwmp_, GetDeveloperKeyHash(_)).WillRepeatedly(Return(false));

  EXPECT_EQ(device_management::
                DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID,
            device_management_service_.GetFirmwareManagementParameters(&fwmp));
}

TEST_F(DeviceManagementServiceAPITest, SetFirmwareManagementParametersSuccess) {
  const std::string kHash = "its_a_hash";
  std::vector<uint8_t> hash(kHash.begin(), kHash.end());
  constexpr uint32_t kFlag = 0x1234;

  std::vector<uint8_t> out_hash;

  EXPECT_CALL(*fwmp_, Create()).WillOnce(Return(true));
  EXPECT_CALL(*fwmp_, Store(kFlag, _))
      .WillOnce(DoAll(SaveArgPointee<1>(&out_hash), Return(true)));

  device_management::FirmwareManagementParameters fwmp;
  fwmp.set_flags(kFlag);
  fwmp.set_developer_key_hash(kHash);

  EXPECT_EQ(device_management::DEVICE_MANAGEMENT_ERROR_NOT_SET,
            device_management_service_.SetFirmwareManagementParameters(fwmp));

  EXPECT_EQ(hash, out_hash);
}

TEST_F(DeviceManagementServiceAPITest, SetFirmwareManagementParametersNoHash) {
  constexpr uint32_t kFlag = 0x1234;

  EXPECT_CALL(*fwmp_, Create()).WillOnce(Return(true));
  EXPECT_CALL(*fwmp_, Store(kFlag, nullptr)).WillOnce(Return(true));

  device_management::FirmwareManagementParameters fwmp;
  fwmp.set_flags(kFlag);

  EXPECT_EQ(device_management::DEVICE_MANAGEMENT_ERROR_NOT_SET,
            device_management_service_.SetFirmwareManagementParameters(fwmp));
}

TEST_F(DeviceManagementServiceAPITest,
       SetFirmwareManagementParametersCreateError) {
  const std::string kHash = "its_a_hash";
  constexpr uint32_t kFlag = 0x1234;

  EXPECT_CALL(*fwmp_, Create()).WillOnce(Return(false));

  device_management::FirmwareManagementParameters fwmp;
  fwmp.set_flags(kFlag);
  fwmp.set_developer_key_hash(kHash);

  EXPECT_EQ(
      device_management::
          DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE,
      device_management_service_.SetFirmwareManagementParameters(fwmp));
}

TEST_F(DeviceManagementServiceAPITest,
       SetFirmwareManagementParametersStoreError) {
  const std::string kHash = "its_a_hash";
  constexpr uint32_t kFlag = 0x1234;

  EXPECT_CALL(*fwmp_, Create()).WillOnce(Return(true));
  EXPECT_CALL(*fwmp_, Store(_, _)).WillOnce(Return(false));

  device_management::FirmwareManagementParameters fwmp;
  fwmp.set_flags(kFlag);
  fwmp.set_developer_key_hash(kHash);

  EXPECT_EQ(
      device_management::
          DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE,
      device_management_service_.SetFirmwareManagementParameters(fwmp));
}

TEST_F(DeviceManagementServiceAPITest,
       RemoveFirmwareManagementParametersSuccess) {
  EXPECT_CALL(*fwmp_, Destroy()).WillOnce(Return(true));
  EXPECT_TRUE(device_management_service_.RemoveFirmwareManagementParameters());
}

TEST_F(DeviceManagementServiceAPITest,
       RemoveFirmwareManagementParametersFailure) {
  EXPECT_CALL(*fwmp_, Destroy()).WillOnce(Return(false));
  EXPECT_FALSE(device_management_service_.RemoveFirmwareManagementParameters());
}

TEST_F(DeviceManagementServiceAPITest, InstallAttributesEnterpriseOwned) {
  brillo::Blob blob_true = brillo::BlobFromString("true");
  blob_true.push_back(0);

  EXPECT_CALL(*install_attrs_, Get("enterprise.owned", _))
      .WillOnce(DoAll(SetArgPointee<1>(blob_true), Return(true)));

  device_management_service_.DetectEnterpriseOwnership();

  EXPECT_TRUE(device_management_service_.IsEnterpriseOwned());
}

TEST_F(DeviceManagementServiceAPITest, InstallAttributesNotEnterpriseOwned) {
  brillo::Blob blob_false = brillo::BlobFromString("false");
  blob_false.push_back(0);

  EXPECT_CALL(*install_attrs_, Get("enterprise.owned", _))
      .WillOnce(DoAll(SetArgPointee<1>(blob_false), Return(true)));

  device_management_service_.DetectEnterpriseOwnership();

  EXPECT_FALSE(device_management_service_.IsEnterpriseOwned());
}

TEST_F(DeviceManagementServiceAPITest, InstallAttributesGet) {
  // Test for successful case.
  EXPECT_CALL(*install_attrs_, Get(kInstallAttributeName, _))
      .WillOnce(DoAll(SetArgPointee<1>(std::vector<uint8_t>(
                          std::begin(kInstallAttributeData),
                          std::end(kInstallAttributeData))),
                      Return(true)));
  std::vector<uint8_t> data;
  EXPECT_TRUE(device_management_service_.InstallAttributesGet(
      kInstallAttributeName, &data));
  EXPECT_THAT(data, ElementsAreArray(kInstallAttributeData));

  // Test for unsuccessful case.
  EXPECT_CALL(*install_attrs_, Get(kInstallAttributeName, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(device_management_service_.InstallAttributesGet(
      kInstallAttributeName, &data));
}

TEST_F(DeviceManagementServiceAPITest, InstallAttributesSet) {
  // Test for successful case.
  EXPECT_CALL(*install_attrs_, Set(kInstallAttributeName,
                                   ElementsAreArray(kInstallAttributeData)))
      .WillOnce(Return(true));

  std::vector<uint8_t> data(std::begin(kInstallAttributeData),
                            std::end(kInstallAttributeData));
  EXPECT_TRUE(device_management_service_.InstallAttributesSet(
      kInstallAttributeName, data));

  // Test for unsuccessful case.
  EXPECT_CALL(*install_attrs_, Set(kInstallAttributeName,
                                   ElementsAreArray(kInstallAttributeData)))
      .WillOnce(Return(false));
  EXPECT_FALSE(device_management_service_.InstallAttributesSet(
      kInstallAttributeName, data));
}

TEST_F(DeviceManagementServiceAPITest, InstallAttributesFinalize) {
  // Test for successful case.
  EXPECT_CALL(*install_attrs_, Finalize()).WillOnce(Return(true));
  EXPECT_CALL(*install_attrs_, Get(_, _)).WillOnce(Return(true));
  EXPECT_TRUE(device_management_service_.InstallAttributesFinalize());

  // Test for unsuccessful case.
  EXPECT_CALL(*install_attrs_, Finalize()).WillOnce(Return(false));
  EXPECT_CALL(*install_attrs_, Get(_, _)).WillOnce(Return(true));
  EXPECT_FALSE(device_management_service_.InstallAttributesFinalize());
}

TEST_F(DeviceManagementServiceAPITest, InstallAttributesCount) {
  constexpr int kCount = 42;  // The Answer!!
  EXPECT_CALL(*install_attrs_, Count()).WillOnce(Return(kCount));
  EXPECT_EQ(kCount, device_management_service_.InstallAttributesCount());
}

TEST_F(DeviceManagementServiceAPITest, InstallAttributesIsSecure) {
  // Test for successful case.
  EXPECT_CALL(*install_attrs_, IsSecure()).WillOnce(Return(true));
  EXPECT_TRUE(device_management_service_.InstallAttributesIsSecure());

  // Test for unsuccessful case.
  EXPECT_CALL(*install_attrs_, IsSecure()).WillOnce(Return(false));
  EXPECT_FALSE(device_management_service_.InstallAttributesIsSecure());
}

TEST_F(DeviceManagementServiceAPITest, InstallAttributesGetStatus) {
  constexpr InstallAttributes::Status status_list[] = {
      InstallAttributes::Status::kUnknown,
      InstallAttributes::Status::kTpmNotOwned,
      InstallAttributes::Status::kFirstInstall,
      InstallAttributes::Status::kValid,
      InstallAttributes::Status::kInvalid,
  };

  for (auto s : status_list) {
    EXPECT_CALL(*install_attrs_, status()).WillOnce(Return(s));
    EXPECT_EQ(s, device_management_service_.InstallAttributesGetStatus());
  }
}

TEST_F(DeviceManagementServiceAPITest, InstallAttributesStatusToProtoEnum) {
  EXPECT_EQ(InstallAttributesState::UNKNOWN,
            DeviceManagementService::InstallAttributesStatusToProtoEnum(
                InstallAttributes::Status::kUnknown));
  EXPECT_EQ(InstallAttributesState::TPM_NOT_OWNED,
            DeviceManagementService::InstallAttributesStatusToProtoEnum(
                InstallAttributes::Status::kTpmNotOwned));
  EXPECT_EQ(InstallAttributesState::FIRST_INSTALL,
            DeviceManagementService::InstallAttributesStatusToProtoEnum(
                InstallAttributes::Status::kFirstInstall));
  EXPECT_EQ(InstallAttributesState::VALID,
            DeviceManagementService::InstallAttributesStatusToProtoEnum(
                InstallAttributes::Status::kValid));
  EXPECT_EQ(InstallAttributesState::INVALID,
            DeviceManagementService::InstallAttributesStatusToProtoEnum(
                InstallAttributes::Status::kInvalid));
  static_assert(InstallAttributesState_MAX == 4,
                "Incorrect element count in InstallAttributesState");
  static_assert(static_cast<int>(InstallAttributes::Status::kMaxValue) == 4,
                "Incorrect element count in InstallAttributes::Status");
}
}  // namespace device_management
