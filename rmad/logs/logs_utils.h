// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_LOGS_LOGS_UTILS_H_
#define RMAD_LOGS_LOGS_UTILS_H_

#include <string>
#include <vector>

#include <base/memory/scoped_refptr.h>

#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/utils/json_store.h"

namespace rmad {

// Adds a state transition type event to `json_store`. Returns true if
// successful.
bool RecordStateTransitionToLogs(scoped_refptr<JsonStore> json_store,
                                 RmadState::StateCase from_state,
                                 RmadState::StateCase to_state);

// Adds the selected repair components to `json_store`. Returns true if
// successful.
bool RecordSelectedComponentsToLogs(
    scoped_refptr<JsonStore> json_store,
    const std::vector<std::string>& replaced_components,
    bool is_mlb_repair);

// Adds the device destination to `json_store`. Returns true if successful.
bool RecordDeviceDestinationToLogs(scoped_refptr<JsonStore> json_store,
                                   const std::string& device_destination);

// Adds the wipe device decision to `json_store`. Returns true if successful.
bool RecordWipeDeviceToLogs(scoped_refptr<JsonStore> json_store,
                            bool wipe_device);

// Adds the wp disable method to `json_store`. Returns true if successful.
bool RecordWpDisableMethodToLogs(scoped_refptr<JsonStore> json_store,
                                 const std::string& wp_disable_method);

// Adds the RSU challenge code to `json_store`. Returns true if successful.
bool RecordRsuChallengeCodeToLogs(scoped_refptr<JsonStore> json_store,
                                  const std::string& challenge_code,
                                  const std::string& hwid);

}  // namespace rmad

#endif  // RMAD_LOGS_LOGS_UTILS_H_
