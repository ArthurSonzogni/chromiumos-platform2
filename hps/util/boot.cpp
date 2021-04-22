// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Boot the module.
 */

#include <iostream>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/strings/string_number_conversions.h>

#include "hps/lib/hps.h"
#include "hps/util/command.h"

namespace {

int boot(std::unique_ptr<hps::HPS> hps,
         const base::CommandLine::StringVector& args) {
  if (args.size() != 4) {
    std::cerr << "Arg error: ... " << args[0] << " version appl spi"
              << std::endl;
    return 1;
  }
  int version = 0;
  base::StringToInt(args[1], &version);

  hps->Init(version, base::FilePath(args[2]), base::FilePath(args[3]));
  if (hps->Boot()) {
    std::cout << "Successful boot" << std::endl;
  } else {
    std::cout << "Boot failed" << std::endl;
  }
  return 0;
}

Command bootcmd("boot", "boot version mcu-file spi-file - Boot module.", boot);

}  // namespace
