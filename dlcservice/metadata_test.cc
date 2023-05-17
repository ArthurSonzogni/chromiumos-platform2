// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dlcservice/metadata.h"
#include "dlcservice/mock_compressor.h"
#include "dlcservice/test_utils.h"
#include "dlcservice/types.h"

using testing::_;
using testing::Return;

namespace dlcservice {

constexpr char kMetadataTemplate[] = R"("%s":{"manifest":%s,"table":"%s"},)";

class MetadataTest : public BaseTest {
 public:
  void SetUp() override {
    BaseTest::SetUp();
    auto compressor = std::make_unique<MockCompressor>();
    compressor_ptr_ = compressor.get();

    auto decompressor = std::make_unique<MockCompressor>();
    decompressor_ptr_ = decompressor.get();

    metadata_ = std::make_unique<Metadata>(manifest_path_, kMaxMetadataFileSize,
                                           std::move(compressor),
                                           std::move(decompressor));
    EXPECT_CALL(*compressor_ptr_, Initialize).WillOnce(Return(true));
    EXPECT_CALL(*decompressor_ptr_, Initialize).WillOnce(Return(true));
    metadata_->Initialize();
  }

 protected:
  std::unique_ptr<Metadata> metadata_;
  MockCompressor* compressor_ptr_;
  MockCompressor* decompressor_ptr_;
};

TEST_F(MetadataTest, GetMetadata) {
  for (const auto& id : supported_dlc_) {
    // Read manifest from original test data as a reference.
    std::string manifest_str;
    EXPECT_TRUE(base::ReadFileToString(
        JoinPaths(manifest_path_, id, kPackage, kManifestName), &manifest_str));
    imageloader::Manifest manifest_ref;
    manifest_ref.ParseManifest(manifest_str);

    // Mock metadata decompression by returning a metadata string directly
    // retrievd from the original test data.
    std::string mock_metadata = base::StringPrintf(
        kMetadataTemplate, id.c_str(), manifest_str.c_str(), id.c_str());
    EXPECT_CALL(*decompressor_ptr_, Reset).WillOnce(Return(true));
    EXPECT_CALL(*decompressor_ptr_, Process).WillOnce(Return(mock_metadata));

    auto entry = metadata_->Get(id);
    EXPECT_TRUE(entry);
    EXPECT_EQ(entry->table, id);

    imageloader::Manifest manifest;
    manifest.ParseManifest(entry->manifest);
    EXPECT_EQ(manifest, manifest_ref);
  }
}

TEST_F(MetadataTest, GetUnsupportedMetadata) {
  std::string table;
  imageloader::Manifest manifest;

  std::string mock_metadata =
      base::StringPrintf(kMetadataTemplate, kFirstDlc, "{}", kFirstDlc);
  EXPECT_CALL(*decompressor_ptr_, Reset).WillOnce(Return(true));
  EXPECT_CALL(*decompressor_ptr_, Process).WillOnce(Return(mock_metadata));

  EXPECT_FALSE(metadata_->Get("unsupported-dlc"));
}

TEST_F(MetadataTest, GetMetadataDecompressionFailure) {
  std::string table;
  imageloader::Manifest manifest;

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
      JoinPaths(manifest_path_, std::string(kMetadataPrefix).append(kFirstDlc)),
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
        JoinPaths(manifest_path_, std::string(kMetadataPrefix).append(f_id)),
        &modified_file));
    EXPECT_EQ(modified_file, modified);
  }
}

}  // namespace dlcservice
