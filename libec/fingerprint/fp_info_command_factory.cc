// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_info_command_factory.h"

#include <base/check.h>

namespace ec {

std::unique_ptr<FpInfoCommand> FpInfoCommandFactory::Create(
    EcCommandVersionSupportedInterface* ec_cmd_ver_supported) {
  CHECK(ec_cmd_ver_supported != nullptr);

  uint32_t version = 1;
  if (ec_cmd_ver_supported->EcCmdVersionSupported(EC_CMD_FP_INFO, 2) ==
      EcCmdVersionSupportStatus::SUPPORTED) {
    version = 2;
  }
  return std::make_unique<ec::FpInfoCommand>(version);
}

}  // namespace ec
