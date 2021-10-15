// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/cros_fp_biometrics_manager.h"

#include <algorithm>
#include <utility>

#include <base/base64.h>
#include <base/bind.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>
#include <base/test/task_environment.h>

#include "biod/biod_crypto.h"
#include "biod/biod_crypto_test_data.h"
#include "biod/cros_fp_device_interface.h"
#include "biod/mock_biod_metrics.h"
#include "biod/mock_biod_storage.h"
#include "biod/mock_cros_fp_biometrics_manager.h"
#include "biod/mock_cros_fp_device.h"

namespace biod {

namespace {
constexpr int kMaxTemplateCount = 5;
constexpr char kRecordID[] = "record0";
constexpr char kLabel[] = "label0";
}  // namespace

using crypto_test_data::kFakePositiveMatchSecret1;
using crypto_test_data::kFakePositiveMatchSecret2;
using crypto_test_data::kFakeValidationValue1;
using crypto_test_data::kFakeValidationValue2;
using crypto_test_data::kUserID;

using testing::Return;
using testing::ReturnRef;

class FakeCrosFpDevice : public CrosFpDeviceInterface {
 public:
  FakeCrosFpDevice() { positive_match_secret_ = kFakePositiveMatchSecret1; }
  // CrosFpDeviceInterface overrides:
  ~FakeCrosFpDevice() override = default;

  void SetMkbpEventCallback(MkbpCallback callback) override {}

  bool SetFpMode(const ec::FpMode& mode) override { return false; }
  ec::FpMode GetFpMode() override {
    return ec::FpMode(ec::FpMode::Mode::kModeInvalid);
  }
  base::Optional<FpStats> GetFpStats() override { return base::nullopt; }
  base::Optional<std::bitset<32>> GetDirtyMap() override {
    return base::nullopt;
  }
  bool SupportsPositiveMatchSecret() override { return true; }
  base::Optional<brillo::SecureVector> GetPositiveMatchSecret(
      int index) override {
    if (positive_match_secret_.empty()) {
      return base::nullopt;
    }
    // Zero-pad the secret if it's too short.
    brillo::SecureVector secret(FP_POSITIVE_MATCH_SECRET_BYTES, 0);
    std::copy(positive_match_secret_.begin(), positive_match_secret_.end(),
              secret.begin());
    return secret;
  }
  std::unique_ptr<VendorTemplate> GetTemplate(int index) override {
    return nullptr;
  }
  bool UploadTemplate(const VendorTemplate& tmpl) override { return false; }
  bool SetContext(std::string user_id) override { return false; }
  bool ResetContext() override { return false; }
  bool InitEntropy(bool reset) override { return false; }
  bool UpdateFpInfo() override { return true; }

  int MaxTemplateCount() override { return kMaxTemplateCount; }
  int TemplateVersion() override { return FP_TEMPLATE_FORMAT_VERSION; }
  int DeadPixelCount() override { return 0; }

  ec::EcCmdVersionSupportStatus EcCmdVersionSupported(uint16_t cmd,
                                                      uint32_t ver) override {
    return ec::EcCmdVersionSupportStatus::UNSUPPORTED;
  }

 private:
  friend class CrosFpBiometricsManagerPeer;
  brillo::SecureVector positive_match_secret_;
};

// Using a peer class to control access to the class under test is better than
// making the text fixture a friend class.
class CrosFpBiometricsManagerPeer {
 public:
  CrosFpBiometricsManagerPeer() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    const auto mock_bus = base::MakeRefCounted<dbus::MockBus>(options);

    // Set EXPECT_CALL, otherwise gmock forces an failure due to "uninteresting
    // call" because we use StrictMock.
    // https://github.com/google/googletest/blob/fb49e6c164490a227bbb7cf5223b846c836a0305/googlemock/docs/cook_book.md#the-nice-the-strict-and-the-naggy-nicestrictnaggy
    const auto power_manager_proxy =
        base::MakeRefCounted<dbus::MockObjectProxy>(
            mock_bus.get(), power_manager::kPowerManagerServiceName,
            dbus::ObjectPath(power_manager::kPowerManagerServicePath));
    EXPECT_CALL(*mock_bus,
                GetObjectProxy(
                    power_manager::kPowerManagerServiceName,
                    dbus::ObjectPath(power_manager::kPowerManagerServicePath)))
        .WillOnce(testing::Return(power_manager_proxy.get()));

    auto fake_cros_dev = std::make_unique<FakeCrosFpDevice>();
    // Keep a pointer to the fake device to manipulate it later.
    fake_cros_dev_ = fake_cros_dev.get();

    cros_fp_biometrics_manager_ = std::make_unique<CrosFpBiometricsManager>(
        PowerButtonFilter::Create(mock_bus), std::move(fake_cros_dev),
        std::make_unique<metrics::MockBiodMetrics>(),
        std::make_unique<storage::MockBiodStorage>());
  }

