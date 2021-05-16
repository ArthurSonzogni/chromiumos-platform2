// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Poll feature and log changes.
 */

#include <iomanip>
#include <iostream>
#include <memory>

#include <base/command_line.h>
#include <base/strings/string_number_conversions.h>
#include <base/threading/thread.h>
#include <base/time/time.h>

#include "hps/lib/hps.h"
#include "hps/lib/hps_reg.h"
#include "hps/util/command.h"

namespace {

// Argument is feature number.
int Watch(std::unique_ptr<hps::HPS> hps,
          const base::CommandLine::StringVector& args) {
  int feat = 0;
  if (args.size() != 2) {
    std::cerr << "Feature number required" << std::endl;
    return 1;
  }
  if (!base::StringToInt(args[1], &feat) || feat < 0 || feat > 1) {
    std::cerr << args[1] << ": illegal feature number" << std::endl;
    return 1;
  }
  hps->Enable(1 << feat);
  int last = -2;
  for (;;) {
    int result = hps->Result(feat);
    if (result != last) {
      last = result;
      if (result < 0) {
        std::cout << "Invalid result" << std::endl;
      } else {
        std::cout << "Result = " << result << std::endl;
      }
    }
    base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
  }
}

Command watch("watch",
              "watch feature-number - "
              "Poll for feature change.",
              Watch);

}  // namespace
