// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mock_firmware_management_parameters.h"

namespace cryptohome {

MockFirmwareManagementParameters::MockFirmwareManagementParameters()
    : FirmwareManagementParameters(ResetMethod::kRecreateSpace,
                                   WriteProtectionMethod::kWriteLock,
                                   /*tpm=*/nullptr,
                                   /*fwmp_checker=*/nullptr) {}

MockFirmwareManagementParameters::~MockFirmwareManagementParameters() {}

}  // namespace cryptohome
