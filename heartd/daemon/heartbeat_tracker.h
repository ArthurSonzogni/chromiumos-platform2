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
  // The time when receiving the last heartbeat.
  base::Time last_touch_time_;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_HEARTBEAT_TRACKER_H_
