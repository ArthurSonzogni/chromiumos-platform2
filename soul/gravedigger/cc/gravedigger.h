// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SOUL_GRAVEDIGGER_CC_GRAVEDIGGER_H_
#define SOUL_GRAVEDIGGER_CC_GRAVEDIGGER_H_

#include <base/files/file_path.h>
#include <base/types/expected.h>
#include <brillo/brillo_export.h>
#include <memory>
#include <vector>

namespace gravedigger {

// Try to initialize the library for the process. Once it has returned `true`
// further calls are not necessary or may return `false`.
[[nodiscard]] bool try_init(std::string_view application_name);

class BRILLO_EXPORT LogFile {
 public:
  // Open the file at `path` for reading.
  // Returns `nullptr` if opening the file failed.
  [[nodiscard]] static std::unique_ptr<LogFile> Open(
      const base::FilePath& path);

  LogFile(const LogFile&) = delete;
  LogFile& operator=(const LogFile&) = delete;
  ~LogFile();

  // Returns `true` if the file at `path` exists and `false` otherwise.
  [[nodiscard]] static bool PathExists(const base::FilePath& path);

  // Read `size` number of bytes from the current position in the file and
  // store the result in `data`.  Returns the number of bytes read, or
  // a negative error code in case of a problem.
  base::expected<int64_t, int64_t> ReadAtCurrentPosition(char* data,
                                                         int64_t size);

  // Read `data.size()` number of bytes from the current position in the file
  // and store the result in `data`.  Returns the number of bytes read, or
  // a negative error code in case of a problem.
  base::expected<int64_t, int64_t> ReadAtCurrentPosition(
      std::vector<unsigned char>& data);

  // Reset the file position to the start of the file.  Returns `true` if the
  // seek succeeded and `false` otherwise.
  bool SeekToBegin();

  // Seek to the character before the end of the file. Returns `true` if the
  // seek succeeded and `false` otherwise.
  // This can be useful when you want to start reading at the last character in
  // the file.
  bool SeekBeforeEnd();

  // Seek to the very end of the file. Returns `true` if the seek succeeded and
  // `false` otherwise.
  bool SeekToEnd();

  // Get the current inode of the open file.
  [[nodiscard]] uint64_t GetInode() const;

  // Get the length of the log file in bytes or -1 in case of an error.
  [[nodiscard]] int64_t GetLength() const;

 private:
  class Impl;
  explicit LogFile(std::unique_ptr<Impl>);

  std::unique_ptr<Impl> impl_;
};

}  // namespace gravedigger

#endif  // SOUL_GRAVEDIGGER_CC_GRAVEDIGGER_H_
