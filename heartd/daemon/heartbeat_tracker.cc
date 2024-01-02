// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/heartbeat_tracker.h"

#include <algorithm>
#include <utility>

#include <base/time/time.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>

#include "heartd/daemon/utils/mojo_output.h"
#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

namespace {

namespace mojom = ::ash::heartd::mojom;

}  // namespace

HeartbeatTracker::HeartbeatTracker(
    mojom::ServiceName name, mojo::PendingReceiver<mojom::Pacemaker> receiver)
    : name_(name), receiver_(this, std::move(receiver)) {
  receiver_.set_disconnect_handler(base::BindOnce(
      &HeartbeatTracker::OnPacemakerDisconnect, base::Unretained(this)));
  last_touch_time_ = base::Time().Now();
}

HeartbeatTracker::~HeartbeatTracker() = default;

void HeartbeatTracker::SendHeartbeat(SendHeartbeatCallback callback) {
  last_touch_time_ = base::Time().Now();
  std::move(callback).Run();
}

void HeartbeatTracker::StopMonitor(StopMonitorCallback callback) {
  LOG(INFO) << "Stop monitoring heartbeat for service: " << ToStr(name_);
  stop_monitor_ = true;
  std::move(callback).Run();
}

bool HeartbeatTracker::IsPacemakerBound() {
  return receiver_.is_bound();
}

bool HeartbeatTracker::IsStopMonitor() {
  return stop_monitor_;
}

void HeartbeatTracker::RebindPacemaker(
    mojo::PendingReceiver<mojom::Pacemaker> receiver) {
  CHECK(!IsPacemakerBound())
      << "Failed to rebind pacemaker for service: " << ToStr(name_);
  stop_monitor_ = false;
  receiver_.Bind(std::move(receiver));
  receiver_.set_disconnect_handler(base::BindOnce(
      &HeartbeatTracker::OnPacemakerDisconnect, base::Unretained(this)));
}

void HeartbeatTracker::OnPacemakerDisconnect() {
  // We don't need to increase the |failure_count_| here because once the
  // pacemaker is disconnected, |last_touch_time_| won't change anymore so
  // |failure_count_| will be increased in |VerifyTimeGap| periodically.
  receiver_.reset();
}

void HeartbeatTracker::SetupArgument(
    mojom::HeartbeatServiceArgumentPtr argument) {
  base::TimeDelta threshold =
      base::Seconds(argument->verification_window_seconds);
  verification_window_ = std::max(verification_window_, threshold);
  actions_ = std::move(argument->actions);
}

uint8_t HeartbeatTracker::GetFailureCount() {
  return failure_count_;
}

bool HeartbeatTracker::VerifyTimeGap(const base::Time& current_time) {
  auto gap = current_time - last_touch_time_;
  // The `verification_window_` is always larger than the heartbeat frequency,
  // so it's likely that we think client is alive while the mojo connection has
  // dropped. It's not a big problem because the `failure_count_` will always
  // increase in later verification. However, checking the mojo connection helps
  // to catch the issue earlier, it's a nice to have.
  if (gap > verification_window_ || !IsPacemakerBound()) {
    ++failure_count_;
    LOG(INFO) << "Service [" << ToStr(name_) << "] failure count increase: "
              << static_cast<int>(failure_count_);
    return false;
  }

  failure_count_ = 0;
  return true;
}

std::vector<mojom::ActionType> HeartbeatTracker::GetFailureCountAction() {
  std::vector<mojom::ActionType> result;
  for (const auto& action : actions_) {
    if (failure_count_ == action->failure_count) {
      result.push_back(action->action);
    } else if (failure_count_ > action->failure_count &&
               (action->action == mojom::ActionType::kNormalReboot ||
                action->action == mojom::ActionType::kForceReboot)) {
      // It's possible that the reboot action is skipped due to the threshold
      // setting. So even if the failure count is not exactly the same as the
      // configuration, we should still report the action if it's reboot action.
      result.push_back(action->action);
    }
  }

  return result;
}

}  // namespace heartd
