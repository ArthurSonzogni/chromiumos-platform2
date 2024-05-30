// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/sheriffs/heartbeat_verifier.h"

#include <base/logging.h>

namespace heartd {

HeartbeatVerifier::HeartbeatVerifier(HeartbeatManager* heartbeat_manager)
    : heartbeat_manager_(heartbeat_manager) {}

bool HeartbeatVerifier::HasShiftWork() {
  return true;
}

void HeartbeatVerifier::AdjustSchedule() {
  schedule_ = base::Minutes(1);
}

void HeartbeatVerifier::ShiftWork() {
  heartbeat_manager_->VerifyHeartbeatAndTakeAction();
  if (!heartbeat_manager_->AnyHeartbeatTracker()) {
    LOG(INFO) << "There is no heartbeat tracker, stop the verifier";
    timer_.Stop();
  }
}

}  // namespace heartd
