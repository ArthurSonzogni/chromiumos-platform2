// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The top_io_threads helper prints stats of the top I/O instensive threads

#include <iostream>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/flag_helper.h>

#include "debugd/src/helpers/top_io_threads_utils.h"

namespace {

constexpr char kProcPrefix[] = "/proc";

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_int32(max_entries, 8, "Number of threads to display I/O stats for");
  brillo::FlagHelper::Init(argc, argv,
                           "Display I/O stats for the specified number of top "
                           "I/O intensive threads");
  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(base::FilePath(kProcPrefix), stats,
                            FLAGS_max_entries);
  debugd::PrintThreadIoStats(stats, std::cout);
  return 0;
}
