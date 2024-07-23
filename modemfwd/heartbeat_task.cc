// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/heartbeat_task.h"

#include <vector>

#include <base/containers/contains.h>
#include <base/functional/callback.h>
#include <base/strings/stringprintf.h>
#include <dbus/modemfwd/dbus-constants.h>

#include "modemfwd/daemon_delegate.h"
#include "modemfwd/error.h"
#include "modemfwd/logging.h"

namespace modemfwd {

HeartbeatTask::HeartbeatTask(Delegate* delegate,
                             Modem* modem,
                             Metrics* metrics,
                             HeartbeatConfig config)
    : Task(delegate, "heartbeat-" + modem->GetDeviceId(), kTaskTypeHeartbeat),
      modem_(modem),
      metrics_(metrics),
      config_(config) {}

// static
std::unique_ptr<HeartbeatTask> HeartbeatTask::Create(
    Delegate* delegate,
    Modem* modem,
    ModemHelperDirectory* helper_directory,
    Metrics* metrics) {
  if (!modem->SupportsHealthCheck())
    return nullptr;

  auto helper = helper_directory->GetHelperForDeviceId(modem->GetDeviceId());
  if (!helper)
    return nullptr;

  auto heartbeat_config = helper->GetHeartbeatConfig();
  if (!heartbeat_config.has_value())
    return nullptr;

  return std::unique_ptr<HeartbeatTask>(
      new HeartbeatTask(delegate, modem, metrics, *heartbeat_config));
}

void HeartbeatTask::Start() {
  delegate()->RegisterOnStartFlashingCallback(
      modem_->GetEquipmentId(),
      BindOnce(&HeartbeatTask::Stop, weak_ptr_factory_.GetWeakPtr()));
  delegate()->RegisterOnModemStateChangedCallback(
      modem_, base::BindRepeating(&HeartbeatTask::OnModemStateChanged,
                                  weak_ptr_factory_.GetWeakPtr()));
  delegate()->RegisterOnModemPowerStateChangedCallback(
      modem_, base::BindRepeating(&HeartbeatTask::OnModemStateChanged,
                                  weak_ptr_factory_.GetWeakPtr()));
  // TODO(b/341753271): restart the task when there is a request to exit power
  // LOW state, even if it does not complete. In that case, there is no power
  // state change on the modem object. Current power state would still be LOW
  Configure();
}

void HeartbeatTask::Stop() {
  timer_.Stop();
}

void HeartbeatTask::Configure() {
  if (modem_->GetPowerState() == Modem::PowerState::LOW) {
    return;
  }

  base::TimeDelta interval;
  auto modem_state = modem_->GetState();
  std::vector<Modem::State> idle_states{
      Modem::State::REGISTERED, Modem::State::ENABLED, Modem::State::LOCKED};
  if (base::Contains(idle_states, modem_state) &&
      config_.modem_idle_interval > base::Seconds(0)) {
    interval = config_.modem_idle_interval;
  } else {
    interval = config_.interval;
  }

  ELOG(INFO) << "Modem state is: " << modem_state
             << ". Apply heartbeat check interval: " << interval;
  timer_.Start(FROM_HERE, interval,
               base::BindRepeating(&HeartbeatTask::DoHealthCheck,
                                   weak_ptr_factory_.GetWeakPtr()));
}

void HeartbeatTask::CancelOutstandingWork() {
  timer_.Stop();
  weak_ptr_factory_.InvalidateWeakPtrs();
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
  auto err = Error::Create(
      FROM_HERE, error::kHeartbeatHealthCheckFailure,
      base::StringPrintf("Modem [%s] No response for health check, "
                         "consecutive_heartbeat_failures_#%d",
                         modem_->GetDeviceId().c_str(),
                         consecutive_heartbeat_failures_));

  if (consecutive_heartbeat_failures_ < config_.max_failures) {
    // Wait for now.
    return;
  }

  LOG(ERROR) << "Modem [" << modem_->GetDeviceId()
             << "] is unresponsive. Trying to recover.";

  if (!delegate()->ResetModem(modem_->GetDeviceId())) {
    // Modem did not respond to a reset either.
    LOG(WARNING) << "Reset failed";
    metrics_->SendModemRecoveryState(
        metrics::ModemRecoveryState::kRecoveryStateFailure);
    Finish(Error::Create(FROM_HERE, error::kHeartbeatResetFailure,
                         "Modem failed to reset"));
    return;
  }

  // Modem reset successfully. The daemon will create another heartbeat
  // task when it finishes coming back up.
  LOG(INFO) << "Reboot succeeded";
  metrics_->SendModemRecoveryState(
      metrics::ModemRecoveryState::kRecoveryStateSuccess);
  Finish();
}

void HeartbeatTask::OnModemStateChanged(Modem* modem) {
  Stop();
  Configure();
}

}  // namespace modemfwd
