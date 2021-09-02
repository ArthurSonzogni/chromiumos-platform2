// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_UTILS_H_
#define HPS_UTILS_H_

#include <base/files/file_path.h>

namespace hps {

constexpr int kVersionOffset = 18;

// Read the version number from the mcu firmware file.
// Returns false on failure.
bool ReadVersionFromFile(const base::FilePath& mcu, uint32_t* version);

}  // namespace hps

#endif  // HPS_UTILS_H_
