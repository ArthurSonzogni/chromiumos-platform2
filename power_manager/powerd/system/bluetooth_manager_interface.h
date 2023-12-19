// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_BLUETOOTH_MANAGER_INTERFACE_H_
#define POWER_MANAGER_POWERD_SYSTEM_BLUETOOTH_MANAGER_INTERFACE_H_

namespace power_manager::system {

// Represents btmanagerd, the daemon which manages Bluetooth.
class BluetoothManagerInterface {
 public:
  virtual ~BluetoothManagerInterface() = default;

  // Register for btmanagerd state changes.
  virtual void RegisterBluetoothManagerCallback(bool available) = 0;

  // Runs when this provider has been registered to receive HCI device status
  // change callbacks.
  virtual void OnRegisteredBluetoothManagerCallback(
      dbus::Response* response) = 0;

  // Runs when an HCI device is enabled or disabled.
  virtual void OnHciEnabledChanged(
      dbus::MethodCall* method_call,
      dbus::ExportedObject::ResponseSender response_sender) = 0;

  // Whether or not this provider is registered for updates from the Bluetooth
  // manager.
  bool is_registered_with_bluetooth_manager_ = false;
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_BLUETOOTH_MANAGER_INTERFACE_H_
