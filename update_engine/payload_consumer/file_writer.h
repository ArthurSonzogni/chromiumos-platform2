// Copyright 2009 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_FILE_WRITER_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_FILE_WRITER_H_

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <base/logging.h>

#include "update_engine/common/error_code.h"
#include "update_engine/common/utils.h"

// FileWriter is a class that is used to (synchronously, for now) write to
// a file. This file is a thin wrapper around open/write/close system calls,
// but provides and interface that can be customized by subclasses that wish
// to filter the data.

namespace chromeos_update_engine {

class FileWriter {
 public:
  FileWriter() {}
  FileWriter(const FileWriter&) = delete;
  FileWriter& operator=(const FileWriter&) = delete;

  virtual ~FileWriter() {}

  // Wrapper around write. Returns true if all requested bytes
  // were written, or false on any error, regardless of progress.
  virtual bool Write(const void* bytes, size_t count) = 0;

  // Same as the Write method above but returns a detailed |error| code
  // in addition if the returned value is false. By default this method
  // returns kActionExitDownloadWriteError as the error code, but subclasses
  // can override if they wish to return more specific error codes.
  virtual bool Write(const void* bytes, size_t count, ErrorCode* error) {
    *error = ErrorCode::kDownloadWriteError;
    return Write(bytes, count);
  }

  // Wrapper around close. Returns 0 on success or -errno on error.
  virtual int Close() = 0;
};

// Direct file writer is probably the simplest FileWriter implementation.
// It calls the system calls directly.

class DirectFileWriter : public FileWriter {
 public:
  DirectFileWriter() = default;
  DirectFileWriter(const DirectFileWriter&) = delete;
  DirectFileWriter& operator=(const DirectFileWriter&) = delete;

  // FileWriter overrides.
  bool Write(const void* bytes, size_t count) override;
  int Close() override;

  // Wrapper around open. Returns 0 on success or -errno on error.
  int Open(const char* path, int flags, mode_t mode);

  int fd() const { return fd_; }

 private:
  int fd_{-1};
};

class ScopedFileWriterCloser {
 public:
  explicit ScopedFileWriterCloser(FileWriter* writer) : writer_(writer) {}
  ScopedFileWriterCloser(const ScopedFileWriterCloser&) = delete;
  ScopedFileWriterCloser& operator=(const ScopedFileWriterCloser&) = delete;

  ~ScopedFileWriterCloser() {
    int err = writer_->Close();
    if (err) {
      LOG(ERROR) << "FileWriter::Close failed: "
                 << utils::ErrnoNumberAsString(-err);
    }
  }

 private:
  FileWriter* writer_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_FILE_WRITER_H_
