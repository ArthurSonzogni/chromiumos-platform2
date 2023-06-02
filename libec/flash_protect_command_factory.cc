// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/flash_protect_command_factory.h"

namespace ec {

std::unique_ptr<EcCommandInterface> FlashProtectCommandFactory::Create(
    CrosFpDeviceInterface* cros_fp,
    flash_protect::Flags flags,
    flash_protect::Flags mask) {
  if (cros_fp->EcCmdVersionSupported(EC_CMD_FLASH_PROTECT, 2) ==
      EcCmdVersionSupportStatus::SUPPORTED) {
    return std::make_unique<ec::FlashProtectCommand_v2>(flags, mask);
  }

  return std::make_unique<ec::FlashProtectCommand_v1>(flags, mask);
}

}  // namespace ec
