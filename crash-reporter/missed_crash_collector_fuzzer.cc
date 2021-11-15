// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdio.h>

#include "crash-reporter/missed_crash_collector.h"

class Environment {
 public:
  Environment() {
    // Set-up code.
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  // Note: const_cast is safe as we open the file in read-only mode, so the
  // input data is not modified.
  FILE* file = fmemopen(const_cast<uint8_t*>(data), size, "r");
  if (!file) {
    std::cerr << "Failed to open memory file";
    return 1;
  }

  MissedCrashCollector collector;
  collector.set_input_file_for_testing(file);
  collector.Collect(/*pid=*/111,
                    /*recent_miss_count=*/222,
                    /*recent_match_count=*/333,
                    /*pending_miss_count=*/444);

  fclose(file);

  return 0;
}
