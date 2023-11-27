// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_FLOSS_BATTERY_PROVIDER_MANAGER_INTERFACE_H_
#define POWER_MANAGER_POWERD_SYSTEM_FLOSS_BATTERY_PROVIDER_MANAGER_INTERFACE_H_

#include <string>

namespace power_manager::system {

// Represents Floss's BatteryProviderManager's DBus API.
class FlossBatteryProviderManagerInterface {
 public:
  virtual ~FlossBatteryProviderManagerInterface() = default;

  // DBus methods.
  static constexpr char kFlossBatteryProviderManagerRegisterBatteryProvider[] =
      "RegisterBatteryProvider";
  static constexpr char kFlossBatteryProviderManagerUpdateDeviceBattery[] =
      "SetBatteryInfo";

  // DBus callback methods.
  static constexpr char kFlossBatteryProviderManagerRefreshBatteryInfo[] =
      "RefreshBatteryInfo";

  // Register for Floss BatteryProviderManager updates.
  virtual void RegisterAsBatteryProvider(const std::string& interface_name,
                                         bool available) = 0;

  // Runs when this provider has been registered for BatteryProviderManager
  // updates.
  virtual void OnRegisteredAsBatteryProvider(dbus::Response* response) = 0;

  // Runs when Floss BatteryProviderManager requests updated battery info.
  virtual void RefreshBatteryInfo(
      dbus::MethodCall* method_call,
      dbus::ExportedObject::ResponseSender response_sender) = 0;

  // Whether or not this provider is registered with the battery provider
  // manager.
  bool is_registered_with_provider_manager_ = false;
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_FLOSS_BATTERY_PROVIDER_MANAGER_INTERFACE_H_
