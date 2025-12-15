// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_dlc_hardware_filter_impl.h"

#include <arpa/inet.h>

#include <cstdint>
#include <string>
#include <string_view>

#include <base/bits.h>
#include <base/byte_count.h>
#include <base/containers/fixed_flat_set.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>

#include "login_manager/arc_dlc_hardware_filter_helper.h"

namespace login_manager {

namespace {

constexpr char kKvmFilePath[] = "dev/kvm";
constexpr char kPathSysPci[] = "sys/bus/pci/devices/";
constexpr char kFilePciClass[] = "class";
constexpr char kFilePciDevice[] = "device";
constexpr char kFilePciVendor[] = "vendor";
constexpr char kIomemPath[] = "proc/iomem";
constexpr char kRotationalFile[] = "queue/rotational";
constexpr char kSysBlockPath[] = "sys/block/";
constexpr char kDevPath[] = "dev";
constexpr uint8_t kPciClassGpu = 0x03;
// Supported GPU PCI IDs for enabling ARC on the device whose ARCVM image is
// downloaded from a DLC.
constexpr auto kSupportGpuIds = base::MakeFixedFlatSet<std::string_view>(
    {"8086:9a49", "8086:9a78", "8086:9a60", "8086:9a40", "8086:9a70",
     "8086:9a68", "8086:9a59", "8086:9af8", "8086:9ad9", "8086:9ac9",
     "8086:9ac0", "8086:a780", "8086:a781", "8086:a782", "8086:a783",
     "8086:a788", "8086:a789", "8086:a78a", "8086:a78b", "8086:a7a9",
     "8086:a721", "8086:a7a1", "8086:a720", "8086:a7a8", "8086:a7a0",
     "8086:5917", "8086:5916", "8086:5912", "8086:591e", "8086:5921",
     "8086:5906", "8086:591c", "8086:5926", "8086:593b", "8086:5923",
     "8086:5927", "8086:591b", "8086:591d", "8086:591a", "8086:87c0",
     "8086:5915", "8086:5913", "8086:590b", "8086:5902", "8086:590e",
     "8086:5908", "8086:590a", "8086:4e61", "8086:4e55", "8086:4e71",
     "8086:4e51", "8086:4e57", "8086:3185", "8086:3184", "8086:3ea0",
     "8086:9b41", "8086:3e92", "8086:9bc8", "8086:3e91", "8086:9ba8",
     "8086:9bc5", "8086:3ea5", "8086:3e90", "8086:9bc4", "8086:3ea9",
     "8086:3e9b", "8086:9bca", "8086:3e98", "8086:9b21", "8086:9baa",
     "8086:3ea8", "8086:3ea6", "8086:3ea7", "8086:3ea2", "8086:3ba5",
     "8086:3ea1", "8086:3e9c", "8086:3e99", "8086:3e93", "8086:9bac",
     "8086:9bab", "8086:9ba4", "8086:9ba2", "8086:9ba0", "8086:9ea4",
     "8086:9bcc", "8086:9bcb", "8086:9bc2", "8086:9bc0", "8086:3ea3",
     "8086:87ca", "8086:9bf6", "8086:9be6", "8086:9bc6", "8086:3e94",
     "8086:3e9a", "8086:3e96", "1002:15e7", "8086:4692", "8086:4690",
     "8086:4693", "8086:4682", "8086:4680", "8086:468b", "8086:468a",
     "8086:4688", "8086:46d1", "8086:46d0", "8086:46d2", "8086:46a8",
     "8086:46b3", "8086:4628", "8086:46a6", "8086:46c3", "8086:46a3",
     "8086:46a2", "8086:46a1", "8086:46a0", "8086:462a", "8086:46b2",
     "8086:46b1", "8086:46b0", "8086:46aa", "8086:4626", "1002:15d8",
     "1002:1638", "8086:7dd5", "1002:1636", "1002:164c"});
}  // namespace

ArcDlcHardwareFilterImpl::ArcDlcHardwareFilterImpl(
    const base::FilePath& root_dir, ArcDlcPlatformInfo* platform_info)
    : root_dir_(root_dir), platform_info_(platform_info) {}

bool ArcDlcHardwareFilterImpl::IsCpuSupportArcDlc() const {
  // The presence of `/dev/kvm` indicates KVM is enabled and supported by the
  // CPU. This is a requirement to enable ARC on the device whose ARCVM image is
  // downloaded from a DLC.
  return base::PathExists(root_dir_.Append(kKvmFilePath));
}

bool ArcDlcHardwareFilterImpl::IsGpuSupportArcDlc() const {
  base::FileEnumerator pci_devices(
      base::FilePath(root_dir_.Append(kPathSysPci)), false,
      base::FileEnumerator::DIRECTORIES);

  // Checks if the device's GPU is on the pre-approved list of supported GPU.
  for (base::FilePath dev_path = pci_devices.Next(); !dev_path.empty();
       dev_path = pci_devices.Next()) {
    auto class_raw = ArcDlcHardwareFilterHelper::ReadHexStringToUint32(
        dev_path.Append(kFilePciClass));
    // Continue to the next device if the class ID is not a GPU (class 0x03).
    if (!class_raw.has_value() || ArcDlcHardwareFilterHelper::GetPciClass(
                                      class_raw.value()) != kPciClassGpu) {
      continue;
    }

    auto vendor_id = ArcDlcHardwareFilterHelper::ReadHexStringToUint16(
        dev_path.Append(kFilePciVendor));
    auto device_id = ArcDlcHardwareFilterHelper::ReadHexStringToUint16(
        dev_path.Append(kFilePciDevice));
    if (!vendor_id.has_value() || !device_id.has_value()) {
      continue;
    }

    const std::string pci_id =
        base::StringPrintf("%04x:%04x", vendor_id.value(), device_id.value());
    if (kSupportGpuIds.contains(pci_id)) {
      LOG(INFO) << "Found a supported GPU device at " << dev_path.value()
                << " with PCI ID: " << pci_id;
      return true;
    }
  }
  return false;
}

bool ArcDlcHardwareFilterImpl::IsRamSupportArcDlc() const {
  std::string iomem_content;

  if (!base::ReadFileToString(root_dir_.Append(base::FilePath(kIomemPath)),
                              &iomem_content)) {
    LOG(ERROR) << "Could not read memory information file from /proc/iomem.";
    return false;
  }

  std::optional<uint64_t> total_bytes =
      ArcDlcHardwareFilterHelper::ParseIomemContent(iomem_content);
  if (!total_bytes.has_value()) {
    LOG(ERROR)
        << "Could not parse correct memory information from /proc/iomem.";
    return false;
  }

  const uint64_t aligned_total_bytes = base::bits::AlignUp(
      total_bytes.value(), static_cast<uint64_t>(base::GiB(1).InBytes()));

  return base::ByteCount(aligned_total_bytes) >= base::GiB(4);
}

bool ArcDlcHardwareFilterImpl::IsBootDiskSupportArcDlc() const {
  auto root_dev = platform_info_->GetRootDeviceName();
  if (!root_dev.has_value()) {
    LOG(ERROR) << "Failed to get root device name.";
    return false;
  }

  const base::FilePath boot_disk_sys_path =
      base::FilePath(root_dir_.Append(kSysBlockPath)).Append(root_dev.value());
  auto rotational_val = ArcDlcHardwareFilterHelper::ReadStringToInt(
      boot_disk_sys_path.Append(kRotationalFile));
  if (!rotational_val.has_value() || rotational_val.value() == 1) {
    LOG(INFO) << "Boot disk is a spinning HDD.";
    return false;
  }

  const base::FilePath boot_disk_node_path =
      base::FilePath(root_dir_.Append(kDevPath)).Append(root_dev.value());
  auto size_result = platform_info_->GetDeviceSize(boot_disk_node_path);

  if (!size_result.has_value()) {
    LOG(ERROR) << "Could not get disk size for hardware filter.";
    return false;
  }

  LOG(INFO) << "Boot disk size is: " << size_result.value().InBytes()
            << " bytes.";

  constexpr base::ByteCount kMinDiskSize = base::GiB(32);
  if (size_result.value() < kMinDiskSize) {
    return false;
  }

  return true;
}

bool ArcDlcHardwareFilterImpl::IsArcDlcHardwareRequirementSatisfied() const {
  // Check if KVM is supported.
  if (!IsCpuSupportArcDlc()) {
    LOG(WARNING) << "Hardware filter failed: KVM support not found in CPU.";
    return false;
  }

  // Check if the GPU is on the supported list.
  if (!IsGpuSupportArcDlc()) {
    LOG(WARNING) << "Hardware filter failed: GPU is not on the "
                    "supported list.";
    return false;
  }

  // Check if there is at least 4 GB of RAM.
  if (!IsRamSupportArcDlc()) {
    LOG(WARNING) << "Hardware filter failed: RAM is less than 4GB.";
    return false;
  }

  // Check if the boot disk is at least 32 GB and is not a spinning HDD.
  if (!IsBootDiskSupportArcDlc()) {
    LOG(WARNING) << "Hardware filter failed: Boot disk requirements not met.";
    return false;
  }

  return true;
}

}  // namespace login_manager
