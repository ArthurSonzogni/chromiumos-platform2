// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Enable feature(s).
 */

#include <iomanip>
#include <iostream>
#include <memory>

#include <base/command_line.h>
#include <base/strings/string_number_conversions.h>

#include "hps/lib/hps.h"
#include "hps/util/command.h"

namespace {

// Argument is feature mask
// 0 - disable features
// 1 - enable feature 1
// 2 - enable feature 2
// 3 - enable both feature 1 and 2.
int enable(std::unique_ptr<hps::HPS> hps,
           const base::CommandLine::StringVector& args) {
  int feat = 0;
  if (args.size() != 2) {
    std::cerr << "Feature enable bit-mask required (0, 1, 2, 3)" << std::endl;
    return 1;
  }
  if (!base::StringToInt(args[1], &feat) || feat < 0 || feat > 3) {
    std::cerr << args[1] << ": illegal feature mask. "
              << "Valid values are 0, 1, 2, 3." << std::endl;
    return 1;
  }
  if (hps->Enable(feat)) {
    std::cout << "Success!" << std::endl;
    return 0;
  } else {
    std::cout << "Enable failed!" << std::endl;
    return 1;
  }
}

Command enableCmd("enable",
                  "enable feature-mask - "
                  "Enable/disable features using bit-mask, valid values are "
                  "0, 1, 2, 3",
                  enable);

}  // namespace
