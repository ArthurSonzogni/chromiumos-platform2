// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dlcservice/metadata/metadata.h"
#include "dlcservice/metadata/metadata_interface.h"
#include "dlcservice/metadata/mock_compressor.h"

using testing::_;
using testing::Return;

namespace dlcservice::metadata {

constexpr char kFirstDlc[] = "first-dlc";
constexpr char kSecondDlc[] = "second-dlc";
constexpr char kThirdDlc[] = "third-dlc";
constexpr char kMetadataTemplate[] = R"("%s":{"manifest":%s,"table":"%s"},)";

class MetadataTest : public testing::Test {
 public:
  void SetUp() override {
    testing::Test::SetUp();
    CHECK(scoped_temp_dir_.CreateUniqueTempDir());
    metadata_path_ = scoped_temp_dir_.GetPath();
    base::WriteFile(
        metadata_path_.Append(std::string(kMetadataPrefix).append(kFirstDlc)),
        "Test metadata file.");

    auto compressor = std::make_unique<MockCompressor>();
    compressor_ptr_ = compressor.get();

    auto decompressor = std::make_unique<MockCompressor>();
    decompressor_ptr_ = decompressor.get();

    metadata_ = std::make_unique<Metadata>(metadata_path_, kMaxMetadataFileSize,
                                           std::move(compressor),
                                           std::move(decompressor));
    EXPECT_CALL(*compressor_ptr_, Initialize).WillOnce(Return(true));
    EXPECT_CALL(*decompressor_ptr_, Initialize).WillOnce(Return(true));
    metadata_->Initialize();
  }

 protected:
  base::ScopedTempDir scoped_temp_dir_;
  base::FilePath metadata_path_;

