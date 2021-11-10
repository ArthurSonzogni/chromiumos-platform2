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
  std::string mcu = "/usr/lib/firmware/hps/mcu_stage1.bin";
  std::string fpga_bitstream = "/usr/lib/firmware/hps/hps_platform.bit";
  std::string fpga_app_image = "/usr/lib/firmware/hps/bios.bin";
  if (args.size() > 1) {
    mcu = args[1];
  }
  if (args.size() > 2) {
    fpga_bitstream = args[2];
  }
  if (args.size() > 3) {
    fpga_app_image = args[3];
  }

  uint32_t version;
  if (args.size() > 4) {
    // there is no StringToUint32
    uint64_t version64;
    if (!base::StringToUint64(args[4], &version64)) {
      std::cerr << "Arg error: version: " << args[4] << std::endl;
      return 1;
    }
    version = base::checked_cast<uint32_t>(version64);
  } else {
    if (!hps::ReadVersionFromFile(base::FilePath(mcu), &version)) {
      return 1;
    }
  }

  hps->Init(version, base::FilePath(mcu), base::FilePath(fpga_bitstream),
            base::FilePath(fpga_app_image));
  if (hps->Boot()) {
    std::cout << "Successful boot" << std::endl;
  } else {
    std::cout << "Boot failed" << std::endl;
  }
  return 0;
}

Command bootcmd("boot",
                "boot [mcu-file [fpga-bitstream [fpga-application [version]]]] "
                "- Boot module.",
                Boot);

}  // namespace
