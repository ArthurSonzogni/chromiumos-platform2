// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Read status registers.
 */

#include <iomanip>
#include <iostream>
#include <memory>

#include <base/command_line.h>
#include <base/strings/string_number_conversions.h>

#include "hps/dev.h"
#include "hps/hps.h"
#include "hps/hps_reg.h"
#include "hps/util/command.h"

namespace {

// No arguments, registers 0 - 4 are dumped
// N - dump register N
// N - M Dump registers between N and M inclusive
int Status(std::unique_ptr<hps::HPS> hps,
           const base::CommandLine::StringVector& args) {
  int start, end;
  switch (args.size()) {
    case 1:
      start = hps::HpsReg::kMagic;
      end = hps::HpsReg::kBankReady;
      break;

    case 2:
      start = 0;
      if (!base::StringToInt(args[1], &start) || start < 0 ||
          start > hps::HpsReg::kMax) {
        std::cerr << args[1] << ": illegal register" << std::endl;
        return 1;
      }
      end = start;
      break;

    case 3:
      start = 0;
      end = 0;
      if (!base::StringToInt(args[1], &start) || start < 0 ||
          start > hps::HpsReg::kMax || !base::StringToInt(args[2], &end) ||
          end < 0 || end > hps::HpsReg::kMax) {
        std::cerr << "status: illegal start/end values" << std::endl;
        return 1;
      }
      break;

    default:
      std::cerr << "status: arg error" << std::endl;
      return 1;
  }
  for (auto i = start; i <= end; i++) {
    std::cout << "reg " << std::dec << i << " = ";
    auto result = hps->Device()->ReadReg(i);
    if (result < 0) {
      std::cout << "Error!" << std::endl;
    } else {
      std::cout << std::hex << std::setfill('0') << std::setw(4) << result
                << std::endl;
    }
  }
  return 0;
}

Command status("status",
               "status [ start [ end ] ] - "
               "Dump status registers (default 0 5).",
               Status);

}  // namespace
