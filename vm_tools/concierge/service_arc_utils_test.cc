// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/service_arc_utils.h"

#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>

#include "gtest/gtest.h"

namespace vm_tools {
namespace concierge {

namespace {

StartArcVmRequest CreateRequest(std::vector<std::string> disk_paths) {
  StartArcVmRequest request;
  for (const std::string& path : disk_paths) {
    auto* disk = request.add_disks();
    disk->set_path(path);
  }
  return request;
}

}  // namespace

TEST(ServiceArcUtilsTest, GetCryptohomePath) {
  EXPECT_EQ(GetCryptohomePath("deadbeef"),
            base::FilePath("/run/daemon-store/crosvm/deadbeef"));
}

TEST(ServiceArcUtilsTest, GetPstoreDest) {
  EXPECT_EQ(
      GetPstoreDest("deadbeef"),
      base::FilePath("/run/daemon-store/crosvm/deadbeef/YXJjdm0=.pstore"));
}

TEST(ServiceArcUtilsTest, GetVmmSwapUsageHistoryPath) {
  EXPECT_EQ(GetVmmSwapUsageHistoryPath("deadbeef"),
            base::FilePath(
                "/run/daemon-store/crosvm/deadbeef/arcvm.vmm_swap_history"));
}

TEST(ServiceArcUtilsTest, GetFileContents) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  std::string contents;

  // Test nonexistent file returns false
  base::FilePath nonfile(dir.GetPath().Append("nonexistent_file.prop"));
  EXPECT_FALSE(GetFileContents(nonfile, contents));

  // Test empty file returns true
  base::FilePath empty(dir.GetPath().Append("empty_file.prop"));
  base::CreateTemporaryFile(&empty);
  EXPECT_TRUE(GetFileContents(empty, contents));
  EXPECT_TRUE(contents.empty());

