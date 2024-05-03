// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/logging.h"
#include "modemfwd/modem.h"
#include "modemfwd/modem_tracker.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/task/single_thread_task_runner.h>
#include <brillo/variant_dictionary.h>
#include <dbus/shill/dbus-constants.h>
#include <ModemManager/ModemManager.h>

namespace modemfwd {

namespace {

void OnSignalConnected(const std::string& interface_name,
                       const std::string& signal_name,
                       bool success) {
  DVLOG(1) << (success ? "Connected" : "Failed to connect") << " to signal "
           << signal_name << " of " << interface_name;
}

}  // namespace

ModemTracker::ModemTracker(
    scoped_refptr<dbus::Bus> bus,
    OnModemCarrierIdReadyCallback on_modem_carrier_id_ready_callback,
    OnModemDeviceSeenCallback on_modem_device_seen_callback,
    OnModemStateChangeCallback on_modem_state_change_callback,
    OnModemPowerStateChangeCallback on_modem_power_state_change_callback)
    : bus_(bus),
      shill_proxy_(new org::chromium::flimflam::ManagerProxy(bus)),
      on_modem_carrier_id_ready_callback_(on_modem_carrier_id_ready_callback),
      on_modem_device_seen_callback_(on_modem_device_seen_callback),
      on_modem_state_change_callback_(on_modem_state_change_callback),
      on_modem_power_state_change_callback_(
          on_modem_power_state_change_callback),
      weak_ptr_factory_(this) {
  shill_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(base::BindOnce(
      &ModemTracker::OnServiceAvailable, weak_ptr_factory_.GetWeakPtr()));
}

void ModemTracker::OnServiceAvailable(bool available) {
  if (!available) {
    LOG(WARNING) << "shill disappeared";
    modem_objects_.clear();
    modem_proxys_.clear();
    return;
  }

  shill_proxy_->RegisterPropertyChangedSignalHandler(
      base::BindRepeating(&ModemTracker::OnManagerPropertyChanged,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&OnSignalConnected));

  brillo::ErrorPtr error;
  brillo::VariantDictionary properties;
  if (!shill_proxy_->GetProperties(&properties, &error)) {
    LOG(ERROR) << "Could not get property list from shill: "
               << error->GetMessage();
    return;
  }

  OnDeviceListChanged(properties[shill::kDevicesProperty]
                          .TryGet<std::vector<dbus::ObjectPath>>());

  // Update |modem_proxys_| in case modem object path has changed.
  UpdateModemProxyMultiDevice(properties[shill::kDevicesProperty]
                                  .TryGet<std::vector<dbus::ObjectPath>>());
}

void ModemTracker::UpdateModemProxySingleDevice(dbus::ObjectPath device_path) {
  brillo::VariantDictionary properties;
  std::string modem_object_path;
  auto device =
      std::make_unique<org::chromium::flimflam::DeviceProxy>(bus_, device_path);

  if (!device->GetProperties(&properties, NULL))
    return;
  if (properties[shill::kTypeProperty].TryGet<std::string>() !=
      shill::kTypeCellular) {
    return;
  }
  if (!properties[shill::kDBusObjectProperty].GetValue(&modem_object_path)) {
    ELOG(ERROR) << "Could not get modem object path for device "
                << device_path.value();
    return;
  }
  // Empty modem object path, reset modem proxy
  if (modem_object_path.empty()) {
    modem_proxys_.erase(device_path);
    return;
  }

  // Modem object path did not change, no need to update proxy
  auto modem_proxy = modem_proxys_.find(device_path);
  if (modem_proxy != modem_proxys_.end()) {
    auto old_path = modem_proxy->second->GetObjectPath().value();
    ELOG(INFO) << __func__ << ": modem object old path: " << old_path;
    if (old_path == modem_object_path)
      return;
  }

  ELOG(INFO) << __func__ << ": modem object new path: " << modem_object_path;
  auto modem_proxy_ptr =
      std::make_unique<org::freedesktop::ModemManager1::ModemProxy>(
          bus_, MM_DBUS_SERVICE, dbus::ObjectPath(modem_object_path));
  if (!modem_proxy_ptr) {
    ELOG(ERROR) << ": could not create modem proxy for object path: "
                << modem_object_path;
    return;
  }

  // Start listening property change on the new modem object
  modem_proxy_ptr->InitializeProperties(
      base::BindRepeating(&ModemTracker::OnModemPropertyChanged,
                          weak_ptr_factory_.GetWeakPtr(), device_path));

  std::string device_id;
  if (properties[shill::kDeviceIdProperty].GetValue(&device_id)) {
    // Get current power state of the new modem object
    if (modem_proxy_ptr->GetProperties()->power_state.GetAndBlock()) {
      on_modem_power_state_change_callback_.Run(
          device_id,
          static_cast<Modem::PowerState>(modem_proxy_ptr->power_state()));
    }
    // Get current modem state of the new modem object
    if (modem_proxy_ptr->GetProperties()->state.GetAndBlock()) {
      on_modem_state_change_callback_.Run(
          device_id, static_cast<Modem::State>(modem_proxy_ptr->state()));
    }
  }
  // Save the updated modem proxy
  modem_proxys_[device_path] = std::move(modem_proxy_ptr);
}

void ModemTracker::UpdateModemProxyMultiDevice(
    const std::vector<dbus::ObjectPath>& device_list) {
  for (const auto& device_path : device_list) {
    UpdateModemProxySingleDevice(device_path);
  }
}

void ModemTracker::OnManagerPropertyChanged(const std::string& property_name,
                                            const brillo::Any& property_value) {
  if (property_name == shill::kDevicesProperty)
    OnDeviceListChanged(property_value.TryGet<std::vector<dbus::ObjectPath>>());
}

void ModemTracker::OnModemPropertyChanged(
    dbus::ObjectPath device_path,
    org::freedesktop::ModemManager1::ModemProxyInterface* modem_proxy_interface,
    const std::string& prop) {
  auto device =
      std::make_unique<org::chromium::flimflam::DeviceProxy>(bus_, device_path);
  std::string device_id;
  brillo::VariantDictionary properties;
  // Cannot get valid device id
  if (!(device->GetProperties(&properties, NULL) &&
        properties[shill::kDeviceIdProperty].GetValue(&device_id))) {
    return;
  }

  auto modem_proxy = modem_proxys_.find(device_path);
  if (modem_proxy == modem_proxys_.end()) {
    return;
  }
  if (prop == MM_MODEM_PROPERTY_POWERSTATE || prop == MM_MODEM_PROPERTY_STATE) {
    // Update both power state and modem state whenever one of them has changed
    // in case a property update signal is missed
    if (modem_proxy->second->GetProperties()->power_state.is_valid()) {
      Modem::PowerState power_state =
          static_cast<Modem::PowerState>(modem_proxy->second->power_state());
      ELOG(INFO) << __func__ << ": new power state: " << power_state;
      on_modem_power_state_change_callback_.Run(device_id, power_state);
    }
    if (modem_proxy->second->GetProperties()->state.is_valid()) {
      Modem::State modem_state =
          static_cast<Modem::State>(modem_proxy->second->state());
      ELOG(INFO) << __func__ << ": new modem state: " << modem_state;
      on_modem_state_change_callback_.Run(device_id, modem_state);
    }
  }
}

void ModemTracker::OnDevicePropertyChanged(dbus::ObjectPath device_path,
                                           const std::string& property_name,
                                           const brillo::Any& property_value) {
  // Modem object has changed. Update modem proxy
  if (property_name == shill::kDBusObjectProperty) {
    UpdateModemProxySingleDevice(device_path);
  }

  // Listen for the HomeProvider change triggered by a SIM change
  if (property_name != shill::kHomeProviderProperty)
    return;

  auto current_carrier_id = modem_objects_.find(device_path);
  if (current_carrier_id == modem_objects_.end())
    return;

  std::map<std::string, std::string> operator_info(
      property_value.TryGet<std::map<std::string, std::string>>());
  std::string carrier_id = operator_info[shill::kOperatorUuidKey];
  if (carrier_id == current_carrier_id->second)
    return;

  current_carrier_id->second = carrier_id;

  ELOG(INFO) << "Carrier UUID changed to [" << carrier_id << "] for device "
             << device_path.value();

  // Skip update if no carrier info
  if (carrier_id.empty())
    return;

  // Trigger the firmware update
  auto device =
      std::make_unique<org::chromium::flimflam::DeviceProxy>(bus_, device_path);
  on_modem_carrier_id_ready_callback_.Run(std::move(device));
}

void ModemTracker::DelayedSimCheck(dbus::ObjectPath device_path) {
  brillo::VariantDictionary properties;
  bool sim_present;
  auto device =
      std::make_unique<org::chromium::flimflam::DeviceProxy>(bus_, device_path);

  if (!device->GetProperties(&properties, NULL) ||
      !properties[shill::kSIMPresentProperty].GetValue(&sim_present)) {
    LOG(ERROR) << "Could not get SIMPresent property for device "
               << device_path.value();
    return;
  }

  if (!sim_present) {
    on_modem_carrier_id_ready_callback_.Run(std::move(device));
  }
}

void ModemTracker::OnDeviceListChanged(
    const std::vector<dbus::ObjectPath>& new_list) {
  std::map<dbus::ObjectPath, std::string> new_modems;
  for (const auto& device_path : new_list) {
    if (modem_objects_.find(device_path) != modem_objects_.end()) {
      // Keep existing devices in the new list.
      new_modems[device_path] = modem_objects_[device_path];
      continue;
    }

    // See if the modem object is of cellular type.
    auto device = std::make_unique<org::chromium::flimflam::DeviceProxy>(
        bus_, device_path);
    brillo::ErrorPtr error;
    brillo::VariantDictionary properties;
    if (!device->GetProperties(&properties, &error)) {
      LOG(ERROR) << "Could not get property list for device "
                 << device_path.value() << ": " << error->GetMessage();
      continue;
    }

    if (properties[shill::kTypeProperty].TryGet<std::string>() !=
        shill::kTypeCellular) {
      DVLOG(1) << "Device " << device_path.value()
               << " is not cellular type, ignoring";
      continue;
    }

    std::string device_id;
    std::string equipment_id;
    if (!properties[shill::kDeviceIdProperty].GetValue(&device_id) ||
        !properties[shill::kEquipmentIdProperty].GetValue(&equipment_id)) {
      LOG(ERROR) << "Modem " << device_path.value()
                 << " has no device ID or no equipment ID, ignoring";
      continue;
    }
    on_modem_device_seen_callback_.Run(device_id, equipment_id);

    std::map<std::string, std::string> operator_info;
    if (!properties[shill::kHomeProviderProperty].GetValue(&operator_info))
      continue;

    bool sim_present;
    if (!properties[shill::kSIMPresentProperty].GetValue(&sim_present)) {
      LOG(ERROR) << "Modem " << device_path.value()
                 << " has no SIM Present property, ignoring";
      continue;
    }
    if (!sim_present) {
      // Test the SIMPresent property again after short delay before fw update
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          BindOnce(&ModemTracker::DelayedSimCheck,
                   weak_ptr_factory_.GetWeakPtr(), device_path),
          base::Seconds(10));
    }

    // Record the modem device with its current carrier UUID
    std::string carrier_id = operator_info[shill::kOperatorUuidKey];
    new_modems[device_path] = carrier_id;

    // Listen to the Device HomeProvider property in order to detect future
    // SIM swaps.
    device->RegisterPropertyChangedSignalHandler(
        base::BindRepeating(&ModemTracker::OnDevicePropertyChanged,
                            weak_ptr_factory_.GetWeakPtr(), device_path),
        base::BindRepeating(&OnSignalConnected));

    // Try to update if carrier is known
    if (!carrier_id.empty())
      on_modem_carrier_id_ready_callback_.Run(std::move(device));
  }
  modem_objects_ = new_modems;
}

}  // namespace modemfwd
