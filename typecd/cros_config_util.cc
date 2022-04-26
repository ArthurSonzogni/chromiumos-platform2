// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/cros_config_util.h"

#include <string>

#include <base/logging.h>

namespace typecd {

CrosConfigUtil::CrosConfigUtil() {
  config_ = std::make_unique<brillo::CrosConfig>();
  config_->Init();
}

bool CrosConfigUtil::APModeEntryDPOnly() {
  // TODO(b/230384036): Use an actual pref to fill this out.
  return false;
}

}  // namespace typecd
