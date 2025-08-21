// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_dlc_hardware_filter_impl.h"

#include <optional>
#include <string>

#include <base/byte_count.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "login_manager/fake_arc_dlc_platform_info_impl.h"

namespace login_manager {

namespace {

// Path and file names for testing.
constexpr char kDirDev[] = "dev";
constexpr char kFileKvm[] = "kvm";
constexpr char kDirPciDevice[] = "sys/bus/pci/devices/0000:00:02.0";
constexpr char kFilePciClass[] = "class";
constexpr char kFilePciVendor[] = "vendor";
constexpr char kFilePciDevice[] = "device";
constexpr char kDirProc[] = "proc";
constexpr char kFileIomem[] = "iomem";
constexpr char kDirSdaDevice[] = "sda";
constexpr char kDirSdaQueue[] = "sys/block/sda/queue";
constexpr char kFileRotational[] = "rotational";
constexpr char kDummyContent[] = "dummy content";
// GPU IDs and related constants
constexpr char kGpuClassIdSupported[] = "0x030000";
constexpr char kGpuVendorIdSupported[] = "0x8086";
constexpr char kGpuDeviceIdSupported[] = "0x9a49";
constexpr char kGpuClassIdUnsupported[] = "0x020000";
constexpr char kGpuDeviceIdUnsupported[] = "0x1111";
// Memory sizes
constexpr char k2GbAddress[] = "0x7fffffff";
constexpr char k8GbAddress[] = "1ffffffff";
// Disk related constants
constexpr char kDiskRotational[] = "1";
constexpr char kDiskNonRotational[] = "0";
constexpr int kDiskSizeSufficientGiB = 64;
constexpr int kDiskSizeInsufficientGiB = 8;

}  // namespace

class ArcDlcHardwareFilterImplTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(test_dir_.CreateUniqueTempDir());
    test_path_ = test_dir_.GetPath();
  }

  // Sets up the environment to simulate CPU virtualization support by creating
  // the necessary file.
  bool SetCpuSupportVirtualization() {
    if (!base::CreateDirectory(test_path_.Append(kDirDev))) {
      return false;
    }
    return base::WriteFile(test_path_.Append(kDirDev).Append(kFileKvm),
                           kDummyContent);
  }

  // Sets up the environment to simulate GPU information with specific IDs.
  bool SetGpuId(const std::string& class_id,
                const std::string& vendor_id,
                const std::string& device_id) {
    base::FilePath pci_path = test_path_.Append(kDirPciDevice);
    if (!base::CreateDirectory(pci_path)) {
      return false;
    }

    if (!base::WriteFile(pci_path.Append(kFilePciClass), class_id)) {
      return false;
    }

    if (!base::WriteFile(pci_path.Append(kFilePciVendor), vendor_id)) {
      return false;
    }

    return base::WriteFile(pci_path.Append(kFilePciDevice), device_id);
  }

  // Sets up the environment to simulate a specific amount of system memory.
  bool SetMemorySize(const std::string& memory_address) {
    base::FilePath proc_path = test_path_.Append(kDirProc);
    if (!base::CreateDirectory(proc_path)) {
      return false;
    }

    const std::string iomem_content =
        "00000000-" + memory_address + " : System RAM";
    return base::WriteFile(proc_path.Append(kFileIomem), iomem_content);
  }

  // Sets up the environment to simulate the boot disk's rotational status.
  bool SetBootDevice(const std::string& is_rotational, int size_in_gib) {
    fake_platform_info_.set_root_device_name(kDirSdaDevice);
    fake_platform_info_.set_device_size_bytes(base::GiB(size_in_gib));
    base::FilePath queue_path = test_path_.Append(kDirSdaQueue);
    if (!base::CreateDirectory(queue_path)) {
      return false;
    }
    return base::WriteFile(queue_path.Append(kFileRotational), is_rotational);
  }

  base::ScopedTempDir test_dir_;
  base::FilePath test_path_;
  FakeArcDlcPlatformInfoImpl fake_platform_info_;
};

