// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spaced/disk_usage.h"

#include <algorithm>
#include <memory>
#include <string>

#include <sys/statvfs.h>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_util.h>
#include <rootdev/rootdev.h>

namespace spaced {
DiskUsageUtil::DiskUsageUtil()
    : lvm_(std::make_unique<brillo::LogicalVolumeManager>()) {}
DiskUsageUtil::~DiskUsageUtil() {}

int DiskUsageUtil::StatVFS(const base::FilePath& path, struct statvfs* st) {
  return HANDLE_EINTR(statvfs(path.value().c_str(), st));
}

base::Optional<brillo::Thinpool> DiskUsageUtil::GetThinpool() {
  // Get the root device.
  char root_device[PATH_MAX];
  int ret = rootdev(root_device, sizeof(root_device),
                    true,   // Do full resolution.
                    true);  // Remove partition number.
  if (ret != 0) {
    LOG(WARNING) << "rootdev failed with error code " << ret;
    return base::nullopt;
  }

  // For some storage devices (eg. eMMC), the path ends in a digit
  // (eg. /dev/mmcblk0). Use 'p' as the partition separator while generating
  // the partition's block device path. For other types of paths (/dev/sda), we
  // directly append the partition number.
  std::string stateful_dev(root_device);
  if (base::IsAsciiDigit(stateful_dev[stateful_dev.size() - 1]))
    stateful_dev += 'p';
  stateful_dev += '1';

  // Attempt to check if the stateful partition is setup with a valid physical
  // volume.
  base::FilePath physical_volume(stateful_dev);

  base::Optional<brillo::PhysicalVolume> pv =
      lvm_->GetPhysicalVolume(physical_volume);
  if (!pv || !pv->IsValid())
    return base::nullopt;

  base::Optional<brillo::VolumeGroup> vg = lvm_->GetVolumeGroup(*pv);
  if (!vg || !vg->IsValid())
    return base::nullopt;

  return lvm_->GetThinpool(*vg, "thinpool");
}

uint64_t DiskUsageUtil::GetFreeDiskSpace(const base::FilePath& path) {
  // Use statvfs() to get the free space for the given path.
  struct statvfs stat;

  if (StatVFS(path, &stat) != 0) {
    LOG(ERROR) << "Failed to run statvfs() on " << path;
    return 0;
  }

  uint64_t free_disk_space =
      static_cast<uint64_t>(stat.f_bavail) * stat.f_frsize;

  base::Optional<brillo::Thinpool> thinpool = GetThinpool();
  if (thinpool && thinpool->IsValid()) {
    free_disk_space = std::min(free_disk_space, thinpool->GetFreeSpace());
  }

  return free_disk_space;
}

uint64_t DiskUsageUtil::GetTotalDiskSpace(const base::FilePath& path) {
  // Use statvfs() to get the total space for the given path.
  struct statvfs stat;

  if (StatVFS(path, &stat) != 0) {
    LOG(ERROR) << "Failed to run statvfs() on " << path;
    return 0;
  }

  uint64_t total_disk_space =
      static_cast<uint64_t>(stat.f_blocks) * stat.f_frsize;

  base::Optional<brillo::Thinpool> thinpool = GetThinpool();
  if (thinpool && thinpool->IsValid()) {
    total_disk_space = std::min(total_disk_space, thinpool->GetTotalSpace());
  }

  return total_disk_space;
}

}  // namespace spaced
