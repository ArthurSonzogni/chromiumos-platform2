// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/rollback_info_command_factory.h"

#include <base/check.h>

namespace ec {

std::unique_ptr<RollbackInfoCommand> RollbackInfoCommandFactory::Create(
    EcCommandVersionSupportedInterface* ec_cmd_ver_supported) {
  CHECK(ec_cmd_ver_supported != nullptr);

  uint32_t version = 0;
  if (ec_cmd_ver_supported->EcCmdVersionSupported(EC_CMD_ROLLBACK_INFO, 1) ==
      EcCmdVersionSupportStatus::SUPPORTED) {
    version = 1;
  }
  return std::make_unique<ec::RollbackInfoCommand>(version);
}

}  // namespace ec
