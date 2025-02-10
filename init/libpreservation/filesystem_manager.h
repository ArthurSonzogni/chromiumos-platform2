// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_LIBPRESERVATION_FILESYSTEM_MANAGER_H_
#define INIT_LIBPRESERVATION_FILESYSTEM_MANAGER_H_

#include <memory>

#include <base/files/file_path.h>
#include <ext2fs/ext2fs.h>
#include <init/libpreservation/ext2fs.h>
#include <init/libpreservation/preseeded_files.pb.h>

namespace libpreservation {

// FilesystemManager manages operations on an _unmounted_ ext2/3/4 filesystem.
class FilesystemManager {
 public:
  explicit FilesystemManager(std::unique_ptr<Ext2fs> fs);
  virtual ~FilesystemManager() = default;

  // Create directory at given path. If any of the parent components do
  // not exist, return false.
  virtual bool CreateDirectory(const base::FilePath& path);

  // Create a new file and fallocate() the given fixed-goal extents to
  // the file.
  virtual bool CreateFileAndFixedGoalFallocate(const base::FilePath& path,
                                               uint64_t size,
                                               const ExtentArray& extents);

  // Unlink file.
  virtual bool UnlinkFile(const base::FilePath& path);

  // Check if the filepath already exists on the filesystem.
  virtual bool FileExists(const base::FilePath& path);

 private:
  std::unique_ptr<Ext2fs> fs_;
};

}  // namespace libpreservation

#endif  // INIT_LIBPRESERVATION_FILESYSTEM_MANAGER_H_
