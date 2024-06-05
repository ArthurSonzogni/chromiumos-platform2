// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libpmt/pmt.h"

namespace pmt {
namespace {

using testing::Return;

static constexpr Guid kId = 0x130671b2;
static constexpr Guid kId1 = 0x130670b2;
static constexpr Guid kId2 = 0x1a067102;
static constexpr Guid kId3 = 0x1a067002;
static constexpr char kTelemDataPath[] = "testdata/test_telem_data";
static constexpr size_t kTelemDataPathSize = 3352;

class DataInterfaceMock : public PmtDataInterface {
 public:
  MOCK_METHOD(std::vector<Guid>, DetectDevices, ());
  MOCK_CONST_METHOD0(GetMetadataMappingsFile, base::FilePath());
  MOCK_CONST_METHOD1(IsValid, bool(Guid guid));
  MOCK_CONST_METHOD1(GetTelemetryFile,
                     const std::optional<base::FilePath>(Guid guid));
  MOCK_CONST_METHOD1(GetTelemetrySize, const size_t(Guid guid));
};

class pmtTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Will be deleted by the unique_ptr.
    data_mock_ = new DataInterfaceMock();
    pmt_ = std::make_unique<PmtCollector>(
        std::unique_ptr<PmtDataInterface>(data_mock_));
  }

  DataInterfaceMock* data_mock_;
  std::unique_ptr<PmtCollector> pmt_;
};

TEST_F(pmtTest, GuidDetection) {
  std::vector<Guid> good_id = {kId};
  EXPECT_CALL(*data_mock_, DetectDevices).WillOnce(Return(good_id));
  auto result = pmt_->DetectDevices();
  ASSERT_EQ(result.size(), 1);
  ASSERT_EQ(result[0], kId);

  std::vector<Guid> no_id = {};
  EXPECT_CALL(*data_mock_, DetectDevices).WillOnce(Return(no_id));
  result = pmt_->DetectDevices();
  ASSERT_EQ(result.size(), 0);
}

TEST_F(pmtTest, SetupCollectionWithNoGuids) {
  std::vector<Guid> guids = {};
  auto result = pmt_->SetUpCollection(guids);

  ASSERT_EQ(result, -EINVAL);
}

TEST_F(pmtTest, SetupCollectionWithInvalidGuid) {
  std::vector<Guid> guids = {kId, kId2};
  // Simulate that the second ID is invalid.
  EXPECT_CALL(*data_mock_, IsValid(kId)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, IsValid(kId2)).WillOnce(Return(false));
  auto result = pmt_->SetUpCollection(guids);

  ASSERT_EQ(result, -EINVAL);
}

TEST_F(pmtTest, SetupCollectionWithMissingTelemetryFile) {
  std::vector<Guid> guids = {kId, kId2};
  const int data_size = 100;
  std::optional<base::FilePath> no_telem_file;

  EXPECT_CALL(*data_mock_, IsValid(kId)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, IsValid(kId2)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, GetTelemetrySize).WillOnce(Return(data_size));
  EXPECT_CALL(*data_mock_, GetTelemetryFile).WillOnce(Return(no_telem_file));
  auto result = pmt_->SetUpCollection(guids);

  ASSERT_EQ(result, -EBADF);
}

TEST_F(pmtTest, SetupCollectionWithInvalidTelemetryFile) {
  std::vector<Guid> guids = {kId, kId2};
  const int data_size = 100;
  base::FilePath telem_data_path("invalid_file");

  EXPECT_CALL(*data_mock_, IsValid(kId)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, IsValid(kId2)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, GetTelemetrySize).WillOnce(Return(data_size));
  EXPECT_CALL(*data_mock_, GetTelemetryFile).WillOnce(Return(telem_data_path));
  auto result = pmt_->SetUpCollection(guids);

  ASSERT_EQ(result, -EBADF);
}

