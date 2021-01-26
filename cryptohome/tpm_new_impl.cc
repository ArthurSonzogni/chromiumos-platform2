// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm_new_impl.h"

#include <string>
#include <vector>

#include <tpm_manager-client/tpm_manager/dbus-constants.h>

namespace cryptohome {

TpmNewImpl::TpmNewImpl(tpm_manager::TpmManagerUtility* tpm_manager_utility)
    : tpm_manager_utility_(tpm_manager_utility) {
  SetTpmManagerUtilityForTesting(tpm_manager_utility);
}

}  // namespace cryptohome
