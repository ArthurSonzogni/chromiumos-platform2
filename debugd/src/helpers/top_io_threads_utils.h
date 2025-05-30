// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_HELPERS_TOP_IO_THREADS_UTILS_H_
#define DEBUGD_SRC_HELPERS_TOP_IO_THREADS_UTILS_H_

#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include <base/files/dir_reader_posix.h>

namespace debugd {

struct thread_io_stats {
  pid_t tid;
  pid_t pid;
  uint64_t bytes_read;
  uint64_t bytes_written;
  std::string command;

  bool operator<(const thread_io_stats& tios) const {
    uint64_t ios1 = bytes_read + bytes_written;
    uint64_t ios2 = tios.bytes_read + tios.bytes_written;
    // We need a  min-heap to collect the set of top I/O intensive threads.
    return ios1 > ios2;
  }
};

class ProcThreadIterator {
 public:
  explicit ProcThreadIterator(const std::string& root) : proc_root_(root) {
    Init();
  }
  bool NextThread() { return MoveToNextThread(); }
  pid_t GetCurrentProcess() {
    return process_iterator_->name() ? std::atoi(process_iterator_->name())
                                     : -1;
  }
  pid_t GetCurrentThread() {
    return thread_iterator_ ? thread_iterator_->name()
                                  ? std::atoi(thread_iterator_->name())
                                  : -1
                            : -1;
  }
  std::string_view GetCurrentThreadCommand() { return command_; }
  uint64_t GetCurrentThreadBytesRead() { return bytes_read_; }
  uint64_t GetCurrentThreadBytesWritten() { return bytes_written_; }

 protected:
  void Init() {
    process_iterator_ =
        std::make_unique<base::DirReaderPosix>(proc_root_.value().c_str());
  }
  bool LoadThreadIo();
  bool LoadThreadCommand();
  bool LoadThreadInfo();
  void ResetThreadInfo() {
    command_.clear();
    bytes_read_ = 0;
    bytes_written_ = 0;
  }
  bool MoveToNextThread();
  bool MoveToNextProcess();
  base::FilePath GetCurrentProcessTaskPath() {
    return proc_root_.Append(std::to_string(GetCurrentProcess()))
        .Append("task");
  }
  base::FilePath GetCurrentThreadPath() {
    return GetCurrentProcessTaskPath().Append(
        std::to_string(GetCurrentThread()));
  }
  base::FilePath GetCurrentThreadIoPath() {
    return GetCurrentThreadPath().Append("io");
  }
  base::FilePath GetCurrentThreadCommPath() {
    return GetCurrentThreadPath().Append("comm");
  }

 private:
  base::FilePath proc_root_;
  std::unique_ptr<base::DirReaderPosix> process_iterator_;
  std::unique_ptr<base::DirReaderPosix> thread_iterator_;
  std::string command_;
  uint64_t bytes_read_;
  uint64_t bytes_written_;
};

void LoadThreadIoStats(const base::FilePath& proc_root,
                       std::vector<thread_io_stats>& stats,
                       int max_entries);
void PrintThreadIoStats(const std::vector<thread_io_stats>& stats,
                        std::ostream& output_stream);

}  // namespace debugd

#endif  // DEBUGD_SRC_HELPERS_TOP_IO_THREADS_UTILS_H_