  std::unique_ptr<Metadata> metadata_;
  MockCompressor* compressor_ptr_;
  MockCompressor* decompressor_ptr_;
};

TEST_F(MetadataTest, GetMetadata) {
  std::string mock_metadata =
      base::StringPrintf(kMetadataTemplate, kFirstDlc, "{}", kFirstDlc);
  EXPECT_CALL(*decompressor_ptr_, Reset).WillOnce(Return(true));
  EXPECT_CALL(*decompressor_ptr_, Process).WillOnce(Return(mock_metadata));

  auto entry = metadata_->Get(kFirstDlc);
  EXPECT_TRUE(entry);
}

TEST_F(MetadataTest, GetUnsupportedMetadata) {
  std::string mock_metadata =
      base::StringPrintf(kMetadataTemplate, kFirstDlc, "{}", kFirstDlc);
  EXPECT_CALL(*decompressor_ptr_, Reset).WillOnce(Return(true));
  EXPECT_CALL(*decompressor_ptr_, Process).WillOnce(Return(mock_metadata));

  EXPECT_FALSE(metadata_->Get("unsupported-dlc"));
}

TEST_F(MetadataTest, GetMetadataDecompressionFailure) {
  EXPECT_CALL(*decompressor_ptr_, Reset).WillOnce(Return(true));
  EXPECT_CALL(*decompressor_ptr_, Process).WillOnce(Return(std::nullopt));

  EXPECT_FALSE(metadata_->Get(kFirstDlc));
}

TEST_F(MetadataTest, ModifyMetadata) {
  std::string mock_metadata_first =
      base::StringPrintf(kMetadataTemplate, kFirstDlc, "{}", kFirstDlc);
  std::string mock_metadata_second =
      base::StringPrintf(kMetadataTemplate, kSecondDlc, "{}", kSecondDlc);
  EXPECT_CALL(*decompressor_ptr_, Reset).WillOnce(Return(true));
  EXPECT_CALL(*decompressor_ptr_, Process)
      .WillOnce(Return(mock_metadata_first + mock_metadata_second));

  // Mock modifying to a small data that still fits inside one file.
  std::string modified("Modified data.");
  EXPECT_CALL(*compressor_ptr_, Process(_, true)).WillRepeatedly(Return(""));
  EXPECT_CALL(*compressor_ptr_, Process(_, false))
      .WillRepeatedly(Return(modified));
  EXPECT_CALL(*compressor_ptr_, Reset).WillRepeatedly(Return(true));

  std::unique_ptr<MockCompressor> clones[2];
  for (auto&& clone : clones) {
    clone = std::make_unique<MockCompressor>();
    EXPECT_CALL(*clone, Process(_, true)).WillOnce(Return(modified));
  }
  EXPECT_CALL(*compressor_ptr_, Clone)
      .WillOnce(Return(std::move(clones[0])))
      .WillOnce(Return(std::move(clones[1])));

  // Test setting metadata with mocked compressor and decompressor.
  Metadata::Entry entry{base::Value::Dict(), "table"};
  EXPECT_TRUE(metadata_->Set(kFirstDlc, entry));

  // The metadata file id list should be unchanged.
  const auto& file_ids = metadata_->GetFileIds();
  EXPECT_EQ(file_ids.size(), 1);

  std::string modified_file = *file_ids.begin();
  EXPECT_TRUE(base::ReadFileToString(
      metadata_path_.Append(std::string(kMetadataPrefix).append(kFirstDlc)),
      &modified_file));

  EXPECT_EQ(modified_file, modified + modified);
}

TEST_F(MetadataTest, ModifyMetadataToLargerContent) {
  std::string mock_metadata_first =
      base::StringPrintf(kMetadataTemplate, kFirstDlc, "{}", kFirstDlc);
  std::string mock_metadata_second =
      base::StringPrintf(kMetadataTemplate, kSecondDlc, "{}", kSecondDlc);
  EXPECT_CALL(*decompressor_ptr_, Reset).WillOnce(Return(true));
  EXPECT_CALL(*decompressor_ptr_, Process)
      .WillOnce(Return(mock_metadata_first + mock_metadata_second));

  // Mock modifying to larger data that causes new file to be created.
  std::string modified(kMaxMetadataFileSize / 2 + 1, 'x');
  EXPECT_CALL(*compressor_ptr_, Process(_, true)).WillRepeatedly(Return(""));
  EXPECT_CALL(*compressor_ptr_, Process(_, false))
      .WillRepeatedly(Return(modified));
  EXPECT_CALL(*compressor_ptr_, Reset).WillRepeatedly(Return(true));

  std::unique_ptr<MockCompressor> clones[3];
  for (auto&& clone : clones) {
    clone = std::make_unique<MockCompressor>();
    EXPECT_CALL(*clone, Process(_, true)).WillOnce(Return(modified));
  }
  EXPECT_CALL(*compressor_ptr_, Clone)
      .WillOnce(Return(std::move(clones[0])))
      .WillOnce(Return(std::move(clones[1])))
      .WillOnce(Return(std::move(clones[2])));

  Metadata::Entry entry{base::Value::Dict(), "table"};
  EXPECT_TRUE(metadata_->Set(kFirstDlc, entry));

  // Verified that a new file is created.
  const auto& file_ids = metadata_->GetFileIds();
  EXPECT_GT(file_ids.size(), 1);

  for (const auto& f_id : file_ids) {
    std::string modified_file;
    EXPECT_TRUE(base::ReadFileToString(
        metadata_path_.Append(std::string(kMetadataPrefix).append(f_id)),
        &modified_file));
    EXPECT_EQ(modified_file, modified);
  }
}

TEST_F(MetadataTest, ListAndFilterDlcIds) {
  std::string mock_metadata1 =
      base::StringPrintf(kMetadataTemplate, kFirstDlc,
                         "{\"factory-install\":\"str_val\"}", kFirstDlc);
  std::string mock_metadata2 = base::StringPrintf(
      kMetadataTemplate, kSecondDlc, "{\"preload-allowed\":true}", kSecondDlc);
  std::string mock_metadata3 = base::StringPrintf(
      kMetadataTemplate, kThirdDlc, "{\"powerwash-safe\":123}", kThirdDlc);
  EXPECT_CALL(*decompressor_ptr_, Reset).WillOnce(Return(true));
  EXPECT_CALL(*decompressor_ptr_, Process)
      .WillRepeatedly(Return(mock_metadata1 + mock_metadata2 + mock_metadata3));

  EXPECT_EQ(metadata_->ListDlcIds(Metadata::FilterKey::kNone, base::Value()),
            std::vector<DlcId>({kFirstDlc, kSecondDlc, kThirdDlc}));
  EXPECT_EQ(metadata_->ListDlcIds(Metadata::FilterKey::kFactoryInstall,
                                  base::Value("str_val")),
            std::vector<DlcId>({kFirstDlc}));
  EXPECT_EQ(metadata_->ListDlcIds(Metadata::FilterKey::kPreloadAllowed,
                                  base::Value(true)),
            std::vector<DlcId>({kSecondDlc}));
  EXPECT_EQ(metadata_->ListDlcIds(Metadata::FilterKey::kPowerwashSafe,
                                  base::Value(123)),
            std::vector<DlcId>({kThirdDlc}));
}

}  // namespace dlcservice::metadata
