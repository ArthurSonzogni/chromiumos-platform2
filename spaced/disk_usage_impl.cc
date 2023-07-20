// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spaced/disk_usage_impl.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/quota.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_util.h>
#include <brillo/blkdev_utils/get_backing_block_device.h>
#include <rootdev/rootdev.h>

extern "C" {
#include <linux/fs.h>
}

namespace spaced {

DiskUsageUtilImpl::DiskUsageUtilImpl(const base::FilePath& rootdev,
                                     std::optional<brillo::Thinpool> thinpool)
    : rootdev_(rootdev), thinpool_(thinpool) {}

int DiskUsageUtilImpl::StatVFS(const base::FilePath& path, struct statvfs* st) {
  return HANDLE_EINTR(statvfs(path.value().c_str(), st));
}

int DiskUsageUtilImpl::QuotaCtl(int cmd,
                                const base::FilePath& device,
                                int id,
                                struct dqblk* dq) {
  return quotactl(cmd, device.value().c_str(), id, reinterpret_cast<char*>(dq));
}

int DiskUsageUtilImpl::Ioctl(int fd, uint32_t request, void* ptr) {
  return ioctl(fd, request, ptr);
}

base::FilePath DiskUsageUtilImpl::GetDevice(const base::FilePath& path) {
  return brillo::GetBackingLogicalDeviceForFile(path);
}

int64_t DiskUsageUtilImpl::GetFreeDiskSpace(const base::FilePath& path) {
  // Use statvfs() to get the free space for the given path.
  struct statvfs stat;

  if (StatVFS(path, &stat) != 0) {
    PLOG(ERROR) << "Failed to run statvfs() on " << path;
    return -1;
  }

  int64_t free_disk_space = static_cast<int64_t>(stat.f_bavail) * stat.f_frsize;

  return free_disk_space;
}

int64_t DiskUsageUtilImpl::GetTotalDiskSpace(const base::FilePath& path) {
  // Use statvfs() to get the total space for the given path.
  struct statvfs stat;

  if (StatVFS(path, &stat) != 0) {
    PLOG(ERROR) << "Failed to run statvfs() on " << path;
    return -1;
  }

  int64_t total_disk_space =
      static_cast<int64_t>(stat.f_blocks) * stat.f_frsize;

  int64_t thinpool_total_space;
  if (thinpool_ && thinpool_->IsValid() &&
      thinpool_->GetTotalSpace(&thinpool_total_space)) {
    total_disk_space = std::min(total_disk_space, thinpool_total_space);
  }

  return total_disk_space;
}

int64_t DiskUsageUtilImpl::GetBlockDeviceSize(const base::FilePath& device) {
  base::ScopedFD fd(HANDLE_EINTR(
      open(device.value().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "open " << device.value();
    return -1;
  }

  int64_t size;
  if (Ioctl(fd.get(), BLKGETSIZE64, &size)) {
    PLOG(ERROR) << "ioctl(BLKGETSIZE): " << device.value();
    return -1;
  }
  return size;
}

int64_t DiskUsageUtilImpl::GetRootDeviceSize() {
  if (rootdev_.empty()) {
    LOG(WARNING) << "Failed to get root device";
    return -1;
  }

  return GetBlockDeviceSize(rootdev_);
}

bool DiskUsageUtilImpl::IsQuotaSupported(const base::FilePath& path) {
  return GetQuotaCurrentSpaceForUid(path, 0) >= 0;
}

int64_t DiskUsageUtilImpl::GetQuotaCurrentSpaceForUid(
    const base::FilePath& path, uint32_t uid) {
  return GetQuotaCurrentSpaceForId(path, uid, USRQUOTA);
}

int64_t DiskUsageUtilImpl::GetQuotaCurrentSpaceForGid(
    const base::FilePath& path, uint32_t gid) {
  return GetQuotaCurrentSpaceForId(path, gid, GRPQUOTA);
}

int64_t DiskUsageUtilImpl::GetQuotaCurrentSpaceForProjectId(
    const base::FilePath& path, uint32_t project_id) {
  return GetQuotaCurrentSpaceForId(path, project_id, PRJQUOTA);
}

int64_t DiskUsageUtilImpl::GetQuotaCurrentSpaceForId(const base::FilePath& path,
                                                     uint32_t id,
                                                     int quota_type) {
  DCHECK(0 <= quota_type && quota_type < MAXQUOTAS)
      << "Invalid quota_type: " << quota_type;

  const base::FilePath device = GetDevice(path);
  if (device.empty()) {
    LOG(ERROR) << "Failed to find logical device for home directory";
    return -1;
  }

  struct dqblk dq = {};
  if (QuotaCtl(QCMD(Q_GETQUOTA, quota_type), device, id, &dq) != 0) {
    PLOG(ERROR) << "quotactl failed: quota_type=" << quota_type << ", id=" << id
                << ", device=" << device.value();
    return -1;
  }
  return dq.dqb_curspace;
}

bool DiskUsageUtilImpl::SetProjectId(const base::ScopedFD& fd,
                                     uint32_t project_id,
                                     int* out_error) {
  if (!fd.is_valid()) {
    *out_error = EBADF;
    LOG(ERROR) << "SetProjectId: Invalid fd";
    return false;
  }

  struct fsxattr fsx = {};
  if (Ioctl(fd.get(), FS_IOC_FSGETXATTR, &fsx) < 0) {
    *out_error = errno;
    PLOG(ERROR) << "ioctl(FS_IOC_FSGETXATTR) failed";
    return false;
  }
  fsx.fsx_projid = project_id;
  if (Ioctl(fd.get(), FS_IOC_FSSETXATTR, &fsx) < 0) {
    *out_error = errno;
    PLOG(ERROR) << "ioctl(FS_IOC_FSSETXATTR) failed: project_id=" << project_id;
    return false;
  }
  return true;
}

bool DiskUsageUtilImpl::SetProjectInheritanceFlag(const base::ScopedFD& fd,
                                                  bool enable,
                                                  int* out_error) {
  if (!fd.is_valid()) {
    *out_error = EBADF;
    LOG(ERROR) << "SetProjectInheritanceFlag: Invalid fd";
    return false;
  }

  uint32_t flags = 0;
  if (Ioctl(fd.get(), FS_IOC_GETFLAGS, &flags) < 0) {
    *out_error = errno;
    PLOG(ERROR) << "ioctl(FS_IOC_GETFLAGS) failed";
    return false;
  }

  if (enable) {
    flags |= FS_PROJINHERIT_FL;
  } else {
    flags &= ~FS_PROJINHERIT_FL;
  }

  if (Ioctl(fd.get(), FS_IOC_SETFLAGS, reinterpret_cast<void*>(&flags)) < 0) {
    *out_error = errno;
    PLOG(ERROR) << "ioctl(FS_IOC_SETFLAGS) failed: flags=" << std::hex << flags;
    return false;
  }
  return true;
}

}  // namespace spaced
