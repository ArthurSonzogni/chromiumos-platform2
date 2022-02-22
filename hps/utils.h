// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_UTILS_H_
#define HPS_UTILS_H_

#include <string>

#include <base/files/file_path.h>
#include <base/strings/stringprintf.h>

#include "hps/dev.h"
#include "hps/hps_reg.h"

namespace hps {

constexpr int kVersionOffset = 20;

// Read the version number from the mcu firmware file.
// Returns false on failure.
bool ReadVersionFromFile(const base::FilePath& mcu, uint32_t* version);

// Convert the register to the name of the register
const char* HpsRegToString(const HpsReg reg);

// Return a pretty printed register value,
// or empty string if there is nothing pretty to print
std::string HpsRegValToString(HpsReg reg, uint16_t val);

// Iterate through all HPS registers from `start` to `end` (inclusive), reading
// their values and writing their contents to `callback`. `callback` should be a
// functor that accepts std::string.
// TODO(skyostil): Add support for >16 bit registers.
template <typename Callback>
int DumpHpsRegisters(DevInterface& dev,
                     Callback callback,
                     int start = 0,
                     int end = static_cast<int>(HpsReg::kLargestRegister)) {
  int failures = 0;
  for (int i = start; i <= end; i++) {
    auto reg = static_cast<HpsReg>(i);
    std::optional<uint16_t> result = dev.ReadReg(reg);
    if (!result) {
      callback(base::StringPrintf("Register %3d: error (%s)", i,
                                  HpsRegToString(reg)));
      failures++;
    } else {
      callback(base::StringPrintf(
          "Register %3d: 0x%04x (%s) %s", i, result.value(),
          HpsRegToString(reg), HpsRegValToString(reg, result.value()).c_str()));
    }
  }
  return failures;
}

}  // namespace hps

#endif  // HPS_UTILS_H_
