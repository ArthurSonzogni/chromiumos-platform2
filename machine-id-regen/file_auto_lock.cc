// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "machine-id-regen/file_auto_lock.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <base/posix/eintr_wrapper.h>

namespace machineidregen {

bool FileAutoLock::lock() {
  fd_ = base::ScopedFD(
      open(lock_path_.value().c_str(), O_CREAT | O_CLOEXEC, 0644));

  if (!fd_.is_valid()) {
    PLOG(ERROR) << "Cannot open lockfile " << lock_path_;
    return false;
  }
  if (HANDLE_EINTR(flock(fd_.get(), LOCK_EX)) != 0) {
    PLOG(ERROR) << "Lock attempt failed for " << lock_path_;
    return false;
  }
  return true;
}

bool FileAutoLock::unlock() {
  if (!is_valid()) {
    PLOG(ERROR) << "Cannot open lockfile " << lock_path_;
    return false;
  }

  if (HANDLE_EINTR(flock(fd_.get(), LOCK_UN)) != 0) {
    PLOG(ERROR) << "Lock release failed for " << lock_path_;
    return false;
  }
  return true;
}

}  // namespace machineidregen
