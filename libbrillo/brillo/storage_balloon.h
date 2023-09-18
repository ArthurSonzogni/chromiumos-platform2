// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_STORAGE_BALLOON_H_
#define LIBBRILLO_BRILLO_STORAGE_BALLOON_H_

#include <sys/statvfs.h>
#include <sys/vfs.h>

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/brillo_export.h>

namespace brillo {

// Storage balloon is a construct that artificially restricts writes to the
// filesystem. By using ext4 reserved clusters, we can reserve space for
// filesystem metadata that will not be used for any file allocations.
class BRILLO_EXPORT StorageBalloon {
 public:
  virtual ~StorageBalloon();

  static std::unique_ptr<StorageBalloon> GenerateStorageBalloon(
      const base::FilePath& path);

  // Checks if the storage balloon is still in a valid state.
  bool IsValid();
  // Resizes the balloon so that a maximum of |target_space| bytes is available
  // on the filesystem.
  bool Adjust(int64_t target_space);
  // Resizes the balloon to zero.
  bool Deflate();

  // Get the current balloon size.
  virtual int64_t GetCurrentBalloonSize();

 protected:
  // Only used by factory method.
  StorageBalloon(const base::FilePath& path,
                 const base::FilePath& reserved_clusters_path);

  // Set the balloon size.
  virtual bool SetBalloonSize(int64_t size);

  virtual int64_t GetClusterSize();

  virtual bool StatVfs(struct statvfs* buf);

 private:
  bool CalculateBalloonInflationSize(int64_t target_space,
                                     int64_t* inflation_size);

  base::FilePath filesystem_path_;
  base::FilePath sysfs_reserved_clusters_path_;
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_STORAGE_BALLOON_H_
