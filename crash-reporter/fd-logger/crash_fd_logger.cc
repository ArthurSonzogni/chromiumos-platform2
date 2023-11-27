// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/fd-logger/crash_fd_logger.h"

#include <algorithm>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/files/file_util.h>
#include <base/files/file_enumerator.h>
#include <base/strings/string_number_conversions.h>
#include "base/strings/string_split.h"

namespace fd_logger {
namespace {

// This file under /proc contains the system-wide fd count.
constexpr char kFileNrPath[] = "sys/fs/file-nr";
constexpr size_t kFdCountThreshold = 64;
constexpr size_t kHistogramBucketsToList = 32;

// Dump the set of file descriptors opened by a process for debugging fd leaks.
// Implemented directly with the POSIX APIs to avoid accidentally opening any
// extra fds, as this program may be run when fd's are exhausted.
void LogOpenFilesInProcess(const base::FilePath& proc_path) {
  // Collect all the open file descriptors in the process.
  std::vector<base::FilePath> entries;
  base::FileEnumerator dir(proc_path.Append("fd"), /*recursive=*/false,
                           base::FileEnumerator::FILES, "*");
  for (base::FilePath name = dir.Next(); !name.empty(); name = dir.Next()) {
    base::FilePath link_target;
    if (!base::ReadSymbolicLink(name, &link_target)) {
      PLOG(ERROR) << "Unable to read symbolic link: " << name;
      continue;
    }
    entries.push_back(link_target);
  }
  if (entries.size() < kFdCountThreshold) {
    return;
  }

  // Read the executable binary name.
  base::FilePath exe;
  if (!base::ReadSymbolicLink(proc_path.Append("exe"), &exe)) {
    PLOG(ERROR) << "Unable to read exe link: " << proc_path;
    return;
  }

  // Count the number of instances per file.
  using Histogram = std::map<base::FilePath, int>;
  Histogram entry_counts;
  for (const auto& it : entries) {
    entry_counts[it]++;
  }

  // Sort the highest counts first.
  std::vector<Histogram::iterator> sorted_entries;
  for (auto it = entry_counts.begin(); it != entry_counts.end(); ++it) {
    sorted_entries.push_back(it);
  }
  std::sort(sorted_entries.begin(), sorted_entries.end(),
            [](const Histogram::iterator& a, const Histogram::iterator& b) {
              return a->second > b->second;
            });
  if (sorted_entries.size() > kHistogramBucketsToList) {
    sorted_entries.resize(kHistogramBucketsToList);
  }

  std::stringstream buckets;
  size_t i = 0;
  for (const auto& it : sorted_entries) {
    buckets << it->second;
    if (++i < sorted_entries.size()) {
      buckets << ",";
    }
  }
  LOG(ERROR) << "Process has many open file descriptors: " << proc_path
             << " exe=" << exe << " fd_count=" << entries.size()
             << " open_counts=" << buckets.str();
}

}  // namespace

void LogOpenFilesInSystem(const base::FilePath& proc_path) {
  base::FileEnumerator dir(proc_path, /*recursive=*/false,
                           base::FileEnumerator::DIRECTORIES, "*");
  for (base::FilePath name = dir.Next(); !name.empty(); name = dir.Next()) {
    // Only descend into directories that represent numeric process ids.
    int pid;
    if (!base::StringToInt(name.BaseName().MaybeAsASCII(), &pid)) {
      continue;
    }
    LogOpenFilesInProcess(name);
  }

  std::string file_nr;
  base::FilePath file_nr_path = proc_path.Append(kFileNrPath);
  if (base::ReadFileToString(file_nr_path, &file_nr)) {
    auto file_nr_contents =
        base::SplitString(file_nr.c_str(), " \t", base::TRIM_WHITESPACE,
                          base::SPLIT_WANT_NONEMPTY);
    CHECK_EQ(file_nr_contents.size(), 3);
    LOG(ERROR) << "System-wide file count from " << file_nr_path
               << ", open: " << file_nr_contents[0]
               << ", max: " << file_nr_contents[2];
  }
}

}  // namespace fd_logger
