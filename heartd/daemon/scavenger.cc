// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/scavenger.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/notreached.h>
#include <base/time/time.h>

namespace heartd {

Scavenger::Scavenger(base::OnceCallback<void()> quit_heartd_job,
                     HeartbeatManager* heartbeat_manager)
    : quit_heartd_job_(std::move(quit_heartd_job)),
      heartbeat_manager_(heartbeat_manager) {}

Scavenger::~Scavenger() = default;

void Scavenger::Start() {
  if (timer_.IsRunning()) {
    return;
  }

  Cleanup();
  timer_.Start(
      FROM_HERE, kScavengerPeriod,
      base::BindRepeating(&Scavenger::Cleanup, base::Unretained(this)));
}

void Scavenger::Cleanup() {
  if (heartbeat_manager_->AnyHeartbeatTracker()) {
    return;
  }

  LOG(INFO) << "There is no running jobs, stop heartd";
  std::move(quit_heartd_job_).Run();
}

}  // namespace heartd
