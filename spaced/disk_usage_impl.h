// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPACED_DISK_USAGE_IMPL_H_
#define SPACED_DISK_USAGE_IMPL_H_

#include <sys/quota.h>
#include <sys/statvfs.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/task/task_runner.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/brillo_export.h>
#include <spaced/proto_bindings/spaced.pb.h>

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
  GetQuotaCurrentSpacesForIdsReply GetQuotaCurrentSpacesForIds(
      const base::FilePath& path,
      const std::vector<uint32_t>& uids,
      const std::vector<uint32_t>& gids,
      const std::vector<uint32_t>& project_ids) override;

  GetQuotaCurrentSpacesForIdsReply GetQuotaOverallUsage(
      const base::FilePath& path) override;
  std::string GetQuotaOverallUsagePrettyPrint(
      const base::FilePath& path) override;

  bool SetProjectId(const base::ScopedFD& fd,
                    uint32_t project_id,
                    int* out_error) override;
  bool SetProjectInheritanceFlag(const base::ScopedFD& fd,
                                 bool enable,
                                 int* out_error) override;

 protected:
  // Runs statvfs() on a given path.
  virtual int StatVFS(const base::FilePath& path, struct statvfs* st);

  // Runs quotactl() on the given device.
  virtual int QuotaCtl(int cmd,
                       const base::FilePath& device,
                       int id,
                       struct dqblk* dq);

  // Runs ioctl() for the given request on the given fd.
  virtual int Ioctl(int fd, uint32_t request, void* ptr);

  // Get backing device for a given file path.
  virtual base::FilePath GetDevice(const base::FilePath& path);

  // Gets the block device size in bytes for a given device.
  virtual int64_t GetBlockDeviceSize(const base::FilePath& device);

  virtual std::vector<uid_t> GetUsers();

  virtual std::vector<gid_t> GetGroups();

  virtual std::vector<uint32_t> GetProjectIds();

 private:
  int64_t GetQuotaCurrentSpaceForId(const base::FilePath& path,
                                    uint32_t id,
                                    int quota_type);
  void SetQuotaCurrentSpacesForIdsMap(
      const base::FilePath& device,
      const std::vector<uint32_t>& ids,
      int quota_type,
      google::protobuf::Map<uint32_t, int64_t>* curspaces_for_ids);

  const base::FilePath rootdev_;
  std::optional<brillo::Thinpool> thinpool_;
  std::map<uint32_t, std::string> projects_;
};

}  // namespace spaced

#endif  // SPACED_DISK_USAGE_IMPL_H_
