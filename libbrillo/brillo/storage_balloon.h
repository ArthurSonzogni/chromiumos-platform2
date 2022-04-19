// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_STORAGE_BALLOON_H_
#define LIBBRILLO_BRILLO_STORAGE_BALLOON_H_

#include <sys/statvfs.h>
#include <sys/vfs.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/brillo_export.h>

namespace brillo {

// Storage balloon is a construct that artificially restricts writes to the
// filesystem. By fallocate()ing space, storage balloons place an upper bound on
// the available space for other users on the filesystem.
class BRILLO_EXPORT StorageBalloon {
 public:
  explicit StorageBalloon(const base::FilePath& path);
  virtual ~StorageBalloon() = default;

  // Checks if the storage balloon is still in a valid state.
  bool IsValid();
  // Resize to only a maximum of |target_space| bytes of space on the
  // filesystem.
  bool Inflate(int64_t target_space);
  // Resize to zero.
  bool Deflate();
  // Get the current balloon size.
  int64_t GetCurrentBalloonSize();

 protected:
  virtual bool Fallocate(int64_t offset, int64_t len);

  virtual bool Ftruncate(int64_t length);

  virtual bool FstatFs(struct statfs* buf);

  virtual bool Fstat(struct stat* buf);

 private:
  int64_t CalculateBalloonInflationSize(int64_t target_space);

  base::ScopedFD balloon_fd_;
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_STORAGE_BALLOON_H_
