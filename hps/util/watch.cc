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

#include "hps/hps.h"
#include "hps/hps_reg.h"
#include "hps/util/command.h"

namespace {

// Argument is feature number.
int Watch(std::unique_ptr<hps::HPS> hps,
          const base::CommandLine::StringVector& args) {
  int feat;
  if (args.size() != 2) {
    std::cerr << "Feature number required" << std::endl;
    return 1;
  }
  if (!base::StringToInt(args[1], &feat)) {
    std::cerr << args[1] << ": illegal feature number" << std::endl;
    return 1;
  }
  if (feat < 0 || feat > 1) {
    std::cerr << args[1] << ": feature number out of range (0,1)" << std::endl;
    return 1;
  }
  hps->Enable(feat);
  for (;;) {
    hps::FeatureResult feature_result;
    uint8_t last_inference_result = 0;
    feature_result = hps->Result(feat);
    if (feature_result.valid) {
      if (last_inference_result != feature_result.inference_result) {
        last_inference_result = feature_result.inference_result;
        std::cout << "Result = " << last_inference_result << std::endl;
      }
    } else {
      std::cout << "Invalid result" << std::endl;
    }
    base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
  }
}

Command watch("watch",
              "watch feature-number - "
              "Poll for feature change.",
              Watch);

}  // namespace