  // Methods to access or modify the fake device.

  void SetDevicePositiveMatchSecret(const brillo::SecureVector& new_secret) {
    fake_cros_dev_->positive_match_secret_ = new_secret;
  }

  // Add a record to cros_fp_biometrics_manager_, return the index.
  int AddRecord(int record_format_version,
                const std::string& record_id,
                const std::string& user_id,
                const std::string& label,
                const std::vector<uint8_t>& validation_value) {
    BiodStorageInterface::RecordMetadata record = {
        record_format_version, record_id, user_id, label, validation_value};
    cros_fp_biometrics_manager_->records_.emplace_back(std::move(record));
    return cros_fp_biometrics_manager_->records_.size() - 1;
  }

  bool ValidationValueEquals(int index,
                             const std::vector<uint8_t>& reference_value) {
    return cros_fp_biometrics_manager_->records_[index].validation_val ==
           reference_value;
  }

  // Methods to execute CrosFpBiometricsManager private methods.

  bool ComputeValidationValue(const brillo::SecureVector& secret,
                              const std::string& user_id,
                              std::vector<uint8_t>* out) {
    return BiodCrypto::ComputeValidationValue(secret, user_id, out);
  }

  bool ValidationValueIsCorrect(uint32_t match_idx) {
    return cros_fp_biometrics_manager_->ValidationValueIsCorrect(match_idx);
  }

  BiometricsManager::AttemptMatches CalculateMatches(int match_idx,
                                                     bool matched) {
    return cros_fp_biometrics_manager_->CalculateMatches(match_idx, matched);
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<CrosFpBiometricsManager> cros_fp_biometrics_manager_;
  FakeCrosFpDevice* fake_cros_dev_;
};

class CrosFpBiometricsManagerTest : public ::testing::Test {
 protected:
  CrosFpBiometricsManagerPeer cros_fp_biometrics_manager_peer_;
};

TEST_F(CrosFpBiometricsManagerTest, TestComputeValidationValue) {
  const std::vector<std::pair<brillo::SecureVector, std::vector<uint8_t>>>
      kSecretValidationValuePairs = {
          std::make_pair(kFakePositiveMatchSecret1, kFakeValidationValue1),
          std::make_pair(kFakePositiveMatchSecret2, kFakeValidationValue2),
      };
  for (const auto& pair : kSecretValidationValuePairs) {
    std::vector<uint8_t> validation_value;
    EXPECT_TRUE(cros_fp_biometrics_manager_peer_.ComputeValidationValue(
        pair.first, kUserID, &validation_value));
    EXPECT_EQ(validation_value, pair.second);
  }
}

TEST_F(CrosFpBiometricsManagerTest, TestValidationValueIsCorrect) {
  cros_fp_biometrics_manager_peer_.SetDevicePositiveMatchSecret(
      kFakePositiveMatchSecret1);
  int index = cros_fp_biometrics_manager_peer_.AddRecord(
      kRecordFormatVersion, kRecordID, kUserID, kLabel, kFakeValidationValue1);
  bool ret = cros_fp_biometrics_manager_peer_.ValidationValueIsCorrect(index);
  EXPECT_TRUE(ret);

  // Make the device return a wrong positive_match_secret.
  cros_fp_biometrics_manager_peer_.SetDevicePositiveMatchSecret(
      kFakePositiveMatchSecret2);
  ret = cros_fp_biometrics_manager_peer_.ValidationValueIsCorrect(index);
  EXPECT_FALSE(ret);
}

TEST_F(CrosFpBiometricsManagerTest, TestCalculateMatchesNotMatched) {
  int index = cros_fp_biometrics_manager_peer_.AddRecord(
      kRecordFormatVersion, kRecordID, kUserID, kLabel, kFakeValidationValue1);
  BiometricsManager::AttemptMatches matches =
      cros_fp_biometrics_manager_peer_.CalculateMatches(index, false);
  // If matched is false then we should report no matches.
  EXPECT_TRUE(matches.empty());
}

TEST_F(CrosFpBiometricsManagerTest, TestCalculateMatchesInvalidIndex) {
  int index = cros_fp_biometrics_manager_peer_.AddRecord(
      kRecordFormatVersion, kRecordID, kUserID, kLabel, kFakeValidationValue1);
  BiometricsManager::AttemptMatches matches =
      cros_fp_biometrics_manager_peer_.CalculateMatches(index + 1, true);
  // If index is invalid then we should report no matches.
  EXPECT_TRUE(matches.empty());
}

TEST_F(CrosFpBiometricsManagerTest, TestCalculateMatches) {
  int index = cros_fp_biometrics_manager_peer_.AddRecord(
      kRecordFormatVersion, kRecordID, kUserID, kLabel, kFakeValidationValue1);
  BiometricsManager::AttemptMatches matches =
      cros_fp_biometrics_manager_peer_.CalculateMatches(index, true);
  EXPECT_EQ(matches,
            BiometricsManager::AttemptMatches({{kUserID, {kRecordID}}}));
}

class CrosFpBiometricsManagerMockTest : public ::testing::Test {
 protected:
  CrosFpBiometricsManagerMockTest() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    const auto mock_bus = base::MakeRefCounted<dbus::MockBus>(options);