TEST_F(pmtTest, SetupCollection) {
  std::vector<Guid> guids = {kId, kId2};
  const int data_size_id = 100, data_size_id2 = 200;
  base::FilePath telem_data_path(kTelemDataPath);
  EXPECT_CALL(*data_mock_, IsValid(kId)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, IsValid(kId2)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, GetTelemetrySize(kId))
      .WillOnce(Return(data_size_id));
  EXPECT_CALL(*data_mock_, GetTelemetrySize(kId2))
      .WillOnce(Return(data_size_id2));
  EXPECT_CALL(*data_mock_, GetTelemetryFile(kId))
      .WillOnce(Return(telem_data_path));
  EXPECT_CALL(*data_mock_, GetTelemetryFile(kId2))
      .WillOnce(Return(telem_data_path));
  auto result = pmt_->SetUpCollection(guids);

  ASSERT_EQ(result, 0);
  ASSERT_EQ(pmt_->GetData()->devices_size(), 2);
  ASSERT_EQ(pmt_->GetData()->devices(0).guid(), kId);
  ASSERT_EQ(pmt_->GetData()->devices(0).data().size(), data_size_id);
  ASSERT_EQ(pmt_->GetData()->devices(1).guid(), kId2);
  ASSERT_EQ(pmt_->GetData()->devices(1).data().size(), data_size_id2);

  // Second call should fail because we're set up already.
  result = pmt_->SetUpCollection(guids);
  ASSERT_EQ(result, -EBUSY);
}

TEST_F(pmtTest, CleanUpCollection) {
  // If nothing is setup clean should fail.
  auto result = pmt_->CleanUpCollection();
  ASSERT_EQ(result, -ENOENT);
  ASSERT_EQ(pmt_->GetData(), nullptr);

  // First set up the collection.
  std::vector<Guid> guids = {kId, kId2};
  const int data_size_id = 100, data_size_id2 = 200;
  base::FilePath telem_data_path(kTelemDataPath);
  EXPECT_CALL(*data_mock_, IsValid(kId)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, IsValid(kId2)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, GetTelemetrySize(kId))
      .WillOnce(Return(data_size_id));
  EXPECT_CALL(*data_mock_, GetTelemetrySize(kId2))
      .WillOnce(Return(data_size_id2));
  EXPECT_CALL(*data_mock_, GetTelemetryFile(kId))
      .WillOnce(Return(telem_data_path));
  EXPECT_CALL(*data_mock_, GetTelemetryFile(kId2))
      .WillOnce(Return(telem_data_path));
  result = pmt_->SetUpCollection(guids);

  // Check if the setup is correct.
  ASSERT_EQ(result, 0);
  ASSERT_NE(pmt_->GetData(), nullptr);
  ASSERT_EQ(pmt_->GetData()->devices_size(), 2);
  ASSERT_EQ(pmt_->GetData()->devices(0).guid(), kId);
  ASSERT_EQ(pmt_->GetData()->devices(0).data().size(), data_size_id);
  ASSERT_EQ(pmt_->GetData()->devices(1).guid(), kId2);
  ASSERT_EQ(pmt_->GetData()->devices(1).data().size(), data_size_id2);
  // Now clean up.
  result = pmt_->CleanUpCollection();
  ASSERT_EQ(pmt_->GetData(), nullptr);
  ASSERT_EQ(result, 0);

  // Now set it up again, it should work.
  // First set up the collection.
  EXPECT_CALL(*data_mock_, IsValid(kId)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, IsValid(kId2)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, GetTelemetrySize(kId))
      .WillOnce(Return(data_size_id));
  EXPECT_CALL(*data_mock_, GetTelemetrySize(kId2))
      .WillOnce(Return(data_size_id2));
  EXPECT_CALL(*data_mock_, GetTelemetryFile(kId))
      .WillOnce(Return(telem_data_path));
  EXPECT_CALL(*data_mock_, GetTelemetryFile(kId2))
      .WillOnce(Return(telem_data_path));
  result = pmt_->SetUpCollection(guids);

  // Check if the setup is correct.
  ASSERT_EQ(result, 0);
  ASSERT_EQ(pmt_->GetData()->devices_size(), 2);
  ASSERT_EQ(pmt_->GetData()->devices(0).guid(), kId);
  ASSERT_EQ(pmt_->GetData()->devices(0).data().size(), data_size_id);
  ASSERT_EQ(pmt_->GetData()->devices(1).guid(), kId2);
  ASSERT_EQ(pmt_->GetData()->devices(1).data().size(), data_size_id2);
}

