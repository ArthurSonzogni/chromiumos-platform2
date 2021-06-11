// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_SMART_BATTERY_SMART_BATTERY_DAEMON_H_
#define ML_SMART_BATTERY_SMART_BATTERY_DAEMON_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>

#include "ml/smart_battery/smart_battery_service.h"

namespace ml {

class SmartBatteryDaemon : public brillo::DBusServiceDaemon {
 public:
  SmartBatteryDaemon();
  SmartBatteryDaemon(const SmartBatteryDaemon&) = delete;
  SmartBatteryDaemon& operator=(const SmartBatteryDaemon&) = delete;

  ~SmartBatteryDaemon();

 private:
  // brillo::DBusServiceDaemon:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;
  void OnShutdown(int* return_code) override;

  std::unique_ptr<SmartBatteryService> smart_battery_service_;
};

}  // namespace ml

#endif  // ML_SMART_BATTERY_SMART_BATTERY_DAEMON_H_
