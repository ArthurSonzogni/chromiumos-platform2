// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/brillo_export.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <string>
#include <sys/ioctl.h>

#include "brillo/blkdev_utils/storage_device.h"

namespace brillo {

bool StorageDevice::WipeBlkDev(const base::FilePath& device_path,
                               const uint64_t device_offset,
                               const uint64_t device_length,
                               bool run_physical_erasure) const {
  if (!LogicalErasure(device_path, device_offset, device_length,
                      GetLogicalErasureIoctlType())) {
    return false;
  }

  if (run_physical_erasure) {
    return PhysicalErasure(device_path, device_length);
  }
  return true;
}

LogicalErasureIoctl StorageDevice::GetLogicalErasureIoctlType() const {
  return LogicalErasureIoctl::blkzeroout;
}

bool StorageDevice::LogicalErasure(const base::FilePath& device_path,
                                   const uint64_t device_offset,
                                   const uint64_t device_length,
                                   LogicalErasureIoctl ioctl_type) const {
  base::File device(
      open(device_path.value().c_str(), O_CLOEXEC | O_WRONLY | O_SYNC));
  if (!device.IsValid()) {
    PLOG(ERROR) << "Failed to open " << device_path.value();
    return false;
  }

  std::string ioctl_str = LogicalErasureIoctlToString(ioctl_type);
  LOG(INFO) << "Wiping " << device_path.value() << " from " << device_offset
            << " to " << (device_offset + device_length) << " with ioctl "
            << ioctl_str;
  uint64_t range[2] = {device_offset, device_length};
  int ioctl_ret = -1;
  switch (ioctl_type) {
    case LogicalErasureIoctl::blkdiscard:
      ioctl_ret = ioctl(device.GetPlatformFile(), BLKDISCARD, &range);
      break;
    case LogicalErasureIoctl::blkzeroout:
      ioctl_ret = ioctl(device.GetPlatformFile(), BLKZEROOUT, &range);
      break;
    case LogicalErasureIoctl::blksecdiscard:
      ioctl_ret = ioctl(device.GetPlatformFile(), BLKSECDISCARD, &range);
      break;
  }
  if (ioctl_ret) {
    if (errno == ENOTTY || errno == EOPNOTSUPP || errno == ENOTSUP) {
      LOG(INFO) << ioctl_str << " is not supported on " << device_path.value();
    } else {
      PLOG(ERROR) << "Wiping with " << ioctl_str << " failed on "
                  << device_path.value();
    }
    return false;
  }
  return true;
}

bool StorageDevice::SupportPhysicalErasure() const {
  return false;
}

bool StorageDevice::PhysicalErasure(const base::FilePath& device_path,
                                    const uint64_t device_length) const {
  LOG(INFO) << "Device does not support physical erasure.";
  return false;
}

std::string LogicalErasureIoctlToString(LogicalErasureIoctl ioctl_type) {
  switch (ioctl_type) {
    case LogicalErasureIoctl::blkdiscard:
      return "BLKDISCARD";
    case LogicalErasureIoctl::blkzeroout:
      return "BLKZEROOUT";
    case LogicalErasureIoctl::blksecdiscard:
      return "BLKSECDISCARD";
  }
}

}  // namespace brillo