TEST_F(pmtTest, CollectionSetupIsSortedByGuid) {
  std::vector<Guid> guids = {kId, kId1, kId2, kId3};
  const int data_size = 100;
  base::FilePath telem_data_path(kTelemDataPath);
  EXPECT_CALL(*data_mock_, IsValid(kId)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, IsValid(kId1)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, IsValid(kId2)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, IsValid(kId3)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, GetTelemetrySize).WillRepeatedly(Return(data_size));
  EXPECT_CALL(*data_mock_, GetTelemetryFile)
      .WillRepeatedly(Return(telem_data_path));
  auto result = pmt_->SetUpCollection(guids);

  ASSERT_EQ(result, 0);
  ASSERT_EQ(pmt_->GetData()->devices_size(), 4);
  ASSERT_EQ(pmt_->GetData()->devices(0).guid(), kId1);
  ASSERT_EQ(pmt_->GetData()->devices(1).guid(), kId);
  ASSERT_EQ(pmt_->GetData()->devices(2).guid(), kId3);
  ASSERT_EQ(pmt_->GetData()->devices(3).guid(), kId2);
}

TEST_F(pmtTest, TakeSnapshot) {
  std::vector<Guid> guids = {kId};
  const int data_size = kTelemDataPathSize;
  base::FilePath telem_data_path(kTelemDataPath);
  EXPECT_CALL(*data_mock_, IsValid(kId)).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, GetTelemetrySize(kId)).WillOnce(Return(data_size));
  EXPECT_CALL(*data_mock_, GetTelemetryFile).WillOnce(Return(telem_data_path));

  auto result = pmt_->SetUpCollection(guids);
  ASSERT_EQ(result, 0);

  auto res = pmt_->TakeSnapshot();
  ASSERT_EQ(res, 0);

  ASSERT_EQ(pmt_->GetData()->devices_size(), 1);
  ASSERT_EQ(pmt_->GetData()->devices(0).guid(), kId);
  ASSERT_EQ(pmt_->GetData()->devices(0).data().size(), data_size);

  // Verify that the contents are the same.
  std::string expected_telem_data;
  ASSERT_TRUE(base::ReadFileToString(telem_data_path, &expected_telem_data));
  ASSERT_EQ(pmt_->GetData()->devices(0).data(), expected_telem_data);
}

TEST_F(pmtTest, TakeSnapshotHandleEOF) {
  std::vector<Guid> guids = {kId};
  const int data_size = kTelemDataPathSize + 1;
  base::FilePath telem_data_path(kTelemDataPath);
  EXPECT_CALL(*data_mock_, IsValid).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, GetTelemetrySize).WillOnce(Return(data_size));
  EXPECT_CALL(*data_mock_, GetTelemetryFile).WillOnce(Return(telem_data_path));
  auto result = pmt_->SetUpCollection(guids);
  ASSERT_EQ(result, 0);

  result = pmt_->TakeSnapshot();
  ASSERT_EQ(result, -EIO);
}

TEST_F(pmtTest, TakeSnapshotHandleBadFilePath) {
  std::vector<Guid> guids = {kId};
  const int data_size = kTelemDataPathSize;
  base::FilePath telem_data_path("bad/path");
  EXPECT_CALL(*data_mock_, IsValid).WillOnce(Return(true));
  EXPECT_CALL(*data_mock_, GetTelemetrySize).WillOnce(Return(data_size));
  EXPECT_CALL(*data_mock_, GetTelemetryFile).WillOnce(Return(telem_data_path));
  auto result = pmt_->SetUpCollection(guids);
  ASSERT_EQ(result, -EBADF);
}

TEST_F(pmtTest, HandleTakesnapshotBeforeSetup) {
  auto result = pmt_->TakeSnapshot();
  ASSERT_EQ(result, -EPERM);
}

}  // namespace
}  // namespace pmt
