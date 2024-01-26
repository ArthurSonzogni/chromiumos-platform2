// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_HEARTBEAT_TRACKER_H_
#define HEARTD_DAEMON_HEARTBEAT_TRACKER_H_

#include <memory>
#include <vector>

#include <base/time/time.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

constexpr base::TimeDelta kMinVerificationWindow = base::Seconds(70);

class HeartbeatTracker : public ash::heartd::mojom::Pacemaker {
 public:
  explicit HeartbeatTracker(
      ash::heartd::mojom::ServiceName name,
      mojo::PendingReceiver<ash::heartd::mojom::Pacemaker> receiver);
  HeartbeatTracker(const HeartbeatTracker&) = delete;
  HeartbeatTracker& operator=(const HeartbeatTracker&) = delete;
  ~HeartbeatTracker();

  // ash::heartd::mojom::Pacemaker overrides:
  void SendHeartbeat(SendHeartbeatCallback callback) override;
  void StopMonitor(StopMonitorCallback callback) override;

  // Returns if pacemaker receiver is bound or not. We use this to check if it's
  // a repeated registration.
  bool IsPacemakerBound();

  // Returns if we should stop monitor or not.
  bool IsStopMonitor();

  // Rebind pacemaker receiver. This can be called when |IsPacemakerBound()|
  // returns false.
  void RebindPacemaker(
      mojo::PendingReceiver<ash::heartd::mojom::Pacemaker> receiver);

  // Set up the service argument.
  void SetupArgument(ash::heartd::mojom::HeartbeatServiceArgumentPtr argument);

  // Returns the number of heartbeat verification failure. It'll be reset to
  // zero every time when receiving a heartbeat.
  uint8_t GetFailureCount();

  // Returns the time to wait for pings before we declare one failure.
  base::TimeDelta GetVerificationWindow();

  // Verifies if the time gap between the |current_time| and the
  // |last_touch_time_| is within the |verification_window_|.
  bool VerifyTimeGap(const base::Time& current_time);

  void SetLastDryRunResponse(ash::heartd::mojom::HeartbeatResponse response);

  // Returns the actions that need to be taken at current |failure_count_|.
  std::vector<ash::heartd::mojom::ActionType> GetFailureCountActions();

  // Returns all valid actions to be executed
  std::vector<ash::heartd::mojom::ActionType> GetActions();

 private:
  // Handler when pacemaker mojo disconnects.
  void OnPacemakerDisconnect();

 private:
  // Service name.
  ash::heartd::mojom::ServiceName name_;
  // Pacemaker receiver.
  mojo::Receiver<ash::heartd::mojom::Pacemaker> receiver_;
  // Once this is set to true, the whole HeartbeatTracker instance will be
  // cleaned up by HeartbeatManager.
  bool stop_monitor_ = false;
  // Number of consecutive heartbeat verification failure.
  uint8_t failure_count_ = 0;
  // The time when receiving the last heartbeat.
  base::Time last_touch_time_;
  // What was the response of last dryrun from action to return back when asked.
  ::ash::heartd::mojom::HeartbeatResponse last_dryrun_response_;
  // For every verification, we check if there is at least one heartbeat in the
  // past |verification_window_seconds|. The minimum value of this is 70.
  base::TimeDelta verification_window_ = kMinVerificationWindow;
  // |actions_| describes what action to be taken for a specific failure count.
  std::vector<ash::heartd::mojom::ActionPtr> actions_;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_HEARTBEAT_TRACKER_H_
