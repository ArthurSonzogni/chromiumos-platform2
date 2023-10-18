// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MACHINE_ID_REGEN_FILE_AUTO_LOCK_H_
#define MACHINE_ID_REGEN_FILE_AUTO_LOCK_H_

#include <brillo/file_utils.h>

namespace machineidregen {
// FileAutoLock represents a generic "flock".
// Lock the lock_path file as per-process lock, do stuff without worrying about
// race condition.
class FileAutoLock {
 public:
  explicit FileAutoLock(const base::FilePath& lock_path)
      : lock_path_(lock_path) {}

  ~FileAutoLock() {}

  FileAutoLock(const FileAutoLock&) = delete;
  FileAutoLock() = delete;

  bool lock();
  bool unlock();
  bool is_valid() const { return fd_.is_valid(); }

 private:
  base::ScopedFD fd_;
  const base::FilePath lock_path_;
};

}  // namespace machineidregen

#endif  // MACHINE_ID_REGEN_FILE_AUTO_LOCK_H_
