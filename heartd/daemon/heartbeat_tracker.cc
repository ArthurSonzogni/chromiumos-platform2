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
    mojom::HeartbeatServiceArgumentPtr argument) {}

}  // namespace heartd
