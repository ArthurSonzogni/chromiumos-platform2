// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/helpers/top_io_threads_utils.h"

#include <iomanip>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include <base/containers/adapters.h>
#include <base/files/dir_reader_posix.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>

namespace debugd {

namespace {

constexpr char kThreadIoStatsScanFormat[] =
    "rchar: %" PRIu64 "\nwchar: %" PRIu64 "\nsyscr: %" PRIu64
    "\nsyscw: %" PRIu64
    "\nread_bytes: "
    "%" PRIu64 "\nwrite_bytes: %" PRIu64 "\n";
constexpr int kThreadIoStatsCount = 6;

}  // namespace

bool ProcThreadIterator::LoadThreadIo() {
  std::string io_data;
  if (!base::ReadFileToString(GetCurrentThreadIoPath(), &io_data)) {
    return false;
  }
  uint64_t unused;
  return sscanf(io_data.c_str(), kThreadIoStatsScanFormat, &unused, &unused,
                &unused, &unused, &bytes_read_,
                &bytes_written_) == kThreadIoStatsCount;
}

bool ProcThreadIterator::LoadThreadCommand() {
  if (!base::ReadFileToString(GetCurrentThreadCommPath(), &command_)) {
    return false;
  }
  // Get rid of the trailing newline.
  base::TrimWhitespaceASCII(command_, base::TRIM_TRAILING, &command_);
  return true;
}

bool ProcThreadIterator::LoadThreadInfo() {
  // Ensure a tid that resembles one.
  if (GetCurrentThread() <= 0) {
    return false;
  }
  // Ensure a readable /proc/pid/task/tid/comm file.
  if (!LoadThreadCommand()) {
    return false;
  }
  // Ensure a readable /proc/pid/task/tid/io file.
  if (!LoadThreadIo()) {
    return false;
  }
  return true;
}

bool ProcThreadIterator::MoveToNextThread() {
  while (true) {
    while (thread_iterator_ && thread_iterator_->IsValid() &&
           thread_iterator_->Next()) {
      // Check for access to the current thread's procfs directory, and load
      // relevant info.
      if (LoadThreadInfo()) {
        return true;
      }
      // Move to the next thread, as we had issues making sense of the
      // current one.
      ResetThreadInfo();
    }
    // Iterated through all threads in the current process; move to the next
    // one.
    if (!MoveToNextProcess()) {
      // Possibly no more threads to inspect.
      break;
    }
    // Point our thread iterator to the next thread's procfs directory
    // structure.
    thread_iterator_ = std::make_unique<base::DirReaderPosix>(
        GetCurrentProcessTaskPath().value().c_str());
  }
  return false;
}

bool ProcThreadIterator::MoveToNextProcess() {
  if (!process_iterator_->IsValid()) {
    return false;
  }
  while (process_iterator_->Next()) {
    // Skip an entry that does not resemble a process ID.
    if (GetCurrentProcess() > 0) {
      return true;
    }
  }
  return false;
}

void LoadThreadIoStats(const base::FilePath& proc_root,
                       std::vector<thread_io_stats>& stats,
                       int max_entries) {
  std::priority_queue<thread_io_stats> queue;
  ProcThreadIterator iter(proc_root.value());
  while (iter.NextThread()) {
    queue.push({iter.GetCurrentThread(), iter.GetCurrentProcess(),
                iter.GetCurrentThreadBytesRead(),
                iter.GetCurrentThreadBytesWritten(),
                std::string(iter.GetCurrentThreadCommand())});
    if (queue.size() > max_entries) {
      queue.pop();
    }
  }
  for (; !queue.empty(); queue.pop()) {
    stats.push_back(queue.top());
  }
}

void PrintThreadIoStats(const std::vector<thread_io_stats>& stats,
                        std::ostream& output_stream) {
  output_stream << std::right << std::setw(8) << "TID" << std::setw(8) << "PID"
                << std::setw(16) << "BYTES_READ" << std::setw(16)
                << "BYTES_WRITTEN" << std::setw(8) << "COMMAND" << std::endl;
  for (const auto& entry : base::Reversed(stats)) {
    output_stream << std::right << std::setw(8) << entry.tid << std::setw(8)
                  << entry.pid << std::setw(16) << entry.bytes_read
                  << std::setw(16) << entry.bytes_written << " "
                  << entry.command << std::endl;
  }
}

}  // namespace debugd
