// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/rmad_interface_impl.h"

namespace rmad {

void RmadInterfaceImpl::GetCurrentState(
    const GetCurrentStateRequest& request,
    const GetCurrentStateCallback& callback) {
  GetCurrentStateReply reply;
  // This is fake for now.
  reply.set_state(STATE_RMA_NOT_REQUIRED);
  callback.Run(reply);
}

}  // namespace rmad
