// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_DEVICE_MANAGEMENT_DAEMON_H_
#define DEVICE_MANAGEMENT_DEVICE_MANAGEMENT_DAEMON_H_

#include "device_management/dbus_interface.h"
#include "device_management/device_management_adaptor.h"
#include "device_management/device_management_service.h"
#include "device_management/install_attributes/platform.h"

#include <memory>

#include <brillo/daemons/dbus_daemon.h>

namespace device_management {

// This class runs the D-Bus service of device_management daemon.
class DeviceManagementDaemon : public brillo::DBusServiceDaemon {
 public:
  DeviceManagementDaemon();
  DeviceManagementDaemon(const DeviceManagementDaemon&) = delete;
  DeviceManagementDaemon& operator=(const DeviceManagementDaemon&) = delete;

  ~DeviceManagementDaemon() override;

 protected:
  void OnShutdown(int* exit_code) override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

 private:
  std::unique_ptr<hwsec::Factory> hwsec_factory_;
  std::unique_ptr<const hwsec::CryptohomeFrontend> hwsec_;
  std::unique_ptr<Platform> platform_;
  std::unique_ptr<DeviceManagementService> service_;
  std::unique_ptr<DeviceManagementServiceAdaptor> adaptor_;
};

}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_DEVICE_MANAGEMENT_DAEMON_H_
