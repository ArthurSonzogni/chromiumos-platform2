// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_device_metrics/flex_device_metrics.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

using testing::Return;
using testing::StrictMock;

namespace {

void CreatePartitionDir(const base::FilePath& dir,
                        const std::string& partition_label,
                        int size_in_blocks) {
  CHECK(base::CreateDirectory(dir));
  CHECK(base::WriteFile(dir.Append("uevent"), "PARTNAME=" + partition_label));
  CHECK(base::WriteFile(dir.Append("size"), std::to_string(size_in_blocks)));
}

void ExpectSuccessfulKernAMetric(MetricsLibraryMock& metrics) {
  EXPECT_CALL(metrics, SendSparseToUMA("Platform.FlexPartitionSize.KERN-A", 16))
      .WillOnce(Return(true));
}

}  // namespace

// Test blocks-to-MiB conversion.
TEST(FlexDiskMetrics, ConvertBlocksToMiB) {
  EXPECT_EQ(ConvertBlocksToMiB(0), 0);
  EXPECT_EQ(ConvertBlocksToMiB(2048), 1);
  EXPECT_EQ(ConvertBlocksToMiB(4096), 2);

  // Round down.
  EXPECT_EQ(ConvertBlocksToMiB(4095), 1);
}

TEST(FlexDiskMetrics, GetPartitionLabelFromUevent) {
  base::ScopedTempDir partition_dir;
  CHECK(partition_dir.CreateUniqueTempDir());

  // Error: uevent file does not exist.
  EXPECT_FALSE(
      GetPartitionLabelFromUevent(partition_dir.GetPath()).has_value());

  // Error: uevent file does not contain PARTNAME.
  CHECK(base::WriteFile(partition_dir.GetPath().Append("uevent"), "MAJOR=8\n"));
  EXPECT_FALSE(
      GetPartitionLabelFromUevent(partition_dir.GetPath()).has_value());

  // Successfully get partition name.
  CHECK(base::WriteFile(partition_dir.GetPath().Append("uevent"),
                        "MAJOR=8\nPARTNAME=EFI-SYSTEM"));
  EXPECT_EQ(GetPartitionLabelFromUevent(partition_dir.GetPath()), "EFI-SYSTEM");
}

TEST(FlexDiskMetrics, GetPartitionSizeInMiB) {
  base::ScopedTempDir partition_dir;
  CHECK(partition_dir.CreateUniqueTempDir());

  // Error: size file does not exist.
  EXPECT_FALSE(GetPartitionSizeInMiB(partition_dir.GetPath()).has_value());

  // Error: size file is invalid.
  CHECK(base::WriteFile(partition_dir.GetPath().Append("size"), "abc\n"));
  EXPECT_FALSE(GetPartitionSizeInMiB(partition_dir.GetPath()).has_value());

  // Successfully get partition size.
  CHECK(base::WriteFile(partition_dir.GetPath().Append("size"), "4096\n"));
  EXPECT_EQ(GetPartitionSizeInMiB(partition_dir.GetPath()), 2);
}

TEST(FlexDiskMetrics, GetPartitionSizeMap) {
  base::ScopedTempDir root_dir;
  CHECK(root_dir.CreateUniqueTempDir());
  const auto sys_block_root_path = root_dir.GetPath().Append("sys/block");
  CHECK(base::CreateDirectory(sys_block_root_path));

  // No results: sda directory does not exist.
  EXPECT_TRUE(GetPartitionSizeMap(root_dir.GetPath(), "sda").empty());

  // No results: sda directory is empty.
  const auto sda_path = sys_block_root_path.Append("sda");
  CHECK(base::CreateDirectory(sda_path));
  EXPECT_TRUE(GetPartitionSizeMap(root_dir.GetPath(), "sda").empty());

  // No results: a directory containing valid partition data exists, but
  // it doesn't start with the device name so it's excluded.
  const auto power_dir = sda_path.Append("power");
  CreatePartitionDir(power_dir, "POWER", 4096);
  EXPECT_TRUE(GetPartitionSizeMap(root_dir.GetPath(), "sda").empty());

  // No results: sda1 directory doesn't provide a partition label.
  const auto sda1_dir = sda_path.Append("sda1");
  CreatePartitionDir(sda1_dir, "SDA1", 4096);
  brillo::DeleteFile(sda1_dir.Append("uevent"));
  EXPECT_TRUE(GetPartitionSizeMap(root_dir.GetPath(), "sda").empty());

  // No results: sda2 directory doesn't provide a partition size.
  const auto sda2_dir = sda_path.Append("sda2");
  CreatePartitionDir(sda2_dir, "SDA2", 4096);
  brillo::DeleteFile(sda2_dir.Append("size"));
  EXPECT_TRUE(GetPartitionSizeMap(root_dir.GetPath(), "sda").empty());

  // Create a normal sda3 partition.
  CreatePartitionDir(sda_path.Append("sda3"), "SDA3", 4096);
  // Create a sda4 and sda5 as "reserved" partitions that both have the
  // same label.
  CreatePartitionDir(sda_path.Append("sda4"), "reserved", 2048);
  CreatePartitionDir(sda_path.Append("sda5"), "reserved", 4096);

  // Check that the map contains the sda3/4/5 partitions.
  const auto label_to_size_map = GetPartitionSizeMap(root_dir.GetPath(), "sda");
  EXPECT_EQ(label_to_size_map.size(), 3);
  EXPECT_EQ(label_to_size_map.find("SDA3")->second, 2);
  EXPECT_EQ(label_to_size_map.count("reserved"), 2);
}

