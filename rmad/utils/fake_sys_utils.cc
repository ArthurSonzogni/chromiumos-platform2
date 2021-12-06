// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/fake_sys_utils.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "rmad/constants.h"

namespace rmad {
namespace fake {

FakeSysUtils::FakeSysUtils(const base::FilePath working_dir_path)
    : SysUtils(), working_dir_path_(working_dir_path) {}

bool FakeSysUtils::IsPowerSourcePresent() const {
  base::FilePath power_source_present_file_path =
      working_dir_path_.AppendASCII(kPowerSourcePresentFilePath);
  return base::PathExists(power_source_present_file_path);
}

}  // namespace fake
}  // namespace rmad
