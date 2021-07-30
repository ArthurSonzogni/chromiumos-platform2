// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_DEVICE_STATUS_MONITOR_H_
#define FEDERATED_DEVICE_STATUS_MONITOR_H_

#include <base/memory/weak_ptr.h>
#include <base/sequence_checker.h>

namespace dbus {
class ObjectProxy;
class Signal;
class Bus;
}  // namespace dbus

namespace federated {

// Monitors the device status and answers whether a federated computation task
// should start or early stop.
// Currently this class only monitors power supply, other info (e.g. memory) can
// be added in the future.
class DeviceStatusMonitor {
 public:
  explicit DeviceStatusMonitor(dbus::Bus* bus);
  DeviceStatusMonitor(const DeviceStatusMonitor&) = delete;
  DeviceStatusMonitor& operator=(const DeviceStatusMonitor&) = delete;
  ~DeviceStatusMonitor();

  // Called before training to see if the device is in a good condition, and
  // during the training to see if it should be aborted.
  bool TrainingConditionsSatisfied() const;

 private:
  // Invoked by powerd dbus signals to update the power info.
  void OnPowerSupplyReceived(dbus::Signal* signal);

  // Obtained from dbus, should never delete it.
  dbus::ObjectProxy* const powerd_dbus_proxy_;

  // Whether the device has enough battery for a federated computation task.
  // Updated in `OnPowerSupplyReceived` and used in
  // `TrainingConditionsSatisfied`.
  bool enough_battery_;

  const base::WeakPtrFactory<DeviceStatusMonitor> weak_ptr_factory_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace federated

#endif  // FEDERATED_DEVICE_STATUS_MONITOR_H_
