// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chromeos/ec/ec_commands.h>

#include "libec/ec_command.h"
#include "libec/fingerprint/fp_encryption_status_command.h"

namespace ec {

FpEncryptionStatusCommand::FpEncryptionStatusCommand()
    : EcCommand(EC_CMD_FP_ENC_STATUS) {}

uint32_t FpEncryptionStatusCommand::GetValidFlags() const {
  return Resp()->valid_flags;
}

uint32_t FpEncryptionStatusCommand::GetStatus() const {
  return Resp()->status;
}

}  // namespace ec
