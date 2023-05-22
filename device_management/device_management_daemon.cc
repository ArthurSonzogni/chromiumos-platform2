// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_management/dbus_interface.h"
#include "device_management/device_management_adaptor.h"
#include "device_management/device_management_daemon.h"
#include "device_management/device_management_service.h"
#include "libhwsec/factory/factory_impl.h"

#include <memory>

#include <brillo/daemons/dbus_daemon.h>

namespace device_management {
DeviceManagementDaemon::DeviceManagementDaemon()
    : brillo::DBusServiceDaemon(kDeviceManagementServiceName),
      service_(std::make_unique<DeviceManagementService>()) {}
DeviceManagementDaemon::~DeviceManagementDaemon() {}

void DeviceManagementDaemon::OnShutdown(int* exit_code) {
  VLOG(1) << "Shutting down device_management service";
  brillo::DBusServiceDaemon::OnShutdown(exit_code);
}
void DeviceManagementDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  VLOG(1) << "Creating service...";

  hwsec_factory_ = std::make_unique<hwsec::FactoryImpl>();
  hwsec_ = hwsec_factory_->GetCryptohomeFrontend();

  CHECK(hwsec_);

  service_ = std::make_unique<DeviceManagementService>();
  service_->Initialize(*hwsec_);

  VLOG(1) << "Registering dbus objects...";

  adaptor_ =
      std::make_unique<DeviceManagementServiceAdaptor>(bus_, service_.get());
  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
  VLOG(1) << "Registering dbus objects complete";
}
}  // namespace device_management