// Tests a scenario where the device meets all hardware requirements.
TEST_F(ArcDlcHardwareFilterImplTest, AllChecksPass) {
  ASSERT_TRUE(SetCpuSupportVirtualization());
  ASSERT_TRUE(SetGpuId(kGpuClassIdSupported, kGpuVendorIdSupported,
                       kGpuDeviceIdSupported));
  ASSERT_TRUE(SetMemorySize(k8GbAddress));
  ASSERT_TRUE(SetBootDevice(kDiskNonRotational, kDiskSizeSufficientGiB));

  ArcDlcHardwareFilterImpl filter(test_path_, &fake_platform_info_);
  EXPECT_TRUE(filter.IsArcDlcHardwareRequirementSatisfied());
}

// Tests a scenario where the KVM file does not exist. The filter should fail
// early at the CPU check.
TEST_F(ArcDlcHardwareFilterImplTest, IsCpuSupportArcDlc_NoKvmFile) {
  ArcDlcHardwareFilterImpl filter(test_path_, &fake_platform_info_);
  EXPECT_FALSE(filter.IsArcDlcHardwareRequirementSatisfied());
}

// Tests a scenario where no GPU is found.
TEST_F(ArcDlcHardwareFilterImplTest, IsGpuSupportArcDlc_NotFoundGpu) {
  ASSERT_TRUE(SetCpuSupportVirtualization());
  ASSERT_TRUE(SetGpuId(kGpuClassIdUnsupported, kGpuVendorIdSupported,
                       kGpuDeviceIdSupported));

  ArcDlcHardwareFilterImpl filter(test_path_, &fake_platform_info_);
  EXPECT_FALSE(filter.IsArcDlcHardwareRequirementSatisfied());
}

// Tests a scenario where the GPU has an unsupported device ID, causing the
// filter to fail.
TEST_F(ArcDlcHardwareFilterImplTest, IsGpuSupportArcDlc_UnsupportedID) {
  ASSERT_TRUE(SetCpuSupportVirtualization());
  ASSERT_TRUE(SetGpuId(kGpuClassIdSupported, kGpuVendorIdSupported,
                       kGpuDeviceIdUnsupported));

  ArcDlcHardwareFilterImpl filter(test_path_, &fake_platform_info_);
  EXPECT_FALSE(filter.IsArcDlcHardwareRequirementSatisfied());
}

// Tests a scenario where the system RAM is insufficient, causing the filter to
// fail.
TEST_F(ArcDlcHardwareFilterImplTest, IsRamSupportArcDlc_InsufficientSize) {
  ASSERT_TRUE(SetCpuSupportVirtualization());
  ASSERT_TRUE(SetGpuId(kGpuClassIdSupported, kGpuVendorIdSupported,
                       kGpuDeviceIdSupported));
  ASSERT_TRUE(SetMemorySize(k2GbAddress));

  ArcDlcHardwareFilterImpl filter(test_path_, &fake_platform_info_);
  EXPECT_FALSE(filter.IsArcDlcHardwareRequirementSatisfied());
}

// Tests a scenario where the boot disk is a rotational HDD, causing the
// filter to fail.
TEST_F(ArcDlcHardwareFilterImplTest, IsBootDiskSupportArcDlc_Hdd) {
  ASSERT_TRUE(SetCpuSupportVirtualization());
  ASSERT_TRUE(SetGpuId(kGpuClassIdSupported, kGpuVendorIdSupported,
                       kGpuDeviceIdSupported));
  ASSERT_TRUE(SetMemorySize(k8GbAddress));
  ASSERT_TRUE(SetBootDevice(kDiskRotational, kDiskSizeSufficientGiB));

  ArcDlcHardwareFilterImpl filter(test_path_, &fake_platform_info_);
  EXPECT_FALSE(filter.IsArcDlcHardwareRequirementSatisfied());
}

// Tests a scenario where the boot disk is a non-rotational SSD but has
// insufficient space.
TEST_F(ArcDlcHardwareFilterImplTest, IsBootDiskSupportArcDlc_InsufficientSize) {
  ASSERT_TRUE(SetCpuSupportVirtualization());
  ASSERT_TRUE(SetGpuId(kGpuClassIdSupported, kGpuVendorIdSupported,
                       kGpuDeviceIdSupported));
  ASSERT_TRUE(SetMemorySize(k8GbAddress));
  ASSERT_TRUE(SetBootDevice(kDiskNonRotational, kDiskSizeInsufficientGiB));

  ArcDlcHardwareFilterImpl filter(test_path_, &fake_platform_info_);
  EXPECT_FALSE(filter.IsArcDlcHardwareRequirementSatisfied());
}

}  // namespace login_manager