    // Set EXPECT_CALL, otherwise gmock forces an failure due to "uninteresting
    // call" because we use StrictMock.
    // https://github.com/google/googletest/blob/fb49e6c164490a227bbb7cf5223b846c836a0305/googlemock/docs/cook_book.md#the-nice-the-strict-and-the-naggy-nicestrictnaggy
    power_manager_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        mock_bus.get(), power_manager::kPowerManagerServiceName,
        dbus::ObjectPath(power_manager::kPowerManagerServicePath));
    EXPECT_CALL(*mock_bus,
                GetObjectProxy(
                    power_manager::kPowerManagerServiceName,
                    dbus::ObjectPath(power_manager::kPowerManagerServicePath)))
        .WillOnce(testing::Return(power_manager_proxy_.get()));

    // Keep a pointer to the mocks so they can be used in the tests. The
    // pointers must come after the MockCrosFpBiometricsManager pointer in the
    // class so that MockCrosFpBiometricsManager outlives the bare pointers,
    // since MockCrosFpBiometricsManager maintains ownership of the underlying
    // objects.
    auto mock_cros_fp_dev = std::make_unique<MockCrosFpDevice>();
    mock_cros_dev_ = mock_cros_fp_dev.get();
    auto mock_biod_metrics = std::make_unique<metrics::MockBiodMetrics>();
    mock_metrics_ = mock_biod_metrics.get();
    auto mock_biod_storage = std::make_unique<storage::MockBiodStorage>();
    mock_biod_storage_ = mock_biod_storage.get();

    EXPECT_CALL(*mock_cros_dev_, SupportsPositiveMatchSecret())
        .WillRepeatedly(Return(true));

    mock_ = std::make_unique<MockCrosFpBiometricsManager>(
        PowerButtonFilter::Create(mock_bus), std::move(mock_cros_fp_dev),
        std::move(mock_biod_metrics), std::move(mock_biod_storage));
    EXPECT_TRUE(mock_);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<dbus::MockObjectProxy> power_manager_proxy_;
  std::unique_ptr<MockCrosFpBiometricsManager> mock_;
  MockCrosFpDevice* mock_cros_dev_;
  metrics::MockBiodMetrics* mock_metrics_;
  storage::MockBiodStorage* mock_biod_storage_;
};

// TODO(b/187951992): The following tests for the automatic maintenance timer
// need to be re-enabled when the maintenace-auth interference is fixed.
// The tests were disabled due to b/184783529.
TEST_F(CrosFpBiometricsManagerMockTest,
       DISABLED_TestMaintenanceTimer_TooShort) {
  EXPECT_CALL(*mock_, OnMaintenanceTimerFired).Times(0);
  task_environment_.FastForwardBy(base::TimeDelta::FromHours(12));
}

TEST_F(CrosFpBiometricsManagerMockTest, DISABLED_TestMaintenanceTimer_Once) {
  EXPECT_CALL(*mock_, OnMaintenanceTimerFired).Times(1);
  task_environment_.FastForwardBy(base::TimeDelta::FromDays(1));
}

TEST_F(CrosFpBiometricsManagerMockTest,
       DISABLED_TestMaintenanceTimer_Multiple) {
  EXPECT_CALL(*mock_, OnMaintenanceTimerFired).Times(2);
  task_environment_.FastForwardBy(base::TimeDelta::FromDays(2));
}

// TODO(b/187951992): The following test must be removed when the
// maintenace-auth interference is fixed.
// This test was added when the maintenance timer was disabled due to
// b/184783529.
TEST_F(CrosFpBiometricsManagerMockTest, TestMaintenanceTimer_Disabled) {
  EXPECT_CALL(*mock_, OnMaintenanceTimerFired).Times(0);
  task_environment_.FastForwardBy(base::TimeDelta::FromDays(1));
}

TEST_F(CrosFpBiometricsManagerMockTest, TestOnMaintenanceTimerFired) {
  constexpr int kNumDeadPixels = 1;

  EXPECT_NE(mock_cros_dev_, nullptr);
  EXPECT_NE(mock_metrics_, nullptr);

  EXPECT_CALL(*mock_metrics_, SendDeadPixelCount(kNumDeadPixels)).Times(1);

  EXPECT_CALL(*mock_cros_dev_, DeadPixelCount)
      .WillOnce(testing::Return(kNumDeadPixels));

  EXPECT_CALL(*mock_cros_dev_,
              SetFpMode(ec::FpMode(ec::FpMode::Mode::kSensorMaintenance)))
      .Times(1);

  mock_->OnMaintenanceTimerFiredDelegate();
}

