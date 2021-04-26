// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Test reading all registers.
 */

#include <iomanip>
#include <iostream>
#include <memory>

#include <base/command_line.h>
#include <base/strings/string_number_conversions.h>

#include "hps/lib/hps.h"
#include "hps/util/command.h"

namespace {

// No arguments, default to 200.
// N - Number of iterations.
int readtest(std::unique_ptr<hps::HPS> hps,
             const base::CommandLine::StringVector& args) {
  int iterations;
  switch (args.size()) {
    case 1:
      iterations = 200;
      break;

    case 2:
      iterations = 0;
      if (!base::StringToInt(args[1], &iterations) || iterations < 0) {
        std::cerr << args[1] << ": illegal count" << std::endl;
        return 1;
      }
      break;

    default:
      std::cerr << "readtest: arg error" << std::endl;
      return 1;
  }
  for (int i = 0; i < iterations; i++) {
    for (int reg = 0; reg < hps::HpsReg::kNumRegs; reg++) {
      int result = hps->Device()->readReg(reg);
      if (result < 0) {
        std::cout << std::endl
                  << "Error on iteration " << i << " register " << i
                  << std::endl;
      } else if (reg > 32 && result != 0) {
        // Assumes registers 32 onwards will be zeros. Check the value.
        std::cout << std::endl
                  << " Iteration " << i << " Bad register value - reg: " << reg
                  << " value: " << std::ios::hex << std::setfill('0')
                  << std::setw(4) << result << std::endl;
        std::cout.unsetf(std::ios::hex);
      }
    }
    std::cout << "." << std::flush;
  }
  std::cout << std::endl << iterations << " iterations complete." << std::endl;
  return 0;
}

Command cmd("readtest",
            "readtest [ iterations ] - "
            "Test reading all registers (default 200 iterations.",
            readtest);

}  // namespace
