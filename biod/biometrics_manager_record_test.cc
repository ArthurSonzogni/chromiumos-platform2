// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "biod/biometrics_manager_record.h"
#include "biod/mock_biod_metrics.h"
#include "biod/mock_biometrics_manager.h"
#include "biod/mock_cros_fp_biometrics_manager.h"
#include "biod/mock_cros_fp_device.h"
#include "biod/mock_cros_fp_record_manager.h"
#include "dbus/mock_bus.h"
#include "dbus/mock_object_proxy.h"

#include <base/environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/test/task_environment.h>

using testing::Return;

namespace biod {
namespace {

const char kRecordId1[] = "00000000_0000_0000_0000_000000000001";
const char kUserId1[] = "0000000000000000000000000000000000000001";
const char kLabel1[] = "record1";
const std::vector<uint8_t> kValidationVal1 = {0x00, 0x01};

const char kLabel2[] = "record2";

using RecordMetadata = BiodStorageInterface::RecordMetadata;

class BiometricsManagerRecordMockTest : public ::testing::Test {
 protected:
  BiometricsManagerRecordMockTest() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    const auto mock_bus = base::MakeRefCounted<dbus::MockBus>(options);

    power_manager_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        mock_bus.get(), power_manager::kPowerManagerServiceName,
        dbus::ObjectPath(power_manager::kPowerManagerServicePath));
    ON_CALL(*mock_bus,
            GetObjectProxy(
                power_manager::kPowerManagerServiceName,
                dbus::ObjectPath(power_manager::kPowerManagerServicePath)))
        .WillByDefault(testing::Return(power_manager_proxy_.get()));

    // Keep a pointer to the mocks so they can be used in the tests. The
    // pointers must come after the MockCrosFpBiometricsManager pointer in the
    // class so that MockCrosFpBiometricsManager outlives the bare pointers,
    // since MockCrosFpBiometricsManager maintains ownership of the underlying
    // objects.
    auto mock_cros_fp_dev = std::make_unique<MockCrosFpDevice>();
    mock_cros_dev_ = mock_cros_fp_dev.get();
    auto mock_record_manager = std::make_unique<MockCrosFpRecordManager>();
    mock_record_manager_ = mock_record_manager.get();

    mock_metrics_ = std::make_unique<metrics::MockBiodMetrics>();
    ON_CALL(*mock_cros_dev_, SupportsPositiveMatchSecret())
        .WillByDefault(Return(true));

    mock_crosfp_biometrics_manager_ =
        std::make_unique<MockCrosFpBiometricsManager>(
            PowerButtonFilter::Create(mock_bus), std::move(mock_cros_fp_dev),
            mock_metrics_.get(), std::move(mock_record_manager));
    EXPECT_TRUE(mock_crosfp_biometrics_manager_);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<dbus::MockObjectProxy> power_manager_proxy_;
  std::unique_ptr<metrics::MockBiodMetrics> mock_metrics_;
  std::unique_ptr<MockCrosFpBiometricsManager> mock_crosfp_biometrics_manager_;
  MockCrosFpDevice* mock_cros_dev_;
  MockCrosFpRecordManager* mock_record_manager_;
};

TEST_F(BiometricsManagerRecordMockTest, GetId) {
  std::string record_id = kRecordId1;

  BiometricsManagerRecord biometrics_manager_record1(
      mock_crosfp_biometrics_manager_->GetWeakFactoryPtr(), record_id);

  EXPECT_EQ(biometrics_manager_record1.GetId(), record_id);
}

TEST_F(BiometricsManagerRecordMockTest, GetUserId) {
  RecordMetadata record_metadata(
      {kRecordFormatVersion, kRecordId1, kUserId1, kLabel1, kValidationVal1});

  std::string record_id = kRecordId1;

  BiometricsManagerRecord biometrics_manager_record1(
      mock_crosfp_biometrics_manager_->GetWeakFactoryPtr(), record_id);

  EXPECT_CALL(*mock_crosfp_biometrics_manager_, GetRecordMetadata(record_id))
      .WillOnce(Return(record_metadata));
  EXPECT_EQ(biometrics_manager_record1.GetUserId(), std::string(kUserId1));
}

TEST_F(BiometricsManagerRecordMockTest, GetLabel) {
  RecordMetadata record_metadata(
      {kRecordFormatVersion, kRecordId1, kUserId1, kLabel1, kValidationVal1});

  std::string record_id = kRecordId1;

  BiometricsManagerRecord biometrics_manager_record1(
      mock_crosfp_biometrics_manager_->GetWeakFactoryPtr(), record_id);

  EXPECT_CALL(*mock_crosfp_biometrics_manager_, GetRecordMetadata(record_id))
      .WillOnce(Return(record_metadata));
  EXPECT_EQ(biometrics_manager_record1.GetLabel(), std::string(kLabel1));
}

TEST_F(BiometricsManagerRecordMockTest, GetValidationVal) {
  RecordMetadata record_metadata(
      {kRecordFormatVersion, kRecordId1, kUserId1, kLabel1, kValidationVal1});

  std::string record_id = kRecordId1;

  BiometricsManagerRecord biometrics_manager_record1(
      mock_crosfp_biometrics_manager_->GetWeakFactoryPtr(), record_id);

  EXPECT_CALL(*mock_crosfp_biometrics_manager_, GetRecordMetadata(record_id))
      .WillOnce(Return(record_metadata));
  EXPECT_EQ(biometrics_manager_record1.GetValidationVal(), kValidationVal1);
}

TEST_F(BiometricsManagerRecordMockTest, SetLabel) {
  RecordMetadata record_metadata1(
      {kRecordFormatVersion, kRecordId1, kUserId1, kLabel1, kValidationVal1});

  RecordMetadata record_metadata2(
      {kRecordFormatVersion, kRecordId1, kUserId1, kLabel2, kValidationVal1});

  std::string record_id = kRecordId1;

  BiometricsManagerRecord biometrics_manager_record1(
      mock_crosfp_biometrics_manager_->GetWeakFactoryPtr(), record_id);

  EXPECT_CALL(*mock_crosfp_biometrics_manager_, GetRecordMetadata(record_id))
      .WillOnce(Return(record_metadata1));

  // TODO(b/288577667): Add test for UpdateRecordMetadata.
  EXPECT_CALL(*mock_record_manager_, UpdateRecordMetadata(record_metadata2))
      .WillOnce(Return(true));

  EXPECT_TRUE(biometrics_manager_record1.SetLabel(kLabel2));
}

TEST_F(BiometricsManagerRecordMockTest, Remove) {
  RecordMetadata record_metadata1(
      {kRecordFormatVersion, kRecordId1, kUserId1, kLabel1, kValidationVal1});
  std::string record_id = kRecordId1;

  BiometricsManagerRecord biometrics_manager_record1(
      mock_crosfp_biometrics_manager_->GetWeakFactoryPtr(), record_id);

  EXPECT_CALL(*mock_record_manager_, GetRecordMetadata(record_id))
      .WillOnce(Return(record_metadata1));

  EXPECT_CALL(*mock_record_manager_, DeleteRecord(record_id))
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_crosfp_biometrics_manager_,
              ReadRecordsForSingleUser(kUserId1))
      .WillOnce(Return(true));

  EXPECT_TRUE(biometrics_manager_record1.Remove());
}

}  // namespace
}  // namespace biod
