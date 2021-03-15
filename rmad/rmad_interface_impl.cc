// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/rmad_interface_impl.h"

#include <base/values.h>

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

const base::FilePath kDefaultJsonStoreFilePath("/var/lib/rmad/state");
constexpr char kRmadCurrentState[] = "current_state";

namespace {

bool RoVerificationKeyPressed() {
  // TODO(b/181000999): Send a D-Bus query to tpm_managerd when API is ready.
  return false;
}

}  // namespace

RmadInterfaceImpl::RmadInterfaceImpl()
    : RmadInterface(), json_store_(kDefaultJsonStoreFilePath) {}

RmadInterfaceImpl::RmadInterfaceImpl(const base::FilePath& json_store_file_path)
    : RmadInterface(), json_store_(json_store_file_path) {}

void RmadInterfaceImpl::GetCurrentState(
    const GetCurrentStateRequest& request,
    const GetCurrentStateCallback& callback) {
  GetCurrentStateReply reply;
  RmadState state;
  if (const base::Value * value;
      json_store_.GetValue(kRmadCurrentState, &value)) {
    if (!value->is_string() || !RmadState_Parse(value->GetString(), &state)) {
      // State string in json_store_ is invalid.
      state = RMAD_STATE_UNKNOWN;
    }
  } else if (RoVerificationKeyPressed()) {
    state = RMAD_STATE_WELCOME_SCREEN;
    // TODO(chenghan): Set error reply if failed to write `json_store_`.
    json_store_.SetValue(kRmadCurrentState, base::Value(RmadState_Name(state)));
  } else {
    state = RMAD_STATE_RMA_NOT_REQUIRED;
  }
  reply.set_state(state);
  callback.Run(reply);
}

}  // namespace rmad
