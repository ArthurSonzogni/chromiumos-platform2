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
    if (heartbeat_tracker->VerifyTimeGap(current_time)) {
      continue;
    }

    auto actions = heartbeat_tracker->GetFailureCountAction();
    for (const auto& action : actions) {
      action_runner_->Run(name, action);
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
