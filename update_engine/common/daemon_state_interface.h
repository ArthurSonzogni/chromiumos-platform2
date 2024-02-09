// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_DAEMON_STATE_INTERFACE_H_
#define UPDATE_ENGINE_COMMON_DAEMON_STATE_INTERFACE_H_

#include "update_engine/common/service_observer_interface.h"

#include <set>

namespace chromeos_update_engine {

class DaemonStateInterface {
 public:
  DaemonStateInterface(const DaemonStateInterface&) = delete;
  DaemonStateInterface& operator=(const DaemonStateInterface&) = delete;

  virtual ~DaemonStateInterface() = default;

  // Start the daemon loop. Should be called only once to start the daemon's
  // main functionality.
  virtual bool StartUpdater() = 0;

  // Add and remove an observer. All the registered observers will be called
  // whenever there's a new status to update.
  virtual void AddObserver(ServiceObserverInterface* observer) = 0;
  virtual void RemoveObserver(ServiceObserverInterface* observer) = 0;

  // Return the set of current observers.
  virtual const std::set<ServiceObserverInterface*>& service_observers() = 0;

 protected:
  DaemonStateInterface() = default;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_DAEMON_STATE_INTERFACE_H_
