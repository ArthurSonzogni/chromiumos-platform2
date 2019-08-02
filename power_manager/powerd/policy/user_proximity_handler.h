// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_USER_PROXIMITY_HANDLER_H_
#define POWER_MANAGER_POWERD_POLICY_USER_PROXIMITY_HANDLER_H_

#include <memory>
#include <unordered_map>

#include <base/macros.h>

#include "power_manager/powerd/policy/user_proximity_voting.h"
#include "power_manager/powerd/system/sar_watcher.h"
#include "power_manager/powerd/system/user_proximity_observer.h"

namespace power_manager {

namespace system {
class UserProximityWatcherInterface;
}

namespace policy {

class WifiController;

// UserProximityHandler responds to events from SAR (Specific Absorption Rate)
// sensors, and routes them to controllers responsible for adjusting radio
// transmit power in response to the physical proximity of the user to their
// Chromebook.
class UserProximityHandler : public system::UserProximityObserver {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void ProximitySensorDetected(UserProximity value) = 0;
    virtual void HandleProximityChange(UserProximity value) = 0;
  };
  UserProximityHandler();
  ~UserProximityHandler() override;

  // Delegates may be == nullptr. Ownership remains with the caller.
  bool Init(system::UserProximityWatcherInterface* user_prox_watcher,
            Delegate* wifi_delegate,
            Delegate* lte_delegate);

  // UserProximityObserver implementations:
  void OnNewSensor(int id, uint32_t roles) override;
  void OnProximityEvent(int id, UserProximity value) override;

 private:
  Delegate* wifi_delegate_ = nullptr;  // Not owned.
  Delegate* lte_delegate_ = nullptr;   // Not owned.
  // Keeps a correspondence among sensor ID (key) and which subsystems
  // it is sending proximity signal for (value).
  std::unordered_map<int, uint32_t> sensor_roles_;
  UserProximityVoting wifi_proximity_voting_;
  UserProximityVoting lte_proximity_voting_;
  system::UserProximityWatcherInterface* user_proximity_watcher_ =
      nullptr;  //  Not owned.

  DISALLOW_COPY_AND_ASSIGN(UserProximityHandler);
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_USER_PROXIMITY_HANDLER_H_
