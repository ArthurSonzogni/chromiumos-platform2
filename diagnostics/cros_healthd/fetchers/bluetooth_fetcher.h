// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BLUETOOTH_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BLUETOOTH_FETCHER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

class BluezInfoManager {
 public:
  BluezInfoManager(const BluezInfoManager&) = delete;
  BluezInfoManager& operator=(const BluezInfoManager&) = delete;
  virtual ~BluezInfoManager() = default;

  static std::unique_ptr<BluezInfoManager> Create(
      org::bluezProxy* bluetooth_proxy);
  virtual std::vector<org::bluez::Adapter1ProxyInterface*> adapters();
  virtual std::vector<org::bluez::Device1ProxyInterface*> devices();
  virtual std::vector<org::bluez::AdminPolicyStatus1ProxyInterface*>
  admin_policies();
  virtual std::vector<org::bluez::LEAdvertisingManager1ProxyInterface*>
  advertisings();
  virtual std::vector<org::bluez::Battery1ProxyInterface*> batteries();

  // Convert std::string to |BluetoothDeviceType| enum.
  chromeos::cros_healthd::mojom::BluetoothDeviceType GetDeviceType(
      const std::string& type);

 protected:
  BluezInfoManager() = default;

 private:
  // Parse the Bluetooth information.
  chromeos::cros_healthd::mojom::BluetoothResultPtr ParseBluetoothInstance();

  // Parse Bluetooth info from different interfaces and store in the map.
  void ParseDevicesInfo(
      std::map<
          dbus::ObjectPath,
          std::vector<chromeos::cros_healthd::mojom::BluetoothDeviceInfoPtr>>&
          out_connected_devices);
  void ParseServiceAllowList(
      std::map<dbus::ObjectPath, std::vector<std::string>>&
          out_service_allow_list);
  void ParseSupportedCapabilities(
      std::map<dbus::ObjectPath,
               chromeos::cros_healthd::mojom::SupportedCapabilitiesPtr>&
          out_supported_capabilities);
  void ParseBatteryPercentage(
      std::map<dbus::ObjectPath, uint8_t>& out_battery_percentage);

  std::vector<org::bluez::Adapter1ProxyInterface*> adapters_;
  std::vector<org::bluez::Device1ProxyInterface*> devices_;
  std::vector<org::bluez::AdminPolicyStatus1ProxyInterface*> admin_policies_;
  std::vector<org::bluez::LEAdvertisingManager1ProxyInterface*> advertisings_;
  std::vector<org::bluez::Battery1ProxyInterface*> batteries_;

  friend class BluetoothFetcher;
};

// The BluetoothFetcher class is responsible for gathering a device's Bluetooth
// information.
class BluetoothFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  // Returns the Bluetooth information.
  chromeos::cros_healthd::mojom::BluetoothResultPtr FetchBluetoothInfo(
      std::unique_ptr<BluezInfoManager> bluez_manager = nullptr);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BLUETOOTH_FETCHER_H_
