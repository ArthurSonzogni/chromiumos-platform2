// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_LOGS_LOGS_UTILS_H_
#define RMAD_LOGS_LOGS_UTILS_H_

#include <base/memory/scoped_refptr.h>

#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/utils/json_store.h"

namespace rmad {

// Adds a state transition type event to `json_store`. Returns true if
// successful.
bool RecordStateTransitionToLogs(scoped_refptr<JsonStore> json_store,
                                 RmadState::StateCase from_state,
                                 RmadState::StateCase to_state);

}  // namespace rmad

#endif  // RMAD_LOGS_LOGS_UTILS_H_
