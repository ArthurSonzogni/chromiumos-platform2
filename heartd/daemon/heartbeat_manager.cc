// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/heartbeat_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/notreached.h>
#include <base/time/time.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>

#include "heartd/daemon/action_runner.h"
#include "heartd/daemon/heartbeat_tracker.h"
#include "heartd/daemon/utils/mojo_output.h"
#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

namespace {

namespace mojom = ::ash::heartd::mojom;

}  // namespace

HeartbeatManager::HeartbeatManager(ActionRunner* action_runner)
    : action_runner_(action_runner) {}

HeartbeatManager::~HeartbeatManager() = default;

bool HeartbeatManager::IsPacemakerBound(mojom::ServiceName name) {
  return heartbeat_trackers_.contains(name) &&
         heartbeat_trackers_[name]->IsPacemakerBound();
}

void HeartbeatManager::EstablishHeartbeatTracker(
    mojom::ServiceName name,
    mojo::PendingReceiver<mojom::Pacemaker> receiver,
    mojom::HeartbeatServiceArgumentPtr argument) {
  CHECK(!IsPacemakerBound(name))
      << "Heartbeat service repeated registration: " << ToStr(name);

  if (!heartbeat_trackers_.contains(name)) {
    // Brand new registration.
    LOG(INFO) << "Brand new registration: " << ToStr(name);
    auto tracker =
        std::make_unique<HeartbeatTracker>(name, std::move(receiver));
    // Sync on last_update_response.
    DryRun(name, tracker.get());

    heartbeat_trackers_.emplace(name, std::move(tracker));
  } else if (!heartbeat_trackers_[name]->IsPacemakerBound()) {
    // This means that the pacemaker disconnected before. Clients respawn and
    // reconnect to us.
    LOG(INFO) << "Rebind pacemaker for service: " << ToStr(name);
    heartbeat_trackers_[name]->RebindPacemaker(std::move(receiver));
  } else {
    NOTREACHED_NORETURN();
  }

  heartbeat_trackers_[name]->SetupArgument(std::move(argument));
  StartVerifier();
}

void HeartbeatManager::RemoveUnusedHeartbeatTrackers() {
  std::vector<mojom::ServiceName> stop_trackers;
  for (const auto& [name, heartbeat_tracker] : heartbeat_trackers_) {
    if (heartbeat_tracker->IsStopMonitor()) {
      stop_trackers.push_back(name);
    }
  }

  for (const auto& name : stop_trackers) {
    heartbeat_trackers_.erase(name);
  }
}

void HeartbeatManager::VerifyHeartbeatAndTakeAction() {
  RemoveUnusedHeartbeatTrackers();
  if (heartbeat_trackers_.empty()) {
    verifier_timer_.Stop();
    LOG(INFO) << "No heartbeat trackers, stop verifier.";
    return;
  }

  auto current_time = base::Time().Now();
  for (const auto& [name, heartbeat_tracker] : heartbeat_trackers_) {
    DryRun(name, heartbeat_tracker.get());

    // nothing todo if we are in time.
    if (heartbeat_tracker->VerifyTimeGap(current_time)) {
      continue;
    }

    // action required if we are out of heartbeat time.
    auto actions = heartbeat_tracker->GetFailureCountActions();
    for (const auto& action : actions) {
      // Produce proper log message to make sure admins can understand why the
      // reboot happened.
      LOG(ERROR) << ToStr(name) << " app caused a " << ToStr(action)
                 << " because of missing pings for "
                 << heartbeat_tracker->GetVerificationWindow() << " for "
                 << heartbeat_tracker->GetFailureCount() << " times.";
      action_runner_->Run(name, action);
    }
  }
}

void HeartbeatManager::DryRun(ash::heartd::mojom::ServiceName name,
                              HeartbeatTracker* heartbeat_tracker) {
  // DryRun only for the two relevant ActionTypes to expose response if needed.
  for (const auto& action : heartbeat_tracker->GetActions()) {
    auto response = action_runner_->DryRun(name, action);
    heartbeat_tracker->SetLastDryRunResponse(response);
    // Exit loop if response is not success because the next action is not
    // important.
    if (response != mojom::HeartbeatResponse::kSuccess) {
      break;
    }
  }
}

void HeartbeatManager::StartVerifier() {
  if (verifier_timer_.IsRunning()) {
    return;
  }

  LOG(INFO) << "Heartd start periodic verifier.";
  verifier_timer_.Start(
      FROM_HERE, kVerificationPeriod,
      base::BindRepeating(&HeartbeatManager::VerifyHeartbeatAndTakeAction,
                          base::Unretained(this)));
}

}  // namespace heartd
