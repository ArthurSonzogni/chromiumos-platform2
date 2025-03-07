// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/service_common.h"

#include <unordered_map>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace vm_tools {
namespace concierge {

TEST(ServiceCommonTest, TestValidOwnerId) {
  EXPECT_EQ(IsValidOwnerId("abdcefABCDEF0123456789"), true);
}

TEST(ServiceCommonTest, TestEmptyOwnerId) {
  EXPECT_EQ(IsValidOwnerId(""), false);
}

TEST(ServiceCommonTest, TestInvalidOwnerId) {
  EXPECT_EQ(IsValidOwnerId("Invalid"), false);
  EXPECT_EQ(IsValidOwnerId("abcd/../012345"), false);
}

TEST(ServiceCommonTest, TestValidVmName) {
  EXPECT_EQ(IsValidVmName("A Valid VM"), true);
}

TEST(ServiceCommonTest, TestEmptyVmName) {
  EXPECT_EQ(IsValidVmName(""), false);
}

// Check we get a failure while retrieving the pflash path for an invalid owner
// id.
TEST(ServiceCommonTest, TestGetPflashMetadataInvalidOwnerId) {
  base::ScopedTempDir temp_root_dir;
  EXPECT_TRUE(temp_root_dir.CreateUniqueTempDir());
  base::FilePath test_root_dir = temp_root_dir.GetPath();

  base::FilePath test_vm_resources_dir =
      test_root_dir.Append(kCrosvmDir).Append(kValidCryptoHomeCharacters);
  EXPECT_TRUE(CreateDirectory(test_vm_resources_dir));

  // Invalid owner id should yield failure."
  std::string invalid_owner_id =
      std::string(kValidCryptoHomeCharacters) + "/./";
  VmId vm_id(invalid_owner_id, "123bru");
  std::optional<PflashMetadata> pflash_metadata_result =
      GetPflashMetadata(vm_id, test_root_dir);
  EXPECT_FALSE(pflash_metadata_result.has_value());
}

// Check the pflash path for a VM.
TEST(ServiceCommonTest, TestGetPflashMetadataSuccess) {
  base::ScopedTempDir temp_root_dir;
  EXPECT_TRUE(temp_root_dir.CreateUniqueTempDir());
  base::FilePath test_root_dir = temp_root_dir.GetPath();

  base::FilePath test_vm_resources_dir =
      test_root_dir.Append(kCrosvmDir).Append(kValidCryptoHomeCharacters);
  EXPECT_TRUE(CreateDirectory(test_vm_resources_dir));

  // Check the pflash path for a VM."
  std::unordered_map<std::string, std::string> vm_name_to_base64 = {
      {"bru", "YnJ1"}, {"foo", "Zm9v"}};
  for (const auto& kv : vm_name_to_base64) {
    VmId vm_id(kValidCryptoHomeCharacters, kv.first);
    std::optional<PflashMetadata> pflash_metadata_result =
        GetPflashMetadata(vm_id, test_root_dir);
    EXPECT_TRUE(pflash_metadata_result.has_value());
    EXPECT_FALSE(pflash_metadata_result->is_installed);
    // The base64 value for the VM name "bru" is "YnJ1".
    EXPECT_EQ(pflash_metadata_result->path,
              test_vm_resources_dir.Append(kv.second +
                                           std::string(kPflashImageExtension)));
  }
}

}  // namespace concierge
}  // namespace vm_tools
