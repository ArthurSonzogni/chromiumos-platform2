// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/smart_battery/smart_battery_daemon.h"

#include <utility>

#include <chromeos/dbus/service_constants.h>

namespace ml {

SmartBatteryDaemon::SmartBatteryDaemon()
    : DBusServiceDaemon(kMachineLearningSmartBatteryServiceName) {}

SmartBatteryDaemon::~SmartBatteryDaemon() = default;

void SmartBatteryDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  auto dbus_object = std::make_unique<brillo::dbus_utils::DBusObject>(
      /*object_manager=*/nullptr, bus_,
      org::chromium::MachineLearningSmartBatteryAdaptor::GetObjectPath());

  smart_battery_service_ =
      std::make_unique<SmartBatteryService>(std::move(dbus_object));

  smart_battery_service_->RegisterAsync(sequencer->GetHandler(
      /*descriptive_message=*/"SmartBatteryService.RegisterAsync() failed.",
      /*failure_is_fatal*/ true));
}

void SmartBatteryDaemon::OnShutdown(int* return_code) {
  DBusServiceDaemon::OnShutdown(return_code);
  smart_battery_service_.reset();
}

}  // namespace ml
