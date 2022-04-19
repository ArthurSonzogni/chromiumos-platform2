// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/storage_balloon.h"

#include <fcntl.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>

#include <base/files/file_path.h>
#include <base/logging.h>

namespace brillo {

// Create a tmpfile for the storage balloon. The file will only exist for
// the scope of this object.
StorageBalloon::StorageBalloon(const base::FilePath& path)
    : balloon_fd_(HANDLE_EINTR(
          open(path.value().c_str(), O_TMPFILE | O_RDWR | O_CLOEXEC, 0600))) {}

bool StorageBalloon::IsValid() {
  return balloon_fd_.is_valid();
}

bool StorageBalloon::Inflate(int64_t target_space) {
  if (!IsValid()) {
    LOG(ERROR) << "Invalid balloon";
    return false;
  }

  if (target_space < 0) {
    LOG(ERROR) << "Invalid target space";
    return false;
  }

  int64_t inflation_size = CalculateBalloonInflationSize(target_space);
  if (inflation_size < 0) {
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

  if (!Fallocate(existing_size, inflation_size)) {
    LOG(ERROR) << "Failed to allocate extra space for balloon";
    return false;
  }

  return true;
}

bool StorageBalloon::Deflate() {
  if (!IsValid()) {
    LOG(ERROR) << "Invalid balloon";
    return false;
  }

  return Ftruncate(0);
}

bool StorageBalloon::Fallocate(int64_t offset, int64_t length) {
  if (!IsValid()) {
    LOG(ERROR) << "Invalid balloon";
    return false;
  }

  return fallocate(balloon_fd_.get(), 0, offset, length);
}

bool StorageBalloon::Ftruncate(int64_t length) {
  if (!IsValid()) {
    LOG(ERROR) << "Invalid balloon";
    return false;
  }

  return ftruncate(balloon_fd_.get(), length) == 0;
}

bool StorageBalloon::FstatFs(struct statfs* buf) {
  if (!IsValid()) {
    LOG(ERROR) << "Invalid balloon";
    return false;
  }

  return fstatfs(balloon_fd_.get(), buf) == 0;
}

bool StorageBalloon::Fstat(struct stat* buf) {
  if (!IsValid()) {
    LOG(ERROR) << "Invalid balloon";
    return false;
  }

  return fstat(balloon_fd_.get(), buf) == 0;
}

int64_t StorageBalloon::CalculateBalloonInflationSize(int64_t target_space) {
  struct statfs buf;

  if (target_space < 0) {
    LOG(ERROR) << "Invalid target space";
    return -1;
  }

  if (!FstatFs(&buf)) {
    LOG(ERROR) << "Failed to statvfs() balloon fd";
    return -1;
  }

  int64_t available_space = buf.f_bfree * buf.f_bsize;

  if (target_space > available_space)
    return 0;

  return (available_space - target_space);
}

int64_t StorageBalloon::GetCurrentBalloonSize() {
  struct stat buf;

  if (!Fstat(&buf)) {
    LOG(ERROR) << "Failed to fstat() balloon fd";
    return -1;
  }

  return buf.st_blocks * 512;
}

}  // namespace brillo
