// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPACED_DISK_USAGE_H_
#define SPACED_DISK_USAGE_H_

#include <base/files/file_path.h>

namespace spaced {
// Abstract class that defines the interface for both disk usage util and its
// D-Bus proxy.
class DiskUsageUtil {
 public:
  virtual ~DiskUsageUtil() = default;

  virtual int64_t GetFreeDiskSpace(const base::FilePath& path) = 0;
  virtual int64_t GetTotalDiskSpace(const base::FilePath& path) = 0;
  virtual int64_t GetRootDeviceSize() = 0;

  // Quota-related operations.
  virtual bool IsQuotaSupported(const base::FilePath& path) = 0;
  virtual int64_t GetQuotaCurrentSpaceForUid(const base::FilePath& path,
                                             uint32_t uid) = 0;
  virtual int64_t GetQuotaCurrentSpaceForGid(const base::FilePath& path,
                                             uint32_t gid) = 0;
  virtual int64_t GetQuotaCurrentSpaceForProjectId(const base::FilePath& path,
                                                   uint32_t project_id) = 0;
};

}  // namespace spaced

#endif  // SPACED_DISK_USAGE_H_
