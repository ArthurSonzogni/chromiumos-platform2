// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPACED_DISK_USAGE_IMPL_H_
#define SPACED_DISK_USAGE_IMPL_H_

#include <sys/statvfs.h>

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/brillo_export.h>

#include "spaced/disk_usage.h"

namespace spaced {

class BRILLO_EXPORT DiskUsageUtilImpl : public DiskUsageUtil {
 public:
  DiskUsageUtilImpl();
  ~DiskUsageUtilImpl() override = default;

  int64_t GetFreeDiskSpace(const base::FilePath& path) override;
  int64_t GetTotalDiskSpace(const base::FilePath& path) override;
  int64_t GetRootDeviceSize() override;

  void set_thinpool_for_test(const brillo::Thinpool& thinpool) {
    thinpool_ = std::make_unique<brillo::Thinpool>(thinpool);
  }

 protected:
  // Runs statvfs() on a given path.
  virtual int StatVFS(const base::FilePath& path, struct statvfs* st);

  // Retrieves the stateful partition's thinpool.
  virtual base::Optional<brillo::Thinpool> GetThinpool();

  // Retrieves the root device.
  virtual base::Optional<base::FilePath> GetRootDevice();

  // Gets the block device size in bytes for a given device.
  virtual int64_t GetBlockDeviceSize(const base::FilePath& device);

 private:
  std::unique_ptr<brillo::Thinpool> thinpool_;
  std::unique_ptr<brillo::LogicalVolumeManager> lvm_;
};

}  // namespace spaced

#endif  // SPACED_DISK_USAGE_IMPL_H_