TEST_F(CrosFpBiometricsManagerMockTest, TestGetDirtyList_Empty) {
  EXPECT_CALL(*mock_cros_dev_, GetDirtyMap).WillOnce(Return(std::bitset<32>()));
  auto dirty_list = mock_->GetDirtyList();
  EXPECT_EQ(dirty_list, std::vector<int>());
}

TEST_F(CrosFpBiometricsManagerMockTest, TestGetDirtyList) {
  EXPECT_CALL(*mock_cros_dev_, GetDirtyMap)
      .WillOnce(Return(std::bitset<32>("1001")));
  auto dirty_list = mock_->GetDirtyList();
  EXPECT_EQ(dirty_list, (std::vector<int>{0, 3}));
}

TEST_F(CrosFpBiometricsManagerMockTest, TestUpdateTemplatesOnDisk) {
  const std::vector<int> dirty_list = {0};
  const std::unordered_set<uint32_t> suspicious_templates;

  EXPECT_CALL(*mock_cros_dev_, GetTemplate).WillOnce([](int) {
    return std::make_unique<VendorTemplate>();
  });

  BiodStorageInterface::RecordMetadata record_metadata;
  EXPECT_CALL(*mock_, GetRecordMetadata(0))
      .WillOnce(ReturnRef(record_metadata));

  EXPECT_CALL(*mock_, WriteRecord).WillOnce(Return(true));

  EXPECT_TRUE(mock_->UpdateTemplatesOnDisk(dirty_list, suspicious_templates));
}

TEST_F(CrosFpBiometricsManagerMockTest,
       TestUpdateTemplatesOnDisk_NoDirtyTemplates) {
  const std::vector<int> dirty_list;
  const std::unordered_set<uint32_t> suspicious_templates;

  EXPECT_CALL(*mock_, WriteRecord).Times(0);

  EXPECT_TRUE(mock_->UpdateTemplatesOnDisk(dirty_list, suspicious_templates));
}

TEST_F(CrosFpBiometricsManagerMockTest,
       TestUpdateTemplatesOnDisk_SkipSuspiciousTemplates) {
  const std::vector<int> dirty_list = {0};
  const std::unordered_set<uint32_t> suspicious_templates = {0};

  EXPECT_CALL(*mock_, WriteRecord).Times(0);

  EXPECT_TRUE(mock_->UpdateTemplatesOnDisk(dirty_list, suspicious_templates));
}

TEST_F(CrosFpBiometricsManagerMockTest,
       TestUpdateTemplatesOnDisk_ErrorFetchingTemplate) {
  const std::vector<int> dirty_list = {0};
  const std::unordered_set<uint32_t> suspicious_templates;

  EXPECT_CALL(*mock_cros_dev_, GetTemplate).WillOnce([](int) {
    return nullptr;
  });
  EXPECT_CALL(*mock_, WriteRecord).Times(0);

  EXPECT_TRUE(mock_->UpdateTemplatesOnDisk(dirty_list, suspicious_templates));
}

TEST_F(CrosFpBiometricsManagerMockTest, TestCallDeleteRecord) {
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillOnce(Return(5));

  EXPECT_CALL(*mock_biod_storage_, DeleteRecord);

  struct ec_fp_template_encryption_metadata Data = {0};
  Data.struct_version = 0x3;  // Correct version is zero.
  const BiodStorageInterface::RecordMetadata mock_test_recordmetadata{
      1, kRecordID, kUserID, kLabel, kFakeValidationValue1};
  const BiodStorageInterface::Record mock_test_record{
      mock_test_recordmetadata,
      base::Base64Encode(base::as_bytes(base::make_span(&Data, sizeof(Data))))};
  mock_->LoadRecord(mock_test_record);
}

TEST_F(CrosFpBiometricsManagerMockTest, TestSkipDeleteRecord) {
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillOnce(Return(5));

  EXPECT_CALL(*mock_biod_storage_, DeleteRecord).Times(0);

  struct ec_fp_template_encryption_metadata Data = {0};
  // Template version is zero because it comes from mock.
  const BiodStorageInterface::RecordMetadata mock_test_recordmetadata{
      1, kRecordID, kUserID, kLabel, kFakeValidationValue1};
  const BiodStorageInterface::Record mock_test_record{
      mock_test_recordmetadata,
      base::Base64Encode(base::as_bytes(base::make_span(&Data, sizeof(Data))))};
  mock_->LoadRecord(mock_test_record);
}

}  // namespace biod
