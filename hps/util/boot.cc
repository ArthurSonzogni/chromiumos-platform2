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
#include <base/numerics/safe_conversions.h>
#include <base/strings/string_number_conversions.h>

#include "hps/hps.h"
#include "hps/hps_reg.h"
#include "hps/util/command.h"
#include "hps/utils.h"

namespace {

int Boot(std::unique_ptr<hps::HPS> hps,
         const base::CommandLine::StringVector& args) {
  if (args.size() != 3 && args.size() != 4) {
    std::cerr << "Arg error: " << args[0] << " mcu-file spi-file [version]"
              << std::endl;
    return 1;
  }

  uint32_t version;
  if (args.size() == 4) {
    // there is no StringToUint32
    uint64_t version64;
    if (!base::StringToUint64(args[3], &version64)) {
      std::cerr << "Arg error: version: " << args[3] << std::endl;
      return 1;
    }
    version = base::checked_cast<uint32_t>(version64);
  } else {
    if (!hps::ReadVersionFromFile(base::FilePath(args[1]), &version)) {
      return 1;
    }
  }

  hps->Init(version, base::FilePath(args[1]), base::FilePath(args[2]));
  if (hps->Boot()) {
    std::cout << "Successful boot" << std::endl;
  } else {
    std::cout << "Boot failed" << std::endl;
  }
  return 0;
}

Command bootcmd("boot",
                "boot mcu-file spi-file [version] - Boot module.",
                Boot);

}  // namespace
