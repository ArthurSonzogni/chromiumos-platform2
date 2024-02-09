// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_FEC_FILE_DESCRIPTOR_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_FEC_FILE_DESCRIPTOR_H_

#include <fec/io.h>

#include "update_engine/payload_consumer/file_descriptor.h"

// A FileDescriptor implementation with error correction based on the "libfec"
// library. The libfec on the running system allows to parse the error
// correction blocks stored in partitions that have verity and error correction
// enabled. This information is present in the raw block device, but of course
// not available via the dm-verity block device.

namespace chromeos_update_engine {

// An error corrected file based on FEC.
class FecFileDescriptor : public FileDescriptor {
 public:
  FecFileDescriptor() = default;
  ~FecFileDescriptor() = default;

  // Interface methods.
  bool Open(const char* path, int flags, mode_t mode) override;
  bool Open(const char* path, int flags) override;
  ssize_t Read(void* buf, size_t count) override;
  ssize_t Write(const void* buf, size_t count) override;
  off64_t Seek(off64_t offset, int whence) override;
  uint64_t BlockDevSize() override;
  bool BlkIoctl(int request,
                uint64_t start,
                uint64_t length,
                int* result) override;
  bool Flush() override { return true; }
  bool Close() override;
  bool IsSettingErrno() override { return true; }
  bool IsOpen() override {
    // The bool operator on the fec::io class tells whether the internal
    // handle is open.
    return static_cast<bool>(fh_);
  }

 protected:
  fec::io fh_;
  uint64_t dev_size_{0};
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_FEC_FILE_DESCRIPTOR_H_
