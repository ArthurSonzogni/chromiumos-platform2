// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Read status registers.
 */

#include <iomanip>
#include <iostream>
#include <memory>

#include "hps/lib/hps.h"
#include "hps/util/command.h"

namespace {

// No arguments, registers 0 - 4 are dumped
// N - dump register N
// N - M Dump registers between N and M inclusive
int status(std::unique_ptr<hps::HPS> hps, int argc, char* argv[]) {
  int start, end;
  switch (argc) {
    case 1:
      start = hps::HpsReg::kMagic;
      end = hps::HpsReg::kBankReady;
      break;

    case 2:
      start = atoi(argv[1]);
      if (start < 0 || start > hps::HpsReg::kMax) {
        std::cerr << argv[1] << ": illegal register" << std::endl;
        return 1;
      }
      end = start;
      break;

    case 3:
      start = atoi(argv[1]);
      end = atoi(argv[2]);
      if (start < 0 || start > hps::HpsReg::kMax || end < 0 ||
          end > hps::HpsReg::kMax || start > end) {
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
    auto result = hps->Device()->readReg(i);
    if (result < 0) {
      std::cout << "Error!" << std::endl;
    } else {
      std::cout << std::hex << std::setfill('0') << std::setw(4) << result
                << std::endl;
    }
  }
  return 0;
}

Command cmd("status",
            "status [ start [ end ] ] - "
            "Dump status registers (default 0 5).",
            status);

}  // namespace
