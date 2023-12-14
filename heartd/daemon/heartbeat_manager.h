// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_HEARTBEAT_MANAGER_H_
#define HEARTD_DAEMON_HEARTBEAT_MANAGER_H_

#include <memory>
#include <unordered_map>

#include <mojo/public/cpp/bindings/pending_receiver.h>

#include "heartd/daemon/heartbeat_tracker.h"
#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

constexpr base::TimeDelta kVerificationPeriod = base::Seconds(60);

class HeartbeatManager {
 public:
  HeartbeatManager();
  HeartbeatManager(const HeartbeatManager&) = delete;
  HeartbeatManager& operator=(const HeartbeatManager&) = delete;
  ~HeartbeatManager();

  // Returns if the pacemaker is bound for the service. This is used to check if
  // it's a repeated registration.
  bool IsPacemakerBound(ash::heartd::mojom::ServiceName name);

  // Establishes the HeartbeatTracker for the registration.
  void EstablishHeartbeatTracker(
      ash::heartd::mojom::ServiceName name,
      mojo::PendingReceiver<ash::heartd::mojom::Pacemaker> receiver,
      ash::heartd::mojom::HeartbeatServiceArgumentPtr argument);

 private:
  // Used to hold all heartbeat trackers.
  std::unordered_map<ash::heartd::mojom::ServiceName,
                     std::unique_ptr<HeartbeatTracker>>
      heartbeat_trackers_;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_HEARTBEAT_MANAGER_H_
