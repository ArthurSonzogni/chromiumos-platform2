// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPACED_DISK_USAGE_H_
#define SPACED_DISK_USAGE_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <spaced/proto_bindings/spaced.pb.h>

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
  virtual GetQuotaCurrentSpacesForIdsReply GetQuotaCurrentSpacesForIds(
      const base::FilePath& path,
      const std::vector<uint32_t>& uids,
      const std::vector<uint32_t>& gids,
      const std::vector<uint32_t>& project_ids) = 0;
  virtual GetQuotaCurrentSpacesForIdsReply GetQuotaOverallUsage(
      const base::FilePath& path) = 0;
  virtual std::string GetQuotaOverallUsagePrettyPrint(
      const base::FilePath& path) = 0;
  virtual bool SetProjectId(const base::ScopedFD& path,
                            uint32_t project_id,
                            int* out_error) = 0;
  virtual bool SetProjectInheritanceFlag(const base::ScopedFD& path,
                                         bool enable,
                                         int* out_error) = 0;
  // Disk I/O stats.
  virtual GetDiskIOStatsForPathsReply GetDiskIOStatsForPaths(
      const std::vector<base::FilePath>& paths) = 0;
  virtual std::string GetDiskIOStatsForPathsPrettyPrint(
      const std::string& paths) = 0;
  virtual std::string GetDiskIOStats() = 0;
};

}  // namespace spaced

#endif  // SPACED_DISK_USAGE_H_
