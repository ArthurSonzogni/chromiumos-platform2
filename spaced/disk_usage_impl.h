// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPACED_DISK_USAGE_IMPL_H_
#define SPACED_DISK_USAGE_IMPL_H_

#include <sys/quota.h>
#include <sys/statvfs.h>

#include <memory>
#include <optional>
#include <utility>

#include <base/task/task_runner.h>
#include <base/files/file_path.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/brillo_export.h>

#include "spaced/disk_usage.h"

namespace spaced {

class BRILLO_EXPORT DiskUsageUtilImpl : public DiskUsageUtil {
 public:
  DiskUsageUtilImpl(const base::FilePath& rootdev,
                    std::optional<brillo::Thinpool> thinpool);
  ~DiskUsageUtilImpl() override = default;

  int64_t GetFreeDiskSpace(const base::FilePath& path) override;
  int64_t GetTotalDiskSpace(const base::FilePath& path) override;
  int64_t GetRootDeviceSize() override;
  bool IsQuotaSupported(const base::FilePath& path) override;
  int64_t GetQuotaCurrentSpaceForUid(const base::FilePath& path,
                                     uint32_t uid) override;
  int64_t GetQuotaCurrentSpaceForGid(const base::FilePath& path,
                                     uint32_t gid) override;
  int64_t GetQuotaCurrentSpaceForProjectId(const base::FilePath& path,
                                           uint32_t project_id) override;

 protected:
  // Runs statvfs() on a given path.
  virtual int StatVFS(const base::FilePath& path, struct statvfs* st);

  // Runs quotactl() on the given device.
  virtual int QuotaCtl(int cmd,
                       const base::FilePath& device,
                       int id,
                       struct dqblk* dq);

  // Get backing device for a given file path.
  virtual base::FilePath GetDevice(const base::FilePath& path);

  // Gets the block device size in bytes for a given device.
  virtual int64_t GetBlockDeviceSize(const base::FilePath& device);

 private:
  int64_t GetQuotaCurrentSpaceForId(const base::FilePath& path,
                                    uint32_t id,
                                    int quota_type);

  const base::FilePath rootdev_;
  std::optional<brillo::Thinpool> thinpool_;
};

}  // namespace spaced

#endif  // SPACED_DISK_USAGE_IMPL_H_
