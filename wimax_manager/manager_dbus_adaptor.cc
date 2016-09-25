// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wimax_manager/manager_dbus_adaptor.h"

#include <memory>
#include <vector>

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "wimax_manager/device.h"
#include "wimax_manager/device_dbus_adaptor.h"
#include "wimax_manager/manager.h"

using std::vector;

namespace wimax_manager {

ManagerDBusAdaptor::ManagerDBusAdaptor(DBus::Connection *connection,
                                       Manager *manager)
    : DBusAdaptor(connection, kWiMaxManagerServicePath),
      manager_(manager) {
  Devices = vector<DBus::Path>();
}

void ManagerDBusAdaptor::UpdateDevices() {
  vector<DBus::Path> device_paths;
  const vector<std::unique_ptr<Device>> &devices = manager_->devices();
  for (const auto &device : devices) {
    device_paths.push_back(device->dbus_object_path());
  }
  Devices = device_paths;
  DevicesChanged(device_paths);
}

}  // namespace wimax_manager
