// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/rollback_info_command.h"

namespace ec {

int32_t RollbackInfoCommand_v1::ID() const {
  return Resp()->id;
}

int32_t RollbackInfoCommand_v1::MinVersion() const {
  return Resp()->rollback_min_version;
}

int32_t RollbackInfoCommand_v1::RWVersion() const {
  return Resp()->rw_rollback_version;
}

bool RollbackInfoCommand_v1::IsSecretInited() const {
  return Resp()->is_secret_inited != 0;
}

}  // namespace ec
