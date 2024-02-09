// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_CACHED_FILE_DESCRIPTOR_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_CACHED_FILE_DESCRIPTOR_H_

#include <errno.h>
#include <sys/types.h>

#include <memory>
#include <vector>

#include <brillo/secure_blob.h>

#include "update_engine/payload_consumer/file_descriptor.h"

namespace chromeos_update_engine {

class CachedFileDescriptor : public FileDescriptor {
 public:
  CachedFileDescriptor(FileDescriptorPtr fd, size_t cache_size) : fd_(fd) {
    cache_.resize(cache_size);
  }
  CachedFileDescriptor(const CachedFileDescriptor&) = delete;
  CachedFileDescriptor& operator=(const CachedFileDescriptor&) = delete;

  ~CachedFileDescriptor() override = default;

  bool Open(const char* path, int flags, mode_t mode) override {
    return fd_->Open(path, flags, mode);
  }
  bool Open(const char* path, int flags) override {
    return fd_->Open(path, flags);
  }
  ssize_t Read(void* buf, size_t count) override {
    return fd_->Read(buf, count);
  }
  ssize_t Write(const void* buf, size_t count) override;
  off64_t Seek(off64_t offset, int whence) override;
  uint64_t BlockDevSize() override { return fd_->BlockDevSize(); }
  bool BlkIoctl(int request,
                uint64_t start,
                uint64_t length,
                int* result) override {
    return fd_->BlkIoctl(request, start, length, result);
  }
  bool Flush() override;
  bool Close() override;
  bool IsSettingErrno() override { return fd_->IsSettingErrno(); }
  bool IsOpen() override { return fd_->IsOpen(); }

 private:
  // Internal flush without the need to call |fd_->Flush()|.
  bool FlushCache();

  FileDescriptorPtr fd_;
  brillo::Blob cache_;
  size_t bytes_cached_{0};
  off64_t offset_{0};
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_CACHED_FILE_DESCRIPTOR_H_
