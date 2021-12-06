// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_SYS_UTILS_H_
#define RMAD_UTILS_FAKE_SYS_UTILS_H_

#include "rmad/utils/sys_utils.h"

#include <base/files/file_path.h>

namespace rmad {
namespace fake {

class FakeSysUtils : public SysUtils {
 public:
  explicit FakeSysUtils(const base::FilePath working_dir_path);
  ~FakeSysUtils() override = default;

  bool IsPowerSourcePresent() const override;

 private:
  base::FilePath working_dir_path_;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_SYS_UTILS_H_