  // Test contents of existing file
  contents.clear();
  const std::string in_data{
      "foo\n"
      "bar\n"
      "baz\n"};
  base::FilePath file(dir.GetPath().Append("file.prop"));
  base::CreateTemporaryFile(&file);
  ASSERT_TRUE(base::WriteFile(file, in_data));
  EXPECT_TRUE(GetFileContents(file, contents));
  EXPECT_EQ(contents, in_data);
}

TEST(ServiceArcUtilsTest, GetPropertyHelper) {
  constexpr char kFooName[] = "ro.foo";
  std::string foo_val;

  // Test empty contents returns false
  const std::string empty_string = "";
  EXPECT_FALSE(GetPropertyHelper(empty_string, kFooName, &foo_val));
  EXPECT_TRUE(foo_val.empty());

  // Test commented out property returns false
  const std::string commented_out{"#ro.foo=bar\n"};
  foo_val.clear();
  EXPECT_FALSE(GetPropertyHelper(commented_out, kFooName, &foo_val));
  EXPECT_TRUE(foo_val.empty());

  // Test property with no assignment returns false
  const std::string no_assignment = {"ro.foo\n"};
  foo_val.clear();
  EXPECT_FALSE(GetPropertyHelper(no_assignment, kFooName, &foo_val));
  EXPECT_TRUE(foo_val.empty());

  // Test property not in contents returns false
  const std::string omit_prop = {
      "ro.baz=bar\n"
      "ro.bar=baz\n"};
  foo_val.clear();
  EXPECT_FALSE(GetPropertyHelper(omit_prop, kFooName, &foo_val));
  EXPECT_TRUE(foo_val.empty());

  // Test that correct property gets returned
  const std::string props = {
      "ro.baz=bar\n"
      "ro.foo=abcd\n"
      "ro.bar=baz\n"};
  foo_val.clear();
  EXPECT_TRUE(GetPropertyHelper(props, kFooName, &foo_val));
  EXPECT_EQ(foo_val, "abcd");

  // Test property assigned multiple times returns last value
  const std::string multi_props = {
      "ro.foo=bar\n"
      "ro.foo=abcd\n"
      "ro.foo=baz\n"};
  foo_val.clear();
  EXPECT_TRUE(GetPropertyHelper(multi_props, kFooName, &foo_val));
  EXPECT_EQ(foo_val, "baz");
}

TEST(ServiceArcUtilsTest, GetPropertyFromFile) {
  constexpr char kFooName[] = "ro.foo";
  std::string value;

  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::FilePath file(dir.GetPath().Append("file.prop"));
  base::CreateTemporaryFile(&file);

  // Test property not in file returns false
  EXPECT_FALSE(GetPropertyFromFile(file, kFooName, &value));
  EXPECT_TRUE(value.empty());

  // Test property in file returns true
  const std::string in_data{
      "ro.foo=a\n"
      "ro.bar=b\n"
      "ro.baz=bc\n"};
  ASSERT_TRUE(base::WriteFile(file, in_data));

  EXPECT_TRUE(GetPropertyFromFile(file, kFooName, &value));
  EXPECT_EQ(value, "a");
}

TEST(ServiceArcUtilsTest, IsValidDemoImagePath) {
  EXPECT_TRUE(IsValidDemoImagePath(
      base::FilePath("/run/imageloader/demo-mode-resources/0.12.34.56/"
                     "android_demo_apps.squash")));

  // Invalid version string.
  EXPECT_TRUE(IsValidDemoImagePath(
      base::FilePath("/run/imageloader/demo-mode-resources/0..12.34.56/"
                     "android_demo_apps.squash")));

  // Invalid file name.
  EXPECT_FALSE(IsValidDemoImagePath(base::FilePath(
      "/run/imageloader/demo-mode-resources/0.12.34.56/invalid.squash")));
}

TEST(ServiceArcUtilsTest, IsValidDataImagePath) {
  // Valid concierge disk path.
  EXPECT_TRUE(IsValidDataImagePath(
      base::FilePath("/run/daemon-store/crosvm/deadbeaf/YXJjdm0=.img")));

  // Invalid user hash.
  EXPECT_FALSE(IsValidDataImagePath(
      base::FilePath("/run/daemon-store/crosvm/invalid/YXJjdm0=.img")));

  // Invalid file name.
  EXPECT_FALSE(IsValidDataImagePath(
      base::FilePath("/run/daemon-store/crosvm/deadbeaf/invalid.img")));

  // Valid LVM block device path.
  EXPECT_TRUE(IsValidDataImagePath(
      base::FilePath("/dev/mapper/vm/dmcrypt-deadbeaf-arcvm")));

  // Invalid device name.
  EXPECT_FALSE(
      IsValidDataImagePath(base::FilePath("/dev/mapper/vm/invalid-arcvm")));
}

TEST(ServiceArcUtilsTest, IsValidMetadataImagePath) {
  // Valid metadata image path.
  EXPECT_TRUE(IsValidMetadataImagePath(base::FilePath(
      "/run/daemon-store/crosvm/deadbeaf/YXJjdm0=.metadata.img")));

  // Invalid user hash.
  EXPECT_FALSE(IsValidMetadataImagePath(base::FilePath(
      "/run/daemon-store/crosvm/invalid/YXJjdm0=.metadata.img")));

  // Invalid file name.
  EXPECT_FALSE(IsValidMetadataImagePath(base::FilePath(
      "/run/daemon-store/crosvm/deadbeaf/invalid.metadata.img")));
}

TEST(ServiceArcUtilsTest, ValidateStartArcVmRequest) {
  constexpr char kValidDemoImagePath[] =
      "/run/imageloader/demo-mode-resources/0.12.34.56/"
      "android_demo_apps.squash";
  constexpr char kValidDataImagePath[] =
      "/run/daemon-store/crosvm/deadbeaf/YXJjdm0=.img";
  constexpr char kValidMetadataImagePath[] =
      "/run/daemon-store/crosvm/deadbeaf/YXJjdm0=.metadata.img";
  constexpr char kInvalidImagePath[] = "/opt/google/vms/android/invalid";

  // No disks.
  EXPECT_FALSE(ValidateStartArcVmRequest(CreateRequest({})));

  // Only vendor image.
  EXPECT_TRUE(ValidateStartArcVmRequest(CreateRequest({kVendorImagePath})));

  // Vendor image is invalid.
  EXPECT_FALSE(ValidateStartArcVmRequest(CreateRequest({kInvalidImagePath})));

  // Vendor image is empty (not allowed).
  EXPECT_FALSE(ValidateStartArcVmRequest(CreateRequest({kEmptyDiskPath})));

  // With valid demo image.
  EXPECT_TRUE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kValidDemoImagePath})));

  // With invalid demo image.
  EXPECT_FALSE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kInvalidImagePath})));

  // With empty demo image (allowed).
  EXPECT_TRUE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath})));

  // With valid apex payload image.
  EXPECT_TRUE(ValidateStartArcVmRequest(CreateRequest(
      {kVendorImagePath, kEmptyDiskPath, kApexPayloadImagePath})));

  // With invalid apex payload image.
  EXPECT_FALSE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kInvalidImagePath})));

  // With empty apex payload image (allowed).
  EXPECT_TRUE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath})));

  // With valid data image.
  EXPECT_TRUE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath,
                     kValidDataImagePath})));

  // With invalid data image.
  EXPECT_FALSE(ValidateStartArcVmRequest(CreateRequest(
      {kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath, kInvalidImagePath})));

  // With empty data image (allowed).
  EXPECT_TRUE(ValidateStartArcVmRequest(CreateRequest(
      {kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath, kEmptyDiskPath})));

  // With valid metadata image.
  EXPECT_TRUE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath,
                     kEmptyDiskPath, kValidMetadataImagePath})));

  // With invalid metadata image.
  EXPECT_FALSE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath,
                     kEmptyDiskPath, kInvalidImagePath})));

  // With empty metadata image (allowed).
  EXPECT_TRUE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath,
                     kEmptyDiskPath, kEmptyDiskPath})));

  // With 5 valid image paths.
  EXPECT_TRUE(ValidateStartArcVmRequest(CreateRequest(
      {kVendorImagePath, kValidDemoImagePath, kApexPayloadImagePath,
       kValidDataImagePath, kValidMetadataImagePath})));

  // Too many disks.
  EXPECT_FALSE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath,
                     kEmptyDiskPath, kEmptyDiskPath, kEmptyDiskPath})));
}

}  // namespace concierge
}  // namespace vm_tools