// Test successfully sending one metric.
TEST(FlexDiskMetrics, Success) {
  StrictMock<MetricsLibraryMock> metrics;
  ExpectSuccessfulKernAMetric(metrics);

  MapPartitionLabelToMiBSize label_to_size_map;
  label_to_size_map.insert(std::make_pair("KERN-A", 16));

  EXPECT_TRUE(SendDiskMetrics(metrics, label_to_size_map, {"KERN-A"}));
}

// Test failure due to an expected partition not being present. Also
// verify that error doesn't prevent another metric from being sent.
TEST(FlexDiskMetrics, MissingPartitionFailure) {
  StrictMock<MetricsLibraryMock> metrics;
  ExpectSuccessfulKernAMetric(metrics);

  MapPartitionLabelToMiBSize label_to_size_map;
  label_to_size_map.insert(std::make_pair("KERN-A", 16));

  // Since some metrics failed to send, expect failure.
  EXPECT_FALSE(
      SendDiskMetrics(metrics, label_to_size_map, {"missing", "KERN-A"}));
}

// Test failure due to multiple partitions having the same label. Also
// verify that error doesn't prevent another metric from being sent.
TEST(FlexDiskMetrics, MultiplePartitionFailure) {
  StrictMock<MetricsLibraryMock> metrics;
  ExpectSuccessfulKernAMetric(metrics);

  MapPartitionLabelToMiBSize label_to_size_map;
  label_to_size_map.insert(std::make_pair("KERN-A", 16));

  label_to_size_map.insert(std::make_pair("multiple", 32));
  label_to_size_map.insert(std::make_pair("multiple", 64));

  // Since some metrics failed to send, expect failure.
  EXPECT_FALSE(
      SendDiskMetrics(metrics, label_to_size_map, {"multiple", "KERN-A"}));
}

// Test successfully sending the CPU ISA level metric.
TEST(FlexCpuMetrics, SendCpuIsaLevelMetric) {
  StrictMock<MetricsLibraryMock> metrics;
  EXPECT_CALL(metrics, SendEnumToUMA("Platform.FlexCpuIsaLevel", 2, 5))
      .WillOnce(Return(true));

  EXPECT_TRUE(SendCpuIsaLevelMetric(metrics, CpuIsaLevel::kX86_64_V2));
}

// Test getting the boot method in various circumstances.
TEST(FlexBootMethodMetrics, GetBootMethod) {
  base::ScopedTempDir root_dir;
  CHECK(root_dir.CreateUniqueTempDir());

  // Expect kBios if the VPD path and EFI path do not exist,
  EXPECT_EQ(GetBootMethod(root_dir.GetPath()), BootMethod::kBios);

  // Expect kCoreboot if the VPD path exists.
  const auto vpd_sysfs_path = root_dir.GetPath().Append("sys/firmware/vpd/");
  CHECK(base::CreateDirectory(vpd_sysfs_path));
  EXPECT_EQ(GetBootMethod(root_dir.GetPath()), BootMethod::kCoreboot);

  // Expect kCoreboot if both the VPD path and EFI paths exist.
  const auto efi_sysfs_path = root_dir.GetPath().Append("sys/firmware/efi/");
  CHECK(base::CreateDirectory(efi_sysfs_path));

  // Delete the VPD path to move onto EFI.
  brillo::DeleteFile(vpd_sysfs_path);

  // Expect kUnknown if the EFI path exists but the value
  // of `fw_platform_size` is bad or missing.
  EXPECT_EQ(GetBootMethod(root_dir.GetPath()), BootMethod::kUnknown);
  CHECK(base::WriteFile(efi_sysfs_path.Append("fw_platform_size"), "abcd"));
  EXPECT_EQ(GetBootMethod(root_dir.GetPath()), BootMethod::kUnknown);

  // Expect kUefi64 if the value of `fw_platform_size` is "64".
  brillo::DeleteFile(efi_sysfs_path.Append("fw_platform_size"));
  CHECK(base::WriteFile(efi_sysfs_path.Append("fw_platform_size"), "64"));
  EXPECT_EQ(GetBootMethod(root_dir.GetPath()), BootMethod::kUefi64);

  // Expect kUefi32 if the value of `fw_platform_size` is "32".
  brillo::DeleteFile(efi_sysfs_path.Append("fw_platform_size"));
  CHECK(base::WriteFile(efi_sysfs_path.Append("fw_platform_size"), "32"));
  EXPECT_EQ(GetBootMethod(root_dir.GetPath()), BootMethod::kUefi32);
}

// Test successfully sending the boot method metric.
TEST(FlexBootMethodMetrics, SendBootMethodMetric) {
  StrictMock<MetricsLibraryMock> metrics;
  EXPECT_CALL(metrics, SendEnumToUMA("Platform.FlexBootMethod", 3, 5))
      .WillOnce(Return(true));

  EXPECT_TRUE(SendBootMethodMetric(metrics, BootMethod::kUefi64));
}
