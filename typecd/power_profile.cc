// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/power_profile.h"

#include <base/files/file_path.h>
#include <base/logging.h>

namespace typecd {

PowerProfile::PowerProfile(const base::FilePath& syspath) : syspath_(syspath) {
  LOG(INFO) << "Registered a power profile with path: " << syspath_;
}

}  // namespace typecd
