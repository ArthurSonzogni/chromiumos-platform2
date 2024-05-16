// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/error.h"
#include "modemfwd/heartbeat_task.h"

#include <base/functional/callback.h>
#include <base/strings/stringprintf.h>

#include "modemfwd/daemon_delegate.h"
#include "modemfwd/logging.h"

namespace modemfwd {

HeartbeatTask::HeartbeatTask(Delegate* delegate,
                             Modem* modem,
                             Metrics* metrics,
                             HeartbeatConfig config)
    : delegate_(delegate), modem_(modem), metrics_(metrics), config_(config) {}

void HeartbeatTask::Start() {
  timer_.Start(FROM_HERE, config_.interval,
               base::BindRepeating(&HeartbeatTask::DoHealthCheck,
                                   weak_ptr_factory_.GetWeakPtr()));
}

void HeartbeatTask::Stop() {
  timer_.Stop();
}

void HeartbeatTask::DoHealthCheck() {
  DCHECK(modem_->SupportsHealthCheck());
  EVLOG(1) << "Performing health check on modem [" << modem_->GetDeviceId()
           << "]";

  if (modem_->CheckHealth()) {
    // Health check succeeded. Reset failures.
    consecutive_heartbeat_failures_ = 0;
    return;
  }
  consecutive_heartbeat_failures_++;

  // Creating this error triggers uploading logs for anomaly detection
  if (consecutive_heartbeat_failures_ == 1) {
    auto err = Error::Create(
        FROM_HERE, error::kHeartbeatHealthCheck1stFailure,
        base::StringPrintf("Modem [%s] No response for health check, 1st time",
                           modem_->GetDeviceId().c_str()));
  } else if (consecutive_heartbeat_failures_ == 2) {
    auto err = Error::Create(
        FROM_HERE, error::kHeartbeatHealthCheck2ndFailure,
        base::StringPrintf(
            "Modem [%s] No response for health check, 2 consecutive times",
            modem_->GetDeviceId().c_str()));
  } else if (consecutive_heartbeat_failures_ == 3) {
    auto err = Error::Create(
        FROM_HERE, error::kHeartbeatHealthCheck3rdFailure,
        base::StringPrintf(
            "Modem [%s] No response for health check, 3 consecutive times",
            modem_->GetDeviceId().c_str()));
  }

  if (consecutive_heartbeat_failures_ < config_.max_failures) {
    // Wait for now.
    return;
  }

  LOG(ERROR) << "Modem [" << modem_->GetDeviceId()
             << "] is unresponsive. Trying to recover.";

  if (delegate_->ResetModem(modem_->GetDeviceId())) {
    // Modem came back after reset.
    LOG(INFO) << "Reboot succeeded";
    consecutive_heartbeat_failures_ = 0;
    metrics_->SendModemRecoveryState(
        metrics::ModemRecoveryState::kRecoveryStateSuccess);
    return;
  }

  // Modem did not respond to a reset either.
  LOG(INFO) << "Reset failed";
  metrics_->SendModemRecoveryState(
      metrics::ModemRecoveryState::kRecoveryStateFailure);
}

}  // namespace modemfwd
