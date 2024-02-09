// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/real_config_provider.h"

#include "update_engine/update_manager/generic_variables.h"

namespace chromeos_update_manager {

bool RealConfigProvider::Init() {
  var_is_oobe_enabled_.reset(new ConstCopyVariable<bool>(
      "is_oobe_enabled", hardware_->IsOOBEEnabled()));
  var_is_running_from_minios_.reset(new ConstCopyVariable<bool>(
      "is_running_from_minios", hardware_->IsRunningFromMiniOs()));
  return true;
}

}  // namespace chromeos_update_manager
