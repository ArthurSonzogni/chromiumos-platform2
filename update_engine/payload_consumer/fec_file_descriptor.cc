// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_consumer/fec_file_descriptor.h"

#include <base/logging.h>

namespace chromeos_update_engine {

bool FecFileDescriptor::Open(const char* path, int flags) {
  return Open(path, flags, 0600);
}

bool FecFileDescriptor::Open(const char* path, int flags, mode_t mode) {
  if (!fh_.open(path, flags, mode))
    return false;

  if (!fh_.has_ecc()) {
    LOG(ERROR) << "No ECC data in the passed file";
    fh_.close();
    return false;
  }

  fec_status status;
  if (!fh_.get_status(status)) {
    LOG(ERROR) << "Couldn't load ECC status";
    fh_.close();
    return false;
  }

  dev_size_ = status.data_size;
  return true;
}

ssize_t FecFileDescriptor::Read(void* buf, size_t count) {
  return fh_.read(buf, count);
}

ssize_t FecFileDescriptor::Write(const void* buf, size_t count) {
  errno = EROFS;
  return -1;
}

off64_t FecFileDescriptor::Seek(off64_t offset, int whence) {
  if (fh_.seek(offset, whence)) {
    return offset;
  }
  return -1;
}

uint64_t FecFileDescriptor::BlockDevSize() {
  return dev_size_;
}

bool FecFileDescriptor::BlkIoctl(int request,
                                 uint64_t start,
                                 uint64_t length,
                                 int* result) {
  // No IOCTL pass-through in this mode.
  return false;
}

bool FecFileDescriptor::Close() {
  return fh_.close();
}

}  // namespace chromeos_update_engine
