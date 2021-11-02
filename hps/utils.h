// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_UTILS_H_
#define HPS_UTILS_H_

#include <string>

#include <base/files/file_path.h>

#include <hps/hps_reg.h>

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

}  // namespace hps

#endif  // HPS_UTILS_H_
