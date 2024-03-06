// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/storage_balloon.h"

#include <algorithm>

#include <fcntl.h>
#include <rootdev/rootdev.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/xattr.h>

#include <cstddef>
#include <memory>
#include <string>
#include "base/files/scoped_file.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/blkdev_utils/get_backing_block_device.h>

namespace brillo {
namespace {
constexpr char kSysFsPath[] = "/sys/fs/ext4";
constexpr char kReservedClustersPath[] = "reserved_clusters";
constexpr uint64_t kDefaultClusterCount = 4096;

}  // namespace

// static
std::unique_ptr<StorageBalloon> StorageBalloon::GenerateStorageBalloon(
    const base::FilePath& path) {
  base::FilePath fixed_path = path.StripTrailingSeparators();

  // TODO(sarthakkukreti@): Move to rootdev, create a separate helper to get
  // the device. Cannot use GetBackingLogicalDeviceForFile, it requires a
  // dependency on udev.
  struct stat fs_stat;
  if (stat(fixed_path.value().c_str(), &fs_stat)) {
    LOG(WARNING) << "Failed to stat filesystem path" << path;
    return nullptr;
  }

  char fs_device[PATH_MAX];
  dev_t dev = fs_stat.st_dev;

  int ret = rootdev_wrapper(fs_device, sizeof(fs_device),
                            false,  // Do full resolution.
                            false,  // Remove partition number.
                            &dev,   // Device.
                                    // Path within mountpoint.
                            fixed_path.value().c_str(),
                            nullptr,   // Use default search path.
                            nullptr);  // Use default /dev path.

  if (ret != 0) {
    LOG(WARNING) << "Failed to find backing device, error code: " << ret;
    return nullptr;
  }

  return std::unique_ptr<StorageBalloon>(new StorageBalloon(
      path, base::FilePath(kSysFsPath)
                .AppendASCII(base::FilePath(fs_device).BaseName().value())
                .AppendASCII(kReservedClustersPath)));
}

StorageBalloon::StorageBalloon(const base::FilePath& path,
                               const base::FilePath& reserved_clusters_path)
    : filesystem_path_(path),
      sysfs_reserved_clusters_path_(reserved_clusters_path) {}

bool StorageBalloon::IsValid() {
  return base::PathExists(filesystem_path_) &&
         base::PathExists(sysfs_reserved_clusters_path_);
}

StorageBalloon::~StorageBalloon() {
  SetBalloonSize(0);
}

bool StorageBalloon::Adjust(int64_t target_space) {
  if (!IsValid()) {
    LOG(ERROR) << "Invalid balloon";
    return false;
  }

  if (target_space < 0) {
    LOG(ERROR) << "Invalid target space";
    return false;
  }

  int64_t inflation_size = 0;
  if (!CalculateBalloonInflationSize(target_space, &inflation_size)) {
    LOG(ERROR) << "Failed to calculate balloon inflation size.";
    return false;
  }

  if (inflation_size == 0)
    return true;

  int64_t existing_size = GetCurrentBalloonSize();
  if (existing_size < 0) {
    LOG(ERROR) << "Failed to get balloon file size";
    return false;
  }

  return SetBalloonSize(existing_size + inflation_size);
}

bool StorageBalloon::Deflate() {
  if (!IsValid()) {
    LOG(ERROR) << "Invalid balloon";
    return false;
  }

  return SetBalloonSize(0);
}

bool StorageBalloon::SetBalloonSize(int64_t size) {
  if (!IsValid()) {
    LOG(ERROR) << "Invalid balloon";
    return false;
  }

  if (size < 0) {
    size = 0;
  }

  base::ScopedFD fd(open(sysfs_reserved_clusters_path_.value().c_str(),
                         O_WRONLY | O_NOFOLLOW | O_CLOEXEC));

  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to open " << sysfs_reserved_clusters_path_.value();
    return false;
  }

  return base::WriteFileDescriptor(
      fd.get(),
      base::NumberToString(kDefaultClusterCount + size / GetClusterSize()));
}

int64_t StorageBalloon::GetCurrentBalloonSize() {
  if (!IsValid()) {
    LOG(ERROR) << "Invalid balloon";
    return -1;
  }

  std::string balloon_size_str;
  int64_t balloon_size;
  if (!base::ReadFileToString(sysfs_reserved_clusters_path_, &balloon_size_str))
    return -1;

  base::TrimWhitespaceASCII(balloon_size_str, base::TRIM_ALL,
                            &balloon_size_str);
  if (!base::StringToInt64(balloon_size_str, &balloon_size))
    return -1;

  return (balloon_size - kDefaultClusterCount) * GetClusterSize();
}

bool StorageBalloon::StatVfs(struct statvfs* buf) {
  if (!IsValid()) {
    LOG(ERROR) << "Invalid balloon";
    return false;
  }

  return statvfs(filesystem_path_.value().c_str(), buf) == 0;
}

bool StorageBalloon::CalculateBalloonInflationSize(int64_t target_space,
                                                   int64_t* inflation_size) {
  struct statvfs buf;

  if (target_space < 0) {
    LOG(ERROR) << "Invalid target space";
    return false;
  }

  if (!StatVfs(&buf)) {
    LOG(ERROR) << "Failed to statvfs() balloon fd";
    return false;
  }

  int64_t available_space = static_cast<int64_t>(buf.f_bavail) * buf.f_bsize;
  *inflation_size = available_space - target_space;

  return true;
}

int64_t StorageBalloon::GetClusterSize() {
  struct statvfs buf;

  if (!StatVfs(&buf)) {
    LOG(ERROR) << "Failed to statvfs() balloon fd";
    return -1;
  }

  return buf.f_bsize;
}

}  // namespace brillo
