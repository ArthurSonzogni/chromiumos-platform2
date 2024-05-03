// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MODEM_TRACKER_H_
#define MODEMFWD_MODEM_TRACKER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>
#include <shill/dbus-proxies.h>

#include "modemmanager/dbus-proxies.h"

namespace modemfwd {

using OnModemCarrierIdReadyCallback = base::RepeatingCallback<void(
    std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface>)>;

using OnModemDeviceSeenCallback =
    base::RepeatingCallback<void(std::string, std::string)>;

using OnModemStateChangeCallback =
    base::RepeatingCallback<void(std::string, Modem::State)>;

using OnModemPowerStateChangeCallback =
    base::RepeatingCallback<void(std::string, Modem::PowerState)>;

class ModemTracker {
 public:
  ModemTracker(
      scoped_refptr<dbus::Bus> bus,
      OnModemCarrierIdReadyCallback on_modem_carrier_id_ready_callback,
      OnModemDeviceSeenCallback on_modem_device_seen_callback,
      OnModemStateChangeCallback on_modem_state_change_callback,
      OnModemPowerStateChangeCallback on_modem_power_state_change_callback);
  ModemTracker(const ModemTracker&) = delete;
  ModemTracker& operator=(const ModemTracker&) = delete;

  ~ModemTracker() = default;

 private:
  // Called when shill appears or disappears.
  void OnServiceAvailable(bool available);

  // Called when a property on the shill manager changes.
  void OnManagerPropertyChanged(const std::string& property_name,
                                const brillo::Any& property_value);

  // Called when a property on a registered shill cellular device changes.
  void OnDevicePropertyChanged(dbus::ObjectPath device_path,
                               const std::string& property_name,
                               const brillo::Any& property_value);

  // Called when the device list changes.
  void OnDeviceListChanged(const std::vector<dbus::ObjectPath>& new_list);

  // Called when a property on the ModemManager modem changes
  void OnModemPropertyChanged(
      dbus::ObjectPath device_path,
      org::freedesktop::ModemManager1::ModemProxyInterface*
          modem_proxy_interface,
      const std::string& prop);

  void DelayedSimCheck(dbus::ObjectPath device_path);

  // Check if modem object path has changed, if so update its modem proxy
  void UpdateModemProxySingleDevice(dbus::ObjectPath device_path);

  void UpdateModemProxyMultiDevice(
      const std::vector<dbus::ObjectPath>& device_list);

  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::flimflam::ManagerProxy> shill_proxy_;
  OnModemCarrierIdReadyCallback on_modem_carrier_id_ready_callback_;
  OnModemDeviceSeenCallback on_modem_device_seen_callback_;
  OnModemStateChangeCallback on_modem_state_change_callback_;
  OnModemPowerStateChangeCallback on_modem_power_state_change_callback_;

  // Store the Carrier UUID for each modem Device.
  std::map<dbus::ObjectPath, std::string> modem_objects_;

  // Store modem object proxy
  std::map<dbus::ObjectPath,
           std::unique_ptr<org::freedesktop::ModemManager1::ModemProxy>>
      modem_proxys_;

  base::WeakPtrFactory<ModemTracker> weak_ptr_factory_;
};

}  // namespace modemfwd

#endif  // MODEMFWD_MODEM_TRACKER_H_
