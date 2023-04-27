// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/log_store_manifest.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::Optional;

namespace minios {

const uint64_t kPartitionSize = 262144 * kBlockSize;

class LogStoreManifestTest : public testing::Test {
 protected:
  void SetUp() override { CHECK(scoped_temp_dir_.CreateUniqueTempDir()); }

  // Helper function to create files of required size.
  base::FilePath CreateFileOfSize(uint64_t size) {
    base::FilePath file_path;
    auto file = base::CreateAndOpenTemporaryFileInDir(
        scoped_temp_dir_.GetPath(), &file_path);
    EXPECT_TRUE(file.IsValid());
    file.SetLength(size);
    return file_path;
  }

  base::ScopedTempDir scoped_temp_dir_;
};

TEST_F(LogStoreManifestTest, VerifyGenerate) {
  auto log_file = CreateFileOfSize(1025);
  const uint64_t kernel_size = 2;
  const uint64_t log_store_offset = 5;
  auto disk_file = CreateFileOfSize(10 * 1024);
  LogStoreManifest manifest_store{log_file, disk_file, kernel_size,
                                  kPartitionSize};
  EXPECT_TRUE(manifest_store.Generate(log_store_offset));
  auto generated_manifest = manifest_store.manifest_;

  ASSERT_TRUE(generated_manifest.has_value());

  EXPECT_EQ(generated_manifest->entry().count(), 1025);
  EXPECT_EQ(generated_manifest->entry().offset(), log_store_offset);
}

TEST_F(LogStoreManifestTest, DisabledWithInvalidArgs) {
  // Manifest helper should be disabled if args are erroneos.
  const uint64_t kernel_size = 2;
  const auto disk_file = CreateFileOfSize(10 * 1024);
  // Error: Empty log archive path.
  LogStoreManifest empty_log_archive{base::FilePath{""}, disk_file, kernel_size,
                                     kPartitionSize};
  EXPECT_FALSE(empty_log_archive.IsValid());
  EXPECT_FALSE(empty_log_archive.Generate(10));
  EXPECT_FALSE(empty_log_archive.Write());
  EXPECT_EQ(empty_log_archive.Retreive(), std::nullopt);

  LogStoreManifest empty_disk_path{base::FilePath{"log"}, base::FilePath{""},
                                   kernel_size, kPartitionSize};
  EXPECT_FALSE(empty_disk_path.IsValid());

  // Manifest store within kernel block
  LogStoreManifest manifest_in_kernel{base::FilePath{"log"}, disk_file,
                                      kBlockSize + 1, 3 * kBlockSize};
  EXPECT_FALSE(manifest_in_kernel.IsValid());

  // Unaligned partition sizes are not supported.
  LogStoreManifest unaligned_partition_size{
      base::FilePath{"log"}, disk_file, kBlockSize + 1, (10 * kBlockSize) + 1};
  EXPECT_EQ(unaligned_partition_size.IsValid(), false);

  LogStoreManifest disk_open_fails{base::FilePath{"log"},
                                   base::FilePath{"unopenable_file"},
                                   kBlockSize, 3 * kBlockSize};
  EXPECT_FALSE(disk_open_fails.IsValid());
}

TEST_F(LogStoreManifestTest, WriteFailsWithoutGenerate) {
  const uint64_t kernel_size = 2;
  auto disk_file = CreateFileOfSize(10 * 1024);
  LogStoreManifest manifest_store{base::FilePath{"log_file"}, disk_file,
                                  kernel_size, kPartitionSize};

  EXPECT_TRUE(manifest_store.IsValid());
  // This method should return false without a Generate() since there's nothing
  // to write.
  EXPECT_FALSE(manifest_store.Write());
}

TEST_F(LogStoreManifestTest, VerifyWriteAndRetrieve) {
  auto log_file = CreateFileOfSize(1025);
  // Set sizes and offsets to not be perfectly block aligned to verify block
  // math.
  const uint64_t kernel_size = (2 * kBlockSize) + 1;
  const uint64_t log_store_offset = (5 * kBlockSize) + 256;
  const uint64_t partition_size = 100 * kBlockSize;
  auto disk_file = CreateFileOfSize(partition_size);

  LogStoreManifest manifest_store{log_file, disk_file, kernel_size,
                                  partition_size};
  EXPECT_TRUE(manifest_store.Generate(log_store_offset));
  auto generated_manifest = manifest_store.manifest_;

  // Write the generated manifest to file.
  EXPECT_TRUE(manifest_store.Write());
  // Expect the same manfiest to be read back from file.
  auto retrieved_manifest = manifest_store.Retreive();
  ASSERT_TRUE(retrieved_manifest.has_value());
  EXPECT_EQ(retrieved_manifest->SerializeAsString(),
            generated_manifest->SerializeAsString());
}

TEST_F(LogStoreManifestTest, VerifyClear) {
  const uint64_t kernel_size = kBlockSize;
  const uint64_t partition_size = 20 * kBlockSize;
  auto log_file = CreateFileOfSize(1025);
  auto disk_file = CreateFileOfSize(partition_size);
  LogStoreManifest manifest_store{log_file, disk_file, kernel_size,
                                  partition_size};

  EXPECT_TRUE(manifest_store.Generate(1024));
  // Find the manifest magic where we expect to.
  EXPECT_TRUE(manifest_store.Write());
  EXPECT_THAT(manifest_store.FindManifestMagic(),
              Optional(manifest_store.manifest_store_start_));

  manifest_store.Clear();
  EXPECT_EQ(manifest_store.FindManifestMagic(), std::nullopt);
}

}  // namespace minios
