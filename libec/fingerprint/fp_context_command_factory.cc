// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_context_command_factory.h"

namespace ec {

std::unique_ptr<EcCommandInterface> FpContextCommandFactory::Create(
    EcCommandVersionSupportedInterface* ec_cmd_ver_supported,
    const std::string& user_id) {
  if (ec_cmd_ver_supported->EcCmdVersionSupported(EC_CMD_FP_CONTEXT, 1) ==
      EcCmdVersionSupportStatus::SUPPORTED) {
    return FpContextCommand_v1::Create(user_id);
  }

  return FpContextCommand_v0::Create(user_id);
}

}  // namespace ec
